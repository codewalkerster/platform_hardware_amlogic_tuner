package com.droidlogic.tuner.psi;

import android.media.tv.tuner.Tuner;
import android.media.tv.tuner.filter.Filter;
import android.media.tv.tuner.filter.FilterCallback;
import android.media.tv.tuner.filter.FilterConfiguration;
import android.media.tv.tuner.filter.FilterEvent;
import android.media.tv.tuner.filter.SectionEvent;
import android.media.tv.tuner.filter.SectionSettingsWithSectionBits;
import android.media.tv.tuner.filter.TsFilterConfiguration;
import android.os.Bundle;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.Log;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.channel.Channel;
import com.droidlogic.tuner.channel.ChannelManager;
import com.droidlogic.tuner.scan.ScanManager;
import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.ThreadManager;
import com.droidlogic.tuner.utils.ThreadManager.TunerExecutor;

import java.util.ArrayList;
import java.util.List;

public class SiParser {
    private static final String TAG = Constans.TAG;
    private static SiParser mInstance = null;
    private TunerExecutor mExecutor;
    private SiParserEvent mParserEvent;
    private Filter mPatFilter = null;
    private List<Filter> mPmtFilters = new ArrayList<>();
    private Filter mSdtFilter = null;
    private final Object mChannelParserLock = new Object();

    private final static int STATE_NONE = 0;
    private final static int STATE_PROGRESS = 1;
    private final static int STATE_DONE = 2;
    private int mChannelParsing = STATE_NONE;

    private static final int SECTION_FILTER_BUFFER_SIZE = 32 * 1024;
    private int[] videoStreamTypes = {0x01, 0x02, 0x1b, 0x24};
    private int[] audioStreamTypes = {0x03, 0x04, 0x06, 0x0e, 0x0f, 0x011, 0x81, 0x87};

    private ChannelParseState mChannelState = null;

    private enum FilterType {
        FILTER_TYPE_PAT,
        FILTER_TYPE_PMT,
        FILTER_TYPE_SDT,
    }

    public enum StreamType {
        STREAM_TYPE_VIDEO,
        STREAM_TYPE_AUDIO,
        STREAM_TYPE_OTHER,
    }

    private SiParser() {
        mExecutor = new TunerExecutor();
    }

    public static SiParser getInstance() {
        if (mInstance == null)
            mInstance = new SiParser();
        return mInstance;
    }

    public void executeDtvChannelParser(final Tuner tuner, final int freq, final int signalType,
                                        @NonNull final Bundle scanParam,
                                        final SiParserEvent siParserEvent, final int delay) {
        ThreadManager.getInstance().runOnScanThreadDelayed(new Runnable() {
            @Override
            public void run() {
                parseDtvChannels(tuner, freq, signalType, scanParam, siParserEvent);
            }
        }, delay);
    }

