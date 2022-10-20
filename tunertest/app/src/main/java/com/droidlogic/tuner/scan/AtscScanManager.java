package com.droidlogic.tuner.scan;

import android.media.tv.tuner.frontend.AtscFrontendSettings;
import android.os.Bundle;

import androidx.annotation.NonNull;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class AtscScanManager extends ScanManagerSession{
    public static final String KEY_MODULATION = "modulation";
    private List<Map<String, String>> mModulationList = new ArrayList<>();

    public AtscScanManager () {
        String[] modulation_array = {
                "Auto", "8VSB", "16VSB"
        };
        for (String q : modulation_array) {
            HashMap<String, String> m = new HashMap<>();
            m.put("name", q);
            mModulationList.add(m);
        }
    }

    public List<Map<String, String>> getModulationSettings() {
        return mModulationList;
    }

    private int getTunerModulation(int modulationIndex) {
        if (modulationIndex == 1 || modulationIndex == 2) {
            return (1 << (modulationIndex + 1));
        }
        return AtscFrontendSettings.MODULATION_AUTO;
    }

    @Override
    public AtscFrontendSettings createScanSettings(int freqMhz, @NonNull Bundle bundle) {
        int modulationIndex = bundle.getInt(KEY_MODULATION, 0);
        AtscFrontendSettings.Builder builder = AtscFrontendSettings.builder()
                .setFrequency(freqMhz * 1000000)
                .setModulation(getTunerModulation(modulationIndex));
        return builder.build();
    }
}
