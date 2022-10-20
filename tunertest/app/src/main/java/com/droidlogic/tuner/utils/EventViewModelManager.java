package com.droidlogic.tuner.utils;

import android.os.Bundle;

import androidx.annotation.NonNull;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;
import androidx.lifecycle.ViewModelProvider;
import androidx.lifecycle.ViewModelStoreOwner;

public class EventViewModelManager{
    private static EventViewModelManager mInstance;
    private EventViewModel mModel;

    private EventViewModelManager() {
    }

    public static EventViewModelManager getInstance() {
        if (mInstance == null)
            mInstance = new EventViewModelManager();
        return mInstance;
    }

    public void createModel(ViewModelStoreOwner owner) {
        if (mModel == null)
            mModel = new ViewModelProvider(owner).get(EventViewModel.class);
    }

    public EventViewModel getModel() {
        return mModel;
    }

    public static class EventViewModel extends ViewModel {
        public EventViewModel() {
        }

        private MutableLiveData<TunerTestEvent> mEvent;
        public MutableLiveData<TunerTestEvent> getEvent() {
            if (mEvent == null) {
                mEvent = new MutableLiveData<>();
            }
            return mEvent;
        }
    }

    public static class TunerTestEvent {
        public String event;
        public Bundle bundle;
        public TunerTestEvent(@NonNull String eventName, @NonNull Bundle bundle) {
            this.event = eventName;
            this.bundle = bundle;
        }
    }
}
