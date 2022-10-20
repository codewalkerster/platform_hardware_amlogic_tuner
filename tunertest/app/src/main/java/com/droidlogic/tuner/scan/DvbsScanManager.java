package com.droidlogic.tuner.scan;

import android.media.tv.tuner.Lnb;
import android.media.tv.tuner.LnbCallback;
import android.media.tv.tuner.Tuner;
import android.media.tv.tuner.frontend.DvbsFrontendSettings;
import android.media.tv.tuner.frontend.FrontendSettings;
import android.os.Bundle;
import android.util.Log;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.ThreadManager;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class DvbsScanManager extends ScanManagerSession {
    private static final String TAG = Constans.TAG;
    public static final int DEFAULT_SINGLE_LNB = 5150;
    public static final int DEFAULT_DOUBLE_LNB_LOW = 9750;
    public static final int DEFAULT_DOUBLE_LNB_HIGH = 10600;
    public static final int DEFAULT_UNICABLE_BAND = 0;

    public static final int LNB_TYPE_SINGLE = 0;
    public static final int LNB_TYPE_UNIVERSE = 1;

    public static final int POLARITY_V = 0;
    public static final int POLARITY_H = 1;

    public static final String KEY_SATELLITE_STANDARD = "isS2";
    public static final String KEY_SATELLITE_FREQUENCY = "satellite_frequency";
    public static final String KEY_SATELLITE_POLARITY = "satellite_polarity";
    public static final String KEY_SATELLITE_SYMBOL = "satellite_symbol";
    public static final String KEY_LNB_TYPE = "lnb_type";
    public static final String KEY_LNB_LOF = "lnb_lof";
    public static final String KEY_LNB_UNICABLE = "unicable";
    public static final String KEY_LNB_HOF = "lnb_hof";
    public static final String KEY_UNICABLE_BAND = "unicable_band";
    public static final String KEY_UNICABLE_FREQUENCY = "unicable_freq";
    public static final String KEY_LNB_IF_FREQUENCY = "lnb_if_frequency";
    public static final String KEY_TUNE_IF_FREQUENCY = "tune_if_frequency";

    private TunerContext mTunerContext = null;
    private List<Map<String, String>> mLnbTypeList = new ArrayList<>();
    private static final int MAX_UB_SLOTS = 8;//only support unicable v1 for test
    private List<Map<String, String>> mUbSlotList = new ArrayList<>();
    private List<Map<String, String>> mPolarityList = new ArrayList<>();

    private final String[] lnb_type_array = {
            "5150", "9750/10600"
    };

    DvbsScanManager() {
        for (String q : lnb_type_array) {
            HashMap<String, String> m = new HashMap<>();
            m.put("name", q);
            mLnbTypeList.add(m);
        }
        for (int i=0;i<MAX_UB_SLOTS;i++) {
            HashMap<String, String> s = new HashMap<>();
            s.put("name", "" + i);
            mUbSlotList.add(s);
        }
        {
            HashMap<String, String> ph = new HashMap<>();
            ph.put("name", "V");
            mPolarityList.add(ph);
            HashMap<String, String> pv = new HashMap<>();
            pv.put("name", "H");
            mPolarityList.add(pv);
        }
    }

    public List<Map<String, String>> getLnbTypeList() {
        return mLnbTypeList;
    }

    public List<Map<String, String>> getUbSlotList() {
        return mUbSlotList;
    }

    public List<Map<String, String>> getPolarityList() {
        return mPolarityList;
    }

    @Override
    public FrontendSettings createScanSettings(int freqMhz, @NonNull Bundle bundle) {
        if (isSettingsValid(bundle)) {
            Log.d(TAG, "dvbs scan " + bundle.toString());
            DvbsFrontendSettings.Builder builder = DvbsFrontendSettings.builder();
            builder.setModulation(DvbsFrontendSettings.MODULATION_AUTO)
                    .setFrequency(bundle.getInt(KEY_TUNE_IF_FREQUENCY) * 1000000)
                    .setSymbolRate(bundle.getInt(KEY_SATELLITE_SYMBOL) * 1000);
            boolean isDvbS2 = bundle.getBoolean(KEY_SATELLITE_STANDARD, false);
            if (isDvbS2) {
                builder.setStandard(DvbsFrontendSettings.STANDARD_S2);
            } else {
                builder.setStandard(DvbsFrontendSettings.STANDARD_AUTO);
            }
            return builder.build();
        }
        return null;
    }

    @Override
    public void onEarlyScan(@NonNull Tuner tuner, @NonNull Bundle bundle) {
        if (mTunerContext == null) {
            mTunerContext = new TunerContext(tuner, bundle);
        }
        applyLnbSettings();
    }

    @Override
    public void onTuneEnd() {
        if (mTunerContext != null) {
            if (mTunerContext.lnb != null) {
                mTunerContext.lnb.setTone(Lnb.TONE_NONE);
                mTunerContext.lnb.setVoltage(Lnb.VOLTAGE_NONE);
                mTunerContext.lnb.close();
                mTunerContext.lnb = null;
            }
            mTunerContext = null;
        }
    }

    @Override
    public boolean isSettingsValid(@NonNull Bundle bundle) {
        int lnbType = bundle.getInt(KEY_LNB_TYPE, LNB_TYPE_SINGLE);
        int satelliteFreq = bundle.getInt(KEY_SATELLITE_FREQUENCY);
        int lnbLof = bundle.getInt(KEY_LNB_LOF);
        int lnbHof = bundle.getInt(KEY_LNB_HOF);
        int ubFreq = bundle.getInt(KEY_UNICABLE_FREQUENCY, 0);
        boolean unicable = bundle.getBoolean(KEY_LNB_UNICABLE, false);
        if (lnbType == LNB_TYPE_SINGLE) {
            //we only handle 5150 now
            if (lnbLof == 5150 && (satelliteFreq < 3400 || satelliteFreq > 4200)) {
                return  false;
            }
        } else if (lnbType == LNB_TYPE_UNIVERSE) {
            if ((lnbLof == 5150 && lnbHof == 5750) &&
                    (satelliteFreq < 3400 || satelliteFreq > 4200)){
                return false;
            } else if ((lnbLof == 9750 && lnbHof == 10600) &&
                    (satelliteFreq < 10700 || satelliteFreq > 12750)) {
                return false;
            }
        }
        if (unicable) {
            return ubFreq >= 950 && ubFreq <= 2150;
        }
        return true;
    }

    private void applyLnbSettings() {
        if (mTunerContext == null)
            return;
        int lnbType = mTunerContext.bundle.getInt(KEY_LNB_TYPE, LNB_TYPE_SINGLE);
        boolean unicable = mTunerContext.bundle.getBoolean(KEY_LNB_UNICABLE, false);
        if (lnbType == LNB_TYPE_SINGLE) {
            applySingleLnb();
        } else if (lnbType == LNB_TYPE_UNIVERSE) {
            applyUniverseLnb();
        }
        if (unicable) {
            applyUnicable();
        } else {
            int lnbIfMhz = mTunerContext.bundle.getInt(KEY_LNB_IF_FREQUENCY, 0);
            mTunerContext.bundle.putInt(KEY_TUNE_IF_FREQUENCY, lnbIfMhz);
        }
        applyLnbPower();
    }

    private void applySingleLnb() {
        int lof = mTunerContext.bundle.getInt(KEY_LNB_LOF);
        int sate_freq = mTunerContext.bundle.getInt(KEY_SATELLITE_FREQUENCY);
        mTunerContext.ifFreq = (lof > sate_freq) ? (lof - sate_freq) : (sate_freq -lof);
        mTunerContext.bundle.putInt(KEY_LNB_IF_FREQUENCY, mTunerContext.ifFreq);
    }

    private void applyUniverseLnb() {
        int lof = mTunerContext.bundle.getInt(KEY_LNB_LOF);
        int hof = mTunerContext.bundle.getInt(KEY_LNB_HOF);
        int polarity = mTunerContext.bundle.getInt(KEY_SATELLITE_POLARITY, POLARITY_V);
        int sate_freq = mTunerContext.bundle.getInt(KEY_SATELLITE_FREQUENCY);
        if (lof == 5150 && hof == 5750) {
            if (polarity == POLARITY_H) {
                mTunerContext.ifFreq = 5150 - sate_freq;
            } else {
                mTunerContext.ifFreq = 5750 - sate_freq;
            }
        } else if (lof == 9750 && hof == 10600) {
            if (sate_freq < 11700) {
                mTunerContext.ifFreq = sate_freq - 9750;
            } else {
                mTunerContext.highBand = true;
                mTunerContext.ifFreq = sate_freq - 10600;
            }
        }
        mTunerContext.bundle.putInt(KEY_LNB_IF_FREQUENCY, mTunerContext.ifFreq);
    }

    private byte createDiseqcCommitByte(boolean option,
                                             boolean position,
                                             boolean horizontal,
                                             boolean highBand) {
        return (byte)(0x0F & (
                (option ? 0x8 : 0x0) |
                        (position ? 0x4 : 0x0) |
                        (horizontal ? 0x2 : 0x0) |
                        (highBand ? 0x1 : 0x0)));
    }

    private void applyUnicable() {
        //only support unicable v1 now
        byte[] diseqcMessages = new byte[5];
        if (mTunerContext == null) {
            return;
        }

        if (mTunerContext.lnb == null) {
            openLnb();
        }

        int lnbIfMhz = mTunerContext.bundle.getInt(KEY_LNB_IF_FREQUENCY, 0);
        int unicableFreq = mTunerContext.bundle.getInt(KEY_UNICABLE_FREQUENCY);
        int slot = mTunerContext.bundle.getInt(KEY_UNICABLE_BAND, DEFAULT_UNICABLE_BAND);
        int polarity = mTunerContext.bundle.getInt(KEY_SATELLITE_POLARITY, POLARITY_V);
        long T = (long)((lnbIfMhz + unicableFreq)/4.0 - 350);
        diseqcMessages[0] = (byte)0xE0;//MASTER_NO_REPLY = 0xE0
        diseqcMessages[1] = (byte)0x10;//ANY_LNB_SWITCH_SMATV = 0x10
        diseqcMessages[2] = (byte)0x5c;//EN50494_ODU_CHANNEL_CHANGE_MDU = 0x5C
        byte data_3 = (byte)0x0;
        data_3 |= (byte)((slot << 5) & 0xff);
        byte commitByte = createDiseqcCommitByte(false, false,
                (polarity == POLARITY_H), mTunerContext.highBand);
        data_3 |= (byte)((commitByte << 2) & 0x1c);
        data_3 |= (byte)((T >> 8) & 0x3);
        diseqcMessages[3] = data_3;
        diseqcMessages[4] = (byte)(T & 0xff);

        mTunerContext.ifFreq = unicableFreq;
        mTunerContext.bundle.putInt(KEY_TUNE_IF_FREQUENCY, mTunerContext.ifFreq);
        mTunerContext.lnb.setVoltage(Lnb.VOLTAGE_18V);
        mTunerContext.lnb.setTone(Lnb.TONE_NONE);
        mTunerContext.lnb.sendDiseqcMessage(diseqcMessages);
    }

    private void applyLnbPower() {
        int polarity = mTunerContext.bundle.getInt(KEY_SATELLITE_POLARITY, POLARITY_V);
        if (mTunerContext.lnb == null) {
            openLnb();
        }
        if (polarity == POLARITY_H) {
            mTunerContext.lnb.setVoltage(Lnb.VOLTAGE_18V);
        } else {
            mTunerContext.lnb.setVoltage(Lnb.VOLTAGE_13V);
        }
        try {
            Thread.sleep(40);
        } catch (Exception ignored) {
        }
        if (mTunerContext.highBand) {
            mTunerContext.lnb.setTone(Lnb.TONE_CONTINUOUS);
        } else {
            mTunerContext.lnb.setTone(Lnb.TONE_NONE);
        }
    }

    private void openLnb() {
        if (mTunerContext.lnb == null) {
            mTunerContext.lnb = mTunerContext.tuner.openLnb(
                    new ThreadManager.TunerExecutor(), new LnbCallback() {
                        @Override
                        public void onEvent(int i) {
                        }

                        @Override
                        public void onDiseqcMessage(byte[] bytes) {
                        }
                    });
        }
    }

    static class TunerContext {
        Tuner tuner;
        Lnb lnb;
        Bundle bundle;
        int ifFreq;
        boolean highBand;
        TunerContext(@NonNull Tuner tuner, @NonNull Bundle bundle) {
            this.tuner = tuner;
            this.bundle = bundle;
            lnb = null;
            ifFreq = 0;
            highBand = false;
        }
    }
}
