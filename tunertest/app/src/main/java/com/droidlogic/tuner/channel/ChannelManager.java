package com.droidlogic.tuner.channel;

import android.util.Log;

import com.droidlogic.tuner.utils.Constans;

import java.util.ArrayList;
import java.util.List;

public class ChannelManager {
    private static String TAG = Constans.TAG;
    private static ChannelManager mInstance;
    private List<Channel> mChannelList = new ArrayList<>();

    private ChannelManager() {
    }

    public static ChannelManager getInstance() {
        if (mInstance == null) {
            mInstance = new ChannelManager();
        }
        return mInstance;
    }

    public Channel getChannel(int index) {
        if (mChannelList.size() > index) {
            return mChannelList.get(index);
        } else if (mChannelList.size() > 0) {
            return mChannelList.get(0);
        }
        return null;
    }

    public int getChannelNumber() {
        return mChannelList.size();
    }

    public void addChannel(Channel channel) {
        if (findChannel(channel.uniqueId) == null) {
            mChannelList.add(channel);
        }
    }

    public void clearChannels() {
        mChannelList.clear();
    }

    private Channel findChannel(String uniqueId) {
        for (Channel channel : mChannelList) {
            if (channel.uniqueId.equals(uniqueId))
                return channel;
        }
        return null;
    }

    public void dump() {
        for (Channel ch: mChannelList) {
            Log.d(TAG, ch.toString());
        }
    }
}
