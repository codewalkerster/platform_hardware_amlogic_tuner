package com.droidlogic.tuner.scan;

import android.content.Context;
import android.media.tv.tuner.Tuner;
import android.util.Log;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.utils.Constants;
import com.droidlogic.tuner.utils.ThreadManager;

import java.util.HashMap;
import java.util.Map;

public class TunerControl {
    private static final String TAG = Constants.TAG;
    private static TunerControl mInstance;
    private HashMap<Integer, Tuner> mTunerContainer = new HashMap<>();

    private TunerControl() {
    }

    public static TunerControl getInstance() {
        if (mInstance == null)
            mInstance = new TunerControl();
        return mInstance;
    }

    public Tuner acquireTuner(Context context, String inputSessionId, final int useCase) {
        if (mTunerContainer.get(useCase) == null) {
            final Tuner tuner = new Tuner(context, inputSessionId, useCase);
            tuner.setResourceLostListener(new ThreadManager.EventExecutor(),
                    new Tuner.OnResourceLostListener() {
                @Override
                public void onResourceLost(Tuner tuner) {
                    Log.i(TAG, "Resource lost of tuner: " + tuner);
                    releaseTuner(tuner);
                }
            });
            mTunerContainer.put(useCase, tuner);
        }
        return mTunerContainer.get(useCase);
    }

    public void releaseTuner(@NonNull Tuner tuner) {
        int toRemoveTunerCase = -1;
        for (Map.Entry<Integer, Tuner> entry : mTunerContainer.entrySet()) {
            Tuner value = entry.getValue();
            if (tuner == value) {
                toRemoveTunerCase = entry.getKey();
                tuner.close();
            }
        }
        if (toRemoveTunerCase != -1) {
            mTunerContainer.remove(toRemoveTunerCase);
        }
    }
}
