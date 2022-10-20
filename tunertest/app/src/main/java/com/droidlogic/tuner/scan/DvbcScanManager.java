package com.droidlogic.tuner.scan;

import android.media.tv.tuner.frontend.DvbcFrontendSettings;
import android.os.Bundle;

import androidx.annotation.NonNull;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class DvbcScanManager extends ScanManagerSession {
    public static final String KEY_SYMBOL_RATE = "symbolRate";
    public static final String KEY_QAM_MODE = "qamMode";
    private List<Map<String, String>> mQamModeList = new ArrayList<>();

    private final String[] qam_string_array = {
            "Auto", "QAM16", "QAM32", "QAM64", "QAM128", "QAM256"
    };

    DvbcScanManager() {
        for (String q : qam_string_array) {
            HashMap<String, String> m = new HashMap<>();
            m.put("name", q);
            mQamModeList.add(m);
        }
    }

    public List<Map<String, String>> getQamModeSettings() {
        return mQamModeList;
    }

    private int getTunerQamMode(int qamModeIndex) {
        if (qamModeIndex >= 0 && qamModeIndex < qam_string_array.length)
            return (1 << qamModeIndex);
        return DvbcFrontendSettings.MODULATION_AUTO;
    }

    @Override
    public DvbcFrontendSettings createScanSettings(int freqMhz, @NonNull Bundle scanParam) {
        int symbolRate = scanParam.getInt(KEY_SYMBOL_RATE, 0);
        int qamMode = scanParam.getInt(KEY_QAM_MODE, 0);
        DvbcFrontendSettings.Builder builder = DvbcFrontendSettings
                .builder()
                .setFrequency(freqMhz  * 1000000)
                .setAnnex(DvbcFrontendSettings.ANNEX_A);
        builder.setSymbolRate(symbolRate * 1000).setModulation(getTunerQamMode(qamMode));
        return builder.build();
    }
}
