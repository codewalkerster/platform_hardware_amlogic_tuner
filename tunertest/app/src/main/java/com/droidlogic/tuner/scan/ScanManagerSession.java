package com.droidlogic.tuner.scan;

import android.media.tv.tuner.Tuner;
import android.media.tv.tuner.frontend.FrontendSettings;
import android.os.Bundle;

import androidx.annotation.NonNull;

public abstract class ScanManagerSession {
    public abstract FrontendSettings createScanSettings(int freqMhz, @NonNull Bundle bundle);
    //should works before scan, do not run on other thread
    public void onEarlyScan(@NonNull Tuner tuner, @NonNull Bundle bundle) {}
    public void onLaterScan() {}
    public void onTuneEnd() {}
    public boolean isSettingsValid(@NonNull Bundle bundle) { return true;}
}
