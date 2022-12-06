package com.droidlogic.tuner.dvr;

import android.content.Context;
import android.media.tv.TvInputService;
import android.media.tv.tuner.Tuner;
import android.media.tv.tuner.dvr.DvrSettings;
import android.media.tv.tuner.filter.Filter;
import android.media.tv.tuner.filter.FilterCallback;
import android.media.tv.tuner.filter.FilterConfiguration;
import android.media.tv.tuner.filter.FilterEvent;
import android.media.tv.tuner.filter.RecordSettings;
import android.media.tv.tuner.filter.Settings;
import android.media.tv.tuner.filter.TsFilterConfiguration;
import android.media.tv.tuner.filter.TsRecordEvent;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.channel.Channel;
import com.droidlogic.tuner.psi.SiParser;
import com.droidlogic.tuner.scan.TunerControl;
import com.droidlogic.tuner.utils.Constants;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;

class Recorder {
    private static final String TAG = Constants.TAG;
    private Channel mRecordingChannel;
    private ParcelFileDescriptor fd;
    private android.media.tv.tuner.dvr.DvrRecorder mTunerRecorder;
    private List<Filter> mAttachedFilters;
    private RecorderDescriptor mContent;

    Recorder(@NonNull Channel channel) {
        mRecordingChannel = channel;
        mAttachedFilters = new ArrayList<>();
    }

    RecorderDescriptor start(@NonNull Context context,
                             @NonNull Executor executor) throws IOException {
        RecorderDescriptor recorderDescriptor =
                prepareRecorder(context, executor);
        if (recorderDescriptor != null && mTunerRecorder != null) {
            mTunerRecorder.start();
            mTunerRecorder.flush();
            return recorderDescriptor;
        }
        return null;
    }

    void stop() throws IOException {
        if (mTunerRecorder != null) {
            mTunerRecorder.stop();
            for (Filter f : mAttachedFilters) {
                mTunerRecorder.detachFilter(f);
                f.stop();
                f.close();
            }
            mAttachedFilters.clear();
            if (mContent != null) {
                mContent.finish();
            }
            mTunerRecorder.flush();
            mTunerRecorder.close();
            if (fd != null) {
                fd.close();
            }
        }
    }

    private RecorderDescriptor prepareRecorder(@NonNull Context context,
                                    @NonNull Executor executor) throws IOException {
        if (mRecordingChannel.videos.size() == 0 && mRecordingChannel.audios.size() == 0) {
            return null;
        }
        String path = context.getCacheDir() +
                "/" + mRecordingChannel.programId + ".dvr_test";
        File file = new File(path);
        fd = ParcelFileDescriptor.open(file,
                ParcelFileDescriptor.MODE_WRITE_ONLY
                        |ParcelFileDescriptor.MODE_CREATE
                        |ParcelFileDescriptor.MODE_TRUNCATE);
        Tuner tuner = TunerControl.getInstance().acquireTuner(context,
                null, TvInputService.PRIORITY_HINT_USE_CASE_TYPE_SCAN);
        if (tuner != null && fd != null) {
            mTunerRecorder = tuner.openDvrRecorder(100 * 1024 * 1024,
                    executor,
                    new DvrRecorder
                            .OnRecordStatusChangedListenerImpl());
            if (mTunerRecorder != null) {
                DvrSettings settings = DvrSettings.builder()
                        .setStatusMask(Filter.STATUS_DATA_READY)
                        .setLowThreshold(1024 * 1024 * 20)
                        .setHighThreshold(1024 * 1024 * 80)
                        .setPacketSize(188L)
                        .setDataFormat(DvrSettings.DATA_FORMAT_TS)
                        .build();
                mTunerRecorder.configure(settings);
                mTunerRecorder.setFileDescriptor(fd);
                mContent = new RecorderDescriptor(mRecordingChannel, path);
                //attach video filter
                if (mRecordingChannel.videos.size() > 0) {
                    Channel.Video video = mRecordingChannel.videos.get(0);
                    Filter videoFilter = tryCreateMediaFilter(
                            SiParser.StreamType.STREAM_TYPE_VIDEO,
                            video.pid, tuner, executor);
                    if (videoFilter != null) {
                        videoFilter.start();
                        videoFilter.flush();
                        mTunerRecorder.attachFilter(videoFilter);
                        mAttachedFilters.add(videoFilter);
                    }
                }
                //attach audio filter
                if (mRecordingChannel.audios.size() > 0) {
                    Channel.Audio audio = mRecordingChannel.audios.get(0);
                    Filter audioFilter = tryCreateMediaFilter(
                            SiParser.StreamType.STREAM_TYPE_AUDIO,
                            audio.pid, tuner, executor);
                    if (audioFilter != null) {
                        audioFilter.start();
                        audioFilter.flush();
                        mTunerRecorder.attachFilter(audioFilter);
                        mAttachedFilters.add(audioFilter);
                    }
                }
                Log.i(TAG, "Create dvr recorder success");
                return mContent;
            }
        }
        Log.i(TAG, "start recorder failed.");
        return null;
    }

    private Filter tryCreateMediaFilter(SiParser.StreamType type, int pid,
                                        @NonNull Tuner tuner, Executor executor) {
        long buffersize = 1024 * 1024;

        if (type == SiParser.StreamType.STREAM_TYPE_VIDEO) {
            buffersize = buffersize * 200;
        } else if (type == SiParser.StreamType.STREAM_TYPE_AUDIO) {
            buffersize = buffersize * 10;
        }
        Filter filter = tuner.openFilter(Filter.TYPE_TS,
                Filter.SUBTYPE_RECORD, buffersize,
                executor, new RecordFilterCallback());
        if (filter != null) {
            Settings videoSettings = RecordSettings
                    .builder(Filter.TYPE_TS)
                    .setTsIndexMask(RecordSettings.TS_INDEX_FIRST_PACKET)
                    .build();
            FilterConfiguration config = TsFilterConfiguration
                    .builder()
                    .setTpid(pid)
                    .setSettings(videoSettings)
                    .build();
            filter.configure(config);
        }
        return filter;
    }

    int getRecordingChannelId() {
        return mRecordingChannel.programId;
    }

    public class RecordFilterCallback implements FilterCallback {
        @Override
        public void onFilterEvent(Filter filter, FilterEvent[] events) {
            for (FilterEvent event: events) {
                if (event instanceof TsRecordEvent) {
                    TsRecordEvent tsRecEvent = (TsRecordEvent) event;
                    //Log.d(TAG, "Receive tsRecord data, size=" + tsRecEvent.getDataLength());
                    long dataLength = tsRecEvent.getDataLength();
                    if (mTunerRecorder != null) {
                        mTunerRecorder.write(dataLength);
                    }
                }
            }
        }

        @Override
        public void onFilterStatusChanged(Filter filter, int status) {

        }
    }
}
