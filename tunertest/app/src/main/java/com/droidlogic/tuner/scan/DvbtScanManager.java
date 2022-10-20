package com.droidlogic.tuner.scan;

import android.media.tv.tuner.frontend.DvbtFrontendSettings;
import android.os.Bundle;
import android.util.Log;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.utils.Constans;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class DvbtScanManager extends ScanManagerSession {
    private static final String TAG = Constans.TAG;
    private List<Map<String, String>> mBwList = new ArrayList<>();
    private List<Map<String, String>> mTsList = new ArrayList<>();
    public static final String KEY_BAND_WIDTH = "bandWidth";
    public static final String KEY_TRANSMISSION_MODE = "transmissionMode";
    public static final String KEY_IS_T2_MODE = "isT2";

    private final String[] bw_array = {
            "Auto", "8HMZ", "7MHZ", "6MHZ", "5MHZ", "10MHZ"
    };

    private final String[] ts_array = {
            "Auto", "2K", "8K", "4K", "1K", "16K", "32K"
    };

    DvbtScanManager() {
        for (String b : bw_array) {
            HashMap<String, String> m = new HashMap<>();
            m.put("name", b);
            mBwList.add(m);
        }
        for (String t : ts_array) {
            HashMap<String, String> m = new HashMap<>();
            m.put("name", t);
            mTsList.add(m);
        }
    }

    public List<Map<String, String>> getBandWidthSettings() {
        return mBwList;
    }

    public List<Map<String, String>> getTransmissionModeSettings() {
        return mTsList;
    }

    private int getTunerBandwidth(int bandwidthIndex) {
        switch (bandwidthIndex) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
                return (1 << bandwidthIndex);
            case 5:
                return DvbtFrontendSettings.BANDWIDTH_10MHZ;
        }
        return DvbtFrontendSettings.BANDWIDTH_AUTO;
    }

    private int getTunerTransmissionMode(int transmissionIndex) {
        if (transmissionIndex >= 0 && transmissionIndex < ts_array.length)
            return (transmissionIndex << 1);
        return DvbtFrontendSettings.TRANSMISSION_MODE_AUTO;
    }

    @Override
    public DvbtFrontendSettings createScanSettings(int freqMhz, @NonNull Bundle scanParam) {
        Log.d(TAG, "dvbt scan " + scanParam.toString());
        boolean isT2 = scanParam.getBoolean(KEY_IS_T2_MODE);
        int bandWidth = scanParam.getInt(KEY_BAND_WIDTH, 0);
        int transmissionMode = scanParam.getInt(KEY_TRANSMISSION_MODE, 0);
        DvbtFrontendSettings.Builder builder = DvbtFrontendSettings
                .builder()
                .setFrequency(freqMhz  * 1000000)
                .setBandwidth(getTunerBandwidth(bandWidth))
                .setTransmissionMode(getTunerTransmissionMode(transmissionMode));
        if (isT2) {
            builder.setStandard(DvbtFrontendSettings.STANDARD_T2);
            builder.setPlpId(0);
        } else {
            builder.setStandard(DvbtFrontendSettings.STANDARD_T);
        }
        return builder.build();
    }
}
