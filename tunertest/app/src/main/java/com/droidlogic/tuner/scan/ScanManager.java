package com.droidlogic.tuner.scan;

import android.content.Context;
import android.media.tv.TvInputService;
import android.media.tv.tuner.Tuner;
import android.media.tv.tuner.frontend.Atsc3PlpInfo;
import android.media.tv.tuner.frontend.FrontendSettings;
import android.media.tv.tuner.frontend.FrontendStatus;
import android.media.tv.tuner.frontend.ScanCallback;
import android.os.Bundle;
import android.util.Log;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.channel.ChannelManager;
import com.droidlogic.tuner.psi.SiParser;
import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.ThreadManager;

public class ScanManager {
    private final String TAG = Constans.TAG;
    private static ScanManager mInstance = null;
    private ThreadManager.TunerExecutor mExecutor;
    private OnScanEvent mScanEvt = null;
    private DvbtScanManager mDvbtScanManager;
    private DvbcScanManager mDvbcScanManager;
    private DvbsScanManager mDvbsScanManager;
    private AtscScanManager mAtscScanManager;
    private IsdbtScanManager mIsdbtScanManager;

    public final static int SIGNAL_TYPE_ATV = 0;
    public final static int SIGNAL_TYPE_ATSC = 1;
    public final static int SIGNAL_TYPE_DTMB = 2;
    public final static int SIGNAL_TYPE_DVBT = 3;
    public final static int SIGNAL_TYPE_DVBC = 4;
    public final static int SIGNAL_TYPE_DVBS = 5;
    public final static int SIGNAL_TYPE_ISDBT = 6;
    public final static int SIGNAL_TYPE_NONE = 255;

    public final static String KEY_FREQUENCY = "frequency";

    private int mSignalType = SIGNAL_TYPE_NONE;

    private ScanManager() {
        mDvbtScanManager = new DvbtScanManager();
        mDvbcScanManager = new DvbcScanManager();
        mDvbsScanManager = new DvbsScanManager();
        mAtscScanManager = new AtscScanManager();
        mIsdbtScanManager = new IsdbtScanManager();
        mExecutor = new ThreadManager.TunerExecutor();
    }

    public static ScanManager getInstance() {
        if (mInstance == null)
            mInstance = new ScanManager();
        return mInstance;
    }

    public void registerScanEvent(OnScanEvent scanEvent) {
        mScanEvt = scanEvent;
    }

    public void switchSignalType(int type) {
        Log.d(TAG, "signal type switch to " + type);
        if (type >= SIGNAL_TYPE_ATV && type <= SIGNAL_TYPE_ISDBT)
            mSignalType = type;
        else
            mSignalType = SIGNAL_TYPE_NONE;
    }

    public int getCurrentSignalType() {
        return mSignalType;
    }

    public ScanManagerSession getSession(int signalType) {
        switch (signalType) {
            case SIGNAL_TYPE_DVBT:
                return mDvbtScanManager;
            case SIGNAL_TYPE_DVBC:
                return mDvbcScanManager;
            case SIGNAL_TYPE_ATSC:
                return mAtscScanManager;
            case SIGNAL_TYPE_ISDBT:
                return mIsdbtScanManager;
            case SIGNAL_TYPE_DVBS:
                return mDvbsScanManager;
        }
        return null;
    }

    private FrontendSettings getFrontendSettings(int freqMhz, @NonNull Bundle scanParam) {
        ScanManagerSession session = getSession(mSignalType);
        if (session != null) {
            return session.createScanSettings(freqMhz, scanParam);
        }
        return null;
    }

