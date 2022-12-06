package com.droidlogic.tuner.scan;

import android.media.tv.tuner.frontend.DtmbFrontendSettings;
import android.media.tv.tuner.frontend.FrontendSettings;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.utils.Constants;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class DtmbScanManager extends ScanManagerSession {
    public static final String KEY_MODULATION = "modulation";
    public static final String KEY_BANDWIDTH = "bandwidth";
    private List<Map<String, String>> mModulationList = new ArrayList<>();
    private List<Map<String, String>> mBandwidthList = new ArrayList<>();

    private String[] modulation_array = {
            "Auto", "16QAM", "32QAM", "64QAM"
    };

    private String[] bandwidth_array = {
            "Auto", "8MHZ", "6MHZ"
    };

    DtmbScanManager() {
        for (String b : modulation_array) {
            HashMap<String, String> m = new HashMap<>();
            m.put("name", b);
            mModulationList.add(m);
        }
        for (String t : bandwidth_array) {
            HashMap<String, String> b = new HashMap<>();
            b.put("name", t);
            mBandwidthList.add(b);
        }
    }

    private int getTunerBandwidth(int bandwidthIndex) {
        if (bandwidthIndex < bandwidth_array.length)
            return (1 << bandwidthIndex);
        return DtmbFrontendSettings.BANDWIDTH_AUTO;
    }

    private int getTunerModulation(int modulationIndex) {
        if (modulationIndex < modulation_array.length && modulationIndex > 0) {
            return (1 << (modulationIndex + 2));
        }
        return DtmbFrontendSettings.MODULATION_CONSTELLATION_AUTO;
    }

    public List<Map<String, String>> getModulationSettings() {
        return mModulationList;
    }

    public List<Map<String, String>> getBandwidthSettings() {
        return mBandwidthList;
    }

    @Override
    public FrontendSettings createScanSettings(int freqMhz, @NonNull Bundle bundle) {
        if (Build.VERSION.SDK_INT >= 33) {
            Log.d(Constants.TAG, "Create dtmb settings");
            int modulationIndex = bundle.getInt(KEY_MODULATION, 0);
            int bandWidthIndex = bundle.getInt(KEY_BANDWIDTH, 0);
            DtmbFrontendSettings.Builder builder = DtmbFrontendSettings.builder()
                    .setFrequency(freqMhz * 1000000)
                    .setBandwidth(getTunerBandwidth(bandWidthIndex))
                    .setModulation(getTunerModulation(modulationIndex));
            return builder.build();
        }
        return null;
    }
}
