package com.droidlogic.tuner.channel;

import android.os.Bundle;

import androidx.annotation.NonNull;

import java.util.ArrayList;
import java.util.List;

public class Channel {
    private Channel(Builder builder) {
        this.programId = builder.programId;
        this.name = builder.name;
        this.pcrId = builder.pcrId;
        this.tp = builder.tp;
        this.videos = builder.videos;
        this.audios = builder.audios;
        this.uniqueId = "" + name + programId + tp.freqMhz + tp.signalType;
    }

    public static final class Builder {
        private int programId;
        private String name;
        private int pcrId;
        private Transponder tp;
        private List<Video> videos;
        private List<Audio> audios;

        public Builder(int programId, int freqMhz, int signalType, @NonNull Bundle scanParam) {
            this.programId = programId;
            this.tp = new Transponder();
            tp.freqMhz = freqMhz;
            tp.signalType = signalType;
            tp.tpArgs = scanParam;
            this.videos = new ArrayList<>();
            this.audios = new ArrayList<>();
        }

        public Builder setName(@NonNull String name) {
            this.name = name;
            return this;
        }

        public void setPcrId(int pcrId) {
            this.pcrId = pcrId;
        }

        public Builder addVideoTrack(int pid, int type) {
            Video v = new Video();
            v.pid = pid;
            v.streamType = type;
            this.videos.add(v);
            return this;
        }

        public Builder addAudioTrack(int pid, int type) {
            Audio a = new Audio();
            a.pid = pid;
            a.streamType = type;
            this.audios.add(a);
            return this;
        }

        public Channel build() {
            return new Channel(this);
        }
    }

    String uniqueId;
    public int programId;
    public String name;
    public int pcrId;
    public Transponder tp;
    public List<Video> videos;
    public List<Audio> audios;

    public static class Transponder {
        public int freqMhz;
        public int signalType;
        public Bundle tpArgs;
    }
    public static class Video {
        public int pid;
        public int streamType;
    }
    public static class Audio {
        public int pid;
        public int streamType;
    }

    @Override
    @NonNull
    public String toString() {
        StringBuilder dump = new StringBuilder();
        dump.append("Channel:name:")
            .append(name).append(", id:")
            .append(programId).append(", pcr:")
            .append(pcrId)
            .append("\n\ttp:(").append("freq:")
            .append(tp.freqMhz).append(", tvType:").append(tp.signalType)
            .append(", args:").append(tp.tpArgs.toString()).append(")\n")
            .append("\tvideos:[");
        for (Video v : videos) {
            dump.append("{pid:").append(v.pid).append(",type:").append(v.streamType).append("},");
        }
        dump.append("]\n\taudio:[");
        for (Audio a : audios) {
            dump.append("{pid:").append(a.pid).append(",type:").append(a.streamType).append("},");
        }
        dump.append("]");
        return dump.toString();
    }
}
