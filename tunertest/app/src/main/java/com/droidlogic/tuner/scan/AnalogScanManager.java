package com.droidlogic.tuner.scan;

import android.media.tv.tuner.frontend.AnalogFrontendSettings;
import android.media.tv.tuner.frontend.AtscFrontendSettings;
import android.media.tv.tuner.frontend.FrontendSettings;
import android.os.Bundle;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import androidx.annotation.NonNull;

public class AnalogScanManager extends ScanManagerSession {
    public static final String KEY_MODE = "mode";
    private List<Map<String, String>> mModeList = new ArrayList<>();

    public AnalogScanManager() {
        String[] mModeList_array = {
                "Auto", "manual"
        };
        for (String q : mModeList_array) {
            HashMap<String, String> m = new HashMap<>();
            m.put("name", q);
            mModeList.add(m);
        }
    }

    public List<Map<String, String>> getModeSettings() {
        return mModeList;
    }

    @Override
    public FrontendSettings createScanSettings(int freqMhz, @NonNull Bundle bundle) {
        int modulationIndex = bundle.getInt(KEY_MODE, 0);
        AnalogFrontendSettings.Builder builder = AnalogFrontendSettings.builder()
                .setFrequency(freqMhz * 1000000 + 250000)
                .setSignalType(AnalogFrontendSettings.SIGNAL_TYPE_AUTO)
                .setSifStandard(AnalogFrontendSettings.SIF_AUTO);
        return builder.build();
    }
}
