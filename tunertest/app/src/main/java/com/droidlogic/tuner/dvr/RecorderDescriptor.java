package com.droidlogic.tuner.dvr;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.channel.Channel;

//We only recorder A/V es for test, no other psi/si tables
//So, if we want to play the recorded file, need extra pid info of A/V
public class RecorderDescriptor {
    private Channel.Video videoInfo;
    private Channel.Audio audioInfo;
    public int programId;
    String filePath;
    private boolean isRecording;

    RecorderDescriptor(@NonNull Channel channel, @NonNull String tsFilePath) {
        if (channel.videos.size() > 0)
            this.videoInfo = channel.videos.get(0);
        if (channel.audios.size() > 0)
            this.audioInfo = channel.audios.get(0);
        this.programId = channel.programId;
        this.filePath = tsFilePath;
        this.isRecording = true;
    }

    public Channel.Video video() {
        return videoInfo;
    }

    public Channel.Audio audio() {
        return audioInfo;
    }

    void finish() {
        this.isRecording = false;
    }
}
