package com.droidlogic.tuner.scan;

import android.media.tv.tuner.frontend.FrontendSettings;
import android.media.tv.tuner.frontend.IsdbtFrontendSettings;
import android.os.Bundle;

import androidx.annotation.NonNull;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class IsdbtScanManager extends ScanManagerSession {
    public static final String KEY_MODULATION = "modulation";
    public static final String KEY_BANDWIDTH = "bandwidth";
    private List<Map<String, String>> mModulationList = new ArrayList<>();
    private List<Map<String, String>> mBandwidthList = new ArrayList<>();

    private String[] modulation_array = {
        "Auto", "DQPSK", "QPSK", "16QAM", "64QAM"
    };

    private String[] bandwidth_array = {
        "Auto", "8MHZ", "7MHZ", "6MHZ"
    };

    IsdbtScanManager() {
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
        return IsdbtFrontendSettings.BANDWIDTH_AUTO;
    }

    private int getTunerModulation(int modulationIndex) {
        if (modulationIndex < modulation_array.length)
            return (1 << modulationIndex);
        return IsdbtFrontendSettings.MODULATION_AUTO;
    }

    public List<Map<String, String>> getModulationSettings() {
        return mModulationList;
    }

    public List<Map<String, String>> getBandwidthSettings() {
        return mBandwidthList;
    }

    @Override
    public FrontendSettings createScanSettings(int freqMhz, @NonNull Bundle bundle) {
        int modulationIdex = bundle.getInt(KEY_MODULATION, IsdbtFrontendSettings.MODULATION_AUTO);
        int bandWidthIndex = bundle.getInt(KEY_BANDWIDTH, IsdbtFrontendSettings.BANDWIDTH_AUTO);
        IsdbtFrontendSettings.Builder builder = IsdbtFrontendSettings.builder()
                .setFrequency(freqMhz * 1000000)
                .setModulation(getTunerModulation(modulationIdex))
                .setBandwidth(getTunerBandwidth(bandWidthIndex));
        return builder.build();
    }
}