    public void startScan(@NonNull Context context, int freqMhz, @NonNull Bundle scanParam) {
        int tuneRest = 0;
        if (mScanEvt != null) {
            mScanEvt.onScanStart();
        }
        Tuner tuner = TunerControl.getInstance().acquireTuner(context,
                null, TvInputService.PRIORITY_HINT_USE_CASE_TYPE_SCAN);
        if (tuner == null) {
            Log.w(TAG, "could not got tuner instance.");
            if (mScanEvt != null) {
                mScanEvt.onScanEnd();
            }
            return;
        }
        ChannelManager.getInstance().clearChannels();
        tuner.clearOnTuneEventListener();
        tuner.cancelScanning();
        getSession(mSignalType).onEarlyScan(tuner, scanParam);
        FrontendSettings setting = getFrontendSettings(freqMhz, scanParam);
        if (setting != null) {
            tuneRest = tuner.scan(
                    setting,
                    Tuner.SCAN_TYPE_AUTO, mExecutor, new OnScanListenerImpl(tuner,
                            freqMhz, scanParam));
            getSession(mSignalType).onLaterScan();
        } else {
            tuneRest = 1;
        }
        if (tuneRest != 0 && mScanEvt != null) {
            mScanEvt.onScanEnd();
        }
    }

    public interface OnScanEvent {
        void onScanStart();
        void onScanLock(int strength);
        void onScanUnLock(int strength);
        void onSymbolRatesReported(int[] rate);
        void onPlpIdsReported(int[] plpIds);
        void onScanEnd();
    }

    private class OnScanListenerImpl implements ScanCallback {
        private Tuner mTuner;
        private int mFreq;
        private Bundle mScanParam;
        OnScanListenerImpl(Tuner tuner, int freqMhz, @NonNull Bundle scanParam) {
            mTuner = tuner;
            mFreq = freqMhz;
            mScanParam = scanParam;
        }

        @Override
        public void onLocked() {
            if (mScanEvt != null) {
                FrontendStatus status =
                        mTuner.getFrontendStatus(
                                new int[] {FrontendStatus.FRONTEND_STATUS_TYPE_SIGNAL_STRENGTH,
                                    FrontendStatus.FRONTEND_STATUS_TYPE_SYMBOL_RATE});
                mScanEvt.onScanLock(status.getSignalStrength());
            }
            SiParser.getInstance().executeDtvChannelParser(mTuner, mFreq, mSignalType,
                    mScanParam, new SiParser.SiParserEvent() {
                @Override
                public void onParseEnd() {
                    Log.d(TAG, "onSiParse end");
                    if (mScanEvt != null) {
                        mTuner.cancelScanning();
                        getSession(mSignalType).onTuneEnd();
                        mScanEvt.onScanEnd();
                    }
                }
            }, 0);
        }

        @Override
        public void onScanStopped() {
        }

        @Override
        public void onProgress(int percent) {
        }

        @Override
        public void onFrequenciesReported(int[] frequency) {
            Log.d(TAG, "onFrequenciesReported");
            if (mTuner != null) {
                FrontendStatus status
                        = mTuner.getFrontendStatus(
                                new int[]{FrontendStatus.FRONTEND_STATUS_TYPE_RF_LOCK,
                                        FrontendStatus.FRONTEND_STATUS_TYPE_SIGNAL_STRENGTH});
                if (!status.isRfLocked()) {
                    Log.d(TAG, "unlock, should stop");
                    if (mScanEvt != null) {
                        mTuner.cancelScanning();
                        mScanEvt.onScanUnLock(status.getSignalStrength());
                        mScanEvt.onScanEnd();
                    }
                } else {
                    Log.d(TAG, "locked, waiting to build psi.");
                }
            }
        }

        @Override
        public void onSymbolRatesReported(int[] rate) {
            if (mScanEvt != null) {
                mScanEvt.onSymbolRatesReported(rate);
            }
        }

        @Override
        public void onPlpIdsReported(int[] plpIds) {
            if (mScanEvt != null) {
                mScanEvt.onPlpIdsReported(plpIds);
            }
        }

        @Override
        public void onGroupIdsReported(int[] groupIds) {
        }

        @Override
        public void onInputStreamIdsReported(int[] inputStreamIds) {
        }

        @Override
        public void onDvbsStandardReported(int dvbsStandard) {
        }

        @Override
        public void onDvbtStandardReported(int dvbtStandard) {
        }

        @Override
        public void onAnalogSifStandardReported(int sif) {
        }

        @Override
        public void onAtsc3PlpInfosReported(Atsc3PlpInfo[] atsc3PlpInfos) {
        }

        @Override
        public void onHierarchyReported(int hierarchy) {
        }

        @Override
        public void onSignalTypeReported(int signalType) {
        }
    }
}