    private void parseDtvChannels(Tuner tuner, int freqMhz, int signalType,
                                  @NonNull Bundle scanParam, SiParserEvent siParserEvent) {
        synchronized (mChannelParserLock) {
            if (mChannelParsing == STATE_NONE) {
                if (siParserEvent != null) {
                    mParserEvent = siParserEvent;
                }
                mChannelState = new ChannelParseState();
                mChannelState.mFreqMhz = freqMhz;
                startFilter(tuner, FilterType.FILTER_TYPE_PAT, 0);
                mChannelParsing = STATE_PROGRESS;
            } else if (mChannelParsing == STATE_PROGRESS) {
                if (mChannelState == null) {
                    Log.w(TAG, "Should not to here, need finish psi build.");
                    mChannelParsing = STATE_DONE;
                } else {
                    long currentTime = SystemClock.uptimeMillis();
                    if (currentTime - mChannelState.parseStartTime >= 10000) {
                        Log.w(TAG, "Timeout for building channels.");
                        mChannelParsing = STATE_DONE;
                    }
                    if (mChannelState.mPats.isEmpty()) {
                        currentTime = SystemClock.uptimeMillis();
                        if (currentTime - mChannelState.parseStartTime >= 5000) {
                            Log.w(TAG, "More than 5 seconds to find pat info, abort");
                            mChannelParsing = STATE_DONE;
                        }
                    } else {
                        int checkParsedCount = 0;
                        for (PatProgram pat : mChannelState.mPats) {
                            if (pat.pmtParseState == STATE_NONE) {
                                startFilter(tuner, FilterType.FILTER_TYPE_PMT, pat.pmtPid);
                                pat.pmtParseState = STATE_PROGRESS;
                            }
                            if (pat.sdtParseState == STATE_NONE) {
                                if (signalType == ScanManager.SIGNAL_TYPE_ATSC) {
                                    pat.sdtParseState = STATE_DONE;
                                } else {
                                    startFilter(tuner, FilterType.FILTER_TYPE_SDT, 0x11);
                                    pat.sdtParseState = STATE_PROGRESS;
                                }
                            }
                            if (pat.pmtParseState == STATE_DONE
                                    && pat.sdtParseState == STATE_DONE) {
                                checkParsedCount += 1;
                            }
                        }
                        if (checkParsedCount == mChannelState.mPats.size()) {
                            mChannelParsing = STATE_DONE;
                        }
                    }
                }
            } else if (mChannelParsing == STATE_DONE) {
                mChannelParsing = STATE_NONE;
                tryReleaseScanFilters();
                generateScannedChannels(scanParam);
                ChannelManager.getInstance().dump();
                if (mParserEvent != null) {
                    mParserEvent.onParseEnd();
                    mParserEvent = null;
                }
                return;
            }
        }
        executeDtvChannelParser(tuner, freqMhz, signalType, scanParam, siParserEvent, 100);
    }

    private void generateScannedChannels(@NonNull Bundle scanParam) {
        //make sure running in lock of mChannelParserLock
        int signalType = ScanManager.getInstance().getCurrentSignalType();
        if (mChannelState != null) {
            if (mChannelState.mPats.size() > 0) {
                for (PatProgram pat : mChannelState.mPats) {
                    int programId = pat.programId;
                    Channel.Builder builder =
                            new Channel.Builder(programId, mChannelState.mFreqMhz,
                                    signalType, scanParam);
                    for (PmtStream pmt : mChannelState.mPmtStreams) {
                        if (pmt.programId == programId) {
                            builder.setPcrId(pmt.pcrPid);
                            if (pmt.type == StreamType.STREAM_TYPE_VIDEO) {
                                builder.addVideoTrack(pmt.esPid, pmt.dvbStreamType);
                            } else if (pmt.type == StreamType.STREAM_TYPE_AUDIO) {
                                builder.addAudioTrack(pmt.esPid, pmt.dvbStreamType);
                            }
                        }
                    }
                    if (mChannelState.mSdtData.size() > 0) {
                        for (SdtData sdt : mChannelState.mSdtData) {
                            if (sdt.programId == programId) {
                                if (TextUtils.isEmpty(sdt.name)) {
                                    builder.setName("CH " + programId);
                                } else {
                                    builder.setName(sdt.name);
                                }
                                break;
                            }
                        }
                    } else {
                        builder.setName("CH " + programId);
                    }
                    Channel channel = builder.build();
                    if (channel.videos.size() > 0) {
                        //only add channel that has video streams
                        ChannelManager.getInstance().addChannel(builder.build());
                    }
                }
            }
            mChannelState.mPats.clear();
            mChannelState.mPmtStreams.clear();
            mChannelState.mSdtData.clear();
            mChannelState = null;
        }
    }

