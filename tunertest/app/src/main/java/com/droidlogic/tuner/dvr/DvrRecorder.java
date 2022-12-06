package com.droidlogic.tuner.dvr;

import android.content.Context;
import android.media.tv.tuner.dvr.OnRecordStatusChangedListener;
import android.util.Log;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.channel.Channel;
import com.droidlogic.tuner.utils.Constants;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;

public class DvrRecorder {
    private static final String TAG = Constants.TAG;
    private static DvrRecorder mInstance;
    private List<Recorder> mRecorders;
    private List<RecorderDescriptor> mRecorderContents;

    private DvrRecorder() {
        mRecorders = new ArrayList<>();
        mRecorderContents = new ArrayList<>();
    }

    public static DvrRecorder getInstance() {
        if (mInstance == null) {
            mInstance = new DvrRecorder();
        }
        return mInstance;
    }

    public void release() throws IOException {
        for (Recorder r : mRecorders) {
            r.stop();
        }
        mRecorders.clear();

        for (RecorderDescriptor c : mRecorderContents) {
            removeDescriptorFile(c);
        }
        mRecorderContents.clear();
    }

    public void startRecorder(@NonNull Context context,
                              @NonNull Channel channel,
                              Executor executor) throws IOException {
        Log.i(TAG, "Start recorder for channel id: " + channel.programId);
        Recorder recorder = null;
        for (Recorder r : mRecorders) {
            if (r.getRecordingChannelId() == channel.programId) {
                recorder = r;
                break;
            }
        }
        if (recorder == null) {
            Recorder r = new Recorder(channel);
            Log.i(TAG, "Create new recorder for channel id: " + channel.programId);
            RecorderDescriptor content = r.start(context, executor);
            if (content != null) {
                mRecorders.add(r);
                addRecorderDescriptors(content);
                return;
            }
        }
        Log.i(TAG, "Recorder for " + channel.programId + " has been created before.");
    }

    public void stopRecorder(@NonNull Channel channel) throws IOException {
        Recorder recorder = null;
        for (Recorder r : mRecorders) {
            if (r.getRecordingChannelId() == channel.programId) {
                recorder = r;
                break;
            }
        }
        if (recorder != null) {
            recorder.stop();
            mRecorders.remove(recorder);
        }
    }

    public RecorderDescriptor getRecorderDescriptor() {
        if (mRecorderContents.size() > 0) {
            return mRecorderContents.get(0);
        }
        return null;
    }

    private void addRecorderDescriptors(@NonNull RecorderDescriptor content) {
        RecorderDescriptor existContent = null;
        for (RecorderDescriptor c : mRecorderContents) {
            if (c.programId == content.programId) {
                existContent = c;
                break;
            }
        }
        if (existContent != null) {
            mRecorderContents.remove(existContent);
        }
        mRecorderContents.add(content);
    }

    private void removeDescriptorFile(@NonNull RecorderDescriptor c) {
        try {
            File f = new File(c.filePath);
            if (f.exists()) {
                f.deleteOnExit();
            }
        } catch (Exception ignored) {
        }
    }

    public static class OnRecordStatusChangedListenerImpl
            implements OnRecordStatusChangedListener {
        OnRecordStatusChangedListenerImpl() {
        }

        @Override
        public void onRecordStatusChanged(int status) {
            Log.d(TAG, "onRecordStatusChanged status:" + status);
        }
    }
}