    private void startFilter(Tuner tuner, FilterType type, int pid) {
        Filter tmpFilter = null;
        byte tableId = 0;

        if (tuner == null)
            return;

        switch (type) {
            case FILTER_TYPE_PAT: {
                if (mPatFilter == null) {
                    mPatFilter = tuner.openFilter(Filter.TYPE_TS,
                            Filter.SUBTYPE_SECTION,
                            SECTION_FILTER_BUFFER_SIZE,
                            mExecutor, mfilterCallback);
                    tmpFilter = mPatFilter;
                    tableId = 0x0;
                    pid = 0;
                }
            }
            break;
            case FILTER_TYPE_PMT: {
                Filter pmtFilter = tuner.openFilter(Filter.TYPE_TS,
                        Filter.SUBTYPE_SECTION,
                        SECTION_FILTER_BUFFER_SIZE,
                        mExecutor, mfilterCallback);
                tmpFilter = pmtFilter;
                mPmtFilters.add(pmtFilter);
                tableId = 0x2;
            }
            break;
            case FILTER_TYPE_SDT: {
                if (mSdtFilter == null) {
                    mSdtFilter = tuner.openFilter(Filter.TYPE_TS,
                            Filter.SUBTYPE_SECTION,
                            SECTION_FILTER_BUFFER_SIZE,
                            mExecutor, mfilterCallback);
                    tmpFilter = mSdtFilter;
                    tableId = 0x42;
                    pid = 0x11;
                }
            }
            break;
        }
        if (tmpFilter != null) {
            byte mask = (byte) 255;
            SectionSettingsWithSectionBits settings =
                    SectionSettingsWithSectionBits
                            .builder(Filter.TYPE_TS)
                            .setCrcEnabled(false)
                            .setRepeat(false)
                            .setRaw(false)
                            .setFilter(new byte[]{tableId, 0, 0})
                            .setMask(new byte[]{mask, 0, 0, 0})
                            .setMode(new byte[]{0, 0, 0})
                            .build();
            FilterConfiguration config = TsFilterConfiguration
                    .builder()
                    .setTpid(pid)
                    .setSettings(settings)
                    .build();
            tmpFilter.configure(config);
            tmpFilter.start();
            Log.d(TAG, "Start filter(t:" + tableId + ",p:" + pid + ")");
        }
    }

    private void stopFiltersByType(FilterType type, boolean release) {
        switch (type) {
            case FILTER_TYPE_PAT:
                if (mPatFilter != null) {
                    mPatFilter.stop();
                    if (release) {
                        mPatFilter.close();
                        mPatFilter = null;
                    }
                }
                break;
            case FILTER_TYPE_PMT: {
                for (Filter f: mPmtFilters) {
                    if (f != null) {
                        f.stop();
                        if (release) {
                            f.close();
                        }
                    }
                }
                if (release) {
                    mPmtFilters.clear();
                }
            }
            break;
            case FILTER_TYPE_SDT: {
                if (mSdtFilter != null) {
                    mSdtFilter.stop();
                    if (release) {
                        mSdtFilter.close();
                        mSdtFilter = null;
                    }
                }
            }
            break;
        }
    }

    private void stopFilerById(FilterType type, int filterId) {
        switch (type) {
            case FILTER_TYPE_PAT:
                if (mPatFilter != null) {
                    mPatFilter.stop();
                }
                break;
            case FILTER_TYPE_PMT: {
                for (Filter f: mPmtFilters) {
                    if (f.getId() == filterId) {
                        f.stop();
                    }
                }
            }
            break;
            case FILTER_TYPE_SDT: {
                if (mSdtFilter != null) {
                    mSdtFilter.stop();
                }
            }
            break;
        }
    }

    private void tryReleaseScanFilters() {
        stopFiltersByType(FilterType.FILTER_TYPE_PAT, true);
        stopFiltersByType(FilterType.FILTER_TYPE_PMT, true);
        stopFiltersByType(FilterType.FILTER_TYPE_SDT, true);
    }

    private void parseSectionData(int filterId, byte[] data) {
        if (data.length == 0) {
            return;
        }
        synchronized (mChannelParserLock) {
            if (mChannelState == null) {
                mChannelParsing = STATE_DONE;
                return;
            }
        }
        int tableId = data[0];
        Log.d(TAG, "onSection data (fid:" + filterId + ", tableId:"
                + tableId + ", size:" + data.length + ")");
        try {
            if (tableId == 0x0) {
                int sectionLength = ((data[1] & 0x0f) << 8) | (data[2] & 0xff);
                int tsId = ((data[3] & 0xff) << 8) | (data[4] & 0xff);
                int channelsNumber = (sectionLength - 9)/4;
                for (int i = 0; i < channelsNumber; i ++) {
                    int proIndex = 8 + i*4;
                    PatProgram p = new PatProgram();
                    p.programId = ((data[proIndex] & 0xff) << 8) | (data[proIndex + 1] & 0xff);
                    p.pmtPid = ((data[proIndex + 2]  & 0x1f) << 8) | (data[proIndex + 3] & 0xff);
                    p.sdtParseState = STATE_NONE;
                    p.pmtParseState = STATE_NONE;
                    if (p.programId != 0) {
                        synchronized (mChannelParserLock) {
                            mChannelState.mPats.add(p);
                            Log.i(TAG, "Add pat program: " + p.toString());
                        }
                    }
                }
                synchronized (mChannelParserLock) {
                    mChannelState.tsId = tsId;
                    mChannelState.parseStartTime = SystemClock.uptimeMillis();
                    stopFiltersByType(FilterType.FILTER_TYPE_PAT, false);
                }
            } else if (tableId == 0x02) {
                int programId = ((data[3] & 0xff) << 8) | (data[4] & 0xff);
                PatProgram pat;
                synchronized (mChannelParserLock) {
                    pat = mChannelState.findPatProgram(programId);
                }
                if (pat == null) {
                    Log.i(TAG, "Skip pmt for program(" +  programId + ") not in pat");
                    return;
                }
                int pcrPid = ((data[8] & 0x1f) << 8) | (data[9] & 0xff);
                int streamStart = 12 + (((data[10] & 0x0f) << 8) | (data[11] & 0xff));
                int end = 2 + (((data[1] & 0x0f) << 8) | (data[2] & 0xff)) - 4/*crc size*/;
                for (int i = streamStart; i <= end; i ++) {
                    PmtStream p = new PmtStream();
                    p.programId = programId;
                    p.pcrPid = pcrPid;
                    p.dvbStreamType = data[i] & 0xff;
                    p.type = filterStreamType(p.dvbStreamType);
                    p.esPid = ((data[i + 1] & 0x1f) << 8) | (data[i+2] & 0xff);
                    i = i + 4 + (((data[i+ 3] & 0x0f) << 8) | (data[i+4] & 0xff));
                    synchronized (mChannelParserLock) {
                        pat.pmtParseState = STATE_DONE;
                        if (p.type != StreamType.STREAM_TYPE_OTHER) {
                            mChannelState.mPmtStreams.add(p);
                            Log.i(TAG, "Add pmt stream: " + p.toString());
                        }
                    }
                }
                synchronized (mChannelParserLock) {
                    mChannelState.parseStartTime = SystemClock.uptimeMillis();
                    stopFilerById(FilterType.FILTER_TYPE_PMT, filterId);
                }
            } else if (tableId == 0x42) {
                int tsId = ((data[3] & 0xff) << 8) + (data[4] & 0xff);
                synchronized (mChannelParserLock) {
                    if (tsId != mChannelState.tsId) {
                        Log.i(TAG, "Skip sdt for tsId(" +  tsId + ") not match with pat");
                        return;
                    }
                }
                int streamStart = 11;
                int end = 2 + (((data[1] & 0x0f) << 8) | (data[2] & 0xff)) - 4/*crc size*/;
                for (int i = streamStart; i <= end; i ++) {
                    int programId = ((data[i] & 0xff) << 8) | (data[i+1] & 0xff);
                    int descriptorLength = ((data[i+3] & 0x0f) << 8) | (data[i+4] & 0xff);
                    PatProgram pat;
                    synchronized (mChannelParserLock) {
                        pat = mChannelState.findPatProgram(programId);
                    }
                    if (pat == null) {
                        Log.i(TAG, "Skip sdt service for program(" +  programId + ") not in pat");
                        i = i + 4 + descriptorLength;
                        continue;
                    }
                    if (descriptorLength > 4) {
                        int providerLength = data[i + 8];
                        if (descriptorLength > (4 + providerLength)) {
                            int serviceNameLength = data[i + 9 + providerLength];
                            SdtData s = new SdtData();
                            s.programId = programId;
                            StringBuilder builder = new StringBuilder();
                            if (serviceNameLength == 0) {
                                s.name = "No name";
                            } else {
                                for (int x = 0; x < serviceNameLength; x ++) {
                                    builder.append((char)(data[i + 10 + providerLength + x]));
                                }
                                s.name = builder.toString();
                            }
                            synchronized (mChannelParserLock) {
                                pat.sdtParseState = STATE_DONE;
                                mChannelState.mSdtData.add(s);
                                Log.i(TAG, "Add sdt data: " + s.toString());
                            }
                        }
                    }
                    i = i + 4 + descriptorLength;
                }
                synchronized (mChannelParserLock) {
                    mChannelState.parseStartTime = SystemClock.uptimeMillis();
                    stopFilerById(FilterType.FILTER_TYPE_SDT, filterId);
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "error in si parser.");
        }
    }

    private FilterCallback mfilterCallback = new FilterCallback() {
        @Override
        public void onFilterEvent(Filter filter, FilterEvent[] events) {
            for (FilterEvent event: events) {
                if (event instanceof SectionEvent) {
                    SectionEvent sectionEvent = (SectionEvent) event;
                    byte[] data = new byte[sectionEvent.getDataLength()];
                    if (filter != null) {
                        filter.read(data, 0, sectionEvent.getDataLength());
                        parseSectionData(filter.getId(), data);
                    }
                }
            }
        }

        @Override
        public void onFilterStatusChanged(Filter filter, int status) {
        }
    };

    private static class PatProgram {
        PatProgram() {
            programId = 0;
            pmtPid = 0;
            pmtParseState = STATE_NONE;
            sdtParseState = STATE_NONE;
        }
        @Override
        @NonNull
        public String toString() {
            return "(program id:" + programId+ ", pid:" + pmtPid + ")";
        }
        int programId;
        int pmtPid;
        int pmtParseState;
        int sdtParseState;
    }

    private static class PmtStream {
        PmtStream() {
            programId = 0;
            esPid = 0;
            pcrPid = 0;
            type = StreamType.STREAM_TYPE_OTHER;
            dvbStreamType = 0;
        }
        @Override
        @NonNull
        public String toString() {
            return "(program id:" + programId+ ", pid:" + esPid
                    + ", pcrPid:" + pcrPid + ",dvbStreamType:" + dvbStreamType
                    + ", SiType:" + type + ")";
        }

        int programId;
        int esPid;
        int pcrPid;
        StreamType type;
        int dvbStreamType;
    }

    private static class SdtData {
        SdtData() {
            programId = 0;
            name = null;
        }
        @Override
        @NonNull
        public String toString() {
            return "(program id:" + programId+ ", name:" + name + ")";
        }
        int programId;
        String name;
    }

    private static class ChannelParseState {
        ChannelParseState() {
            parseStartTime = SystemClock.uptimeMillis();
            mFreqMhz = 0;
            tsId = 0;
            mPats = new ArrayList<>();
            mPmtStreams = new ArrayList<>();
            mSdtData = new ArrayList<>();
        }
        PatProgram findPatProgram(int progId) {
            for (PatProgram p:mPats) {
                if (p.programId == progId)
                    return p;
            }
            return null;
        }
        long parseStartTime;
        int mFreqMhz;
        int tsId;
        List<PatProgram> mPats;
        List<PmtStream> mPmtStreams;
        List<SdtData> mSdtData;
    }

    private StreamType filterStreamType(int dvbStreamType) {
        for (int videoType : videoStreamTypes) {
            if (videoType == dvbStreamType) {
                return StreamType.STREAM_TYPE_VIDEO;
            }
        }
        for (int audioType : audioStreamTypes) {
            if (audioType == dvbStreamType) {
                return StreamType.STREAM_TYPE_AUDIO;
            }
        }
        return StreamType.STREAM_TYPE_OTHER;
    }

    public interface SiParserEvent {
        void onParseEnd();
    }
}
