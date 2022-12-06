package com.droidlogic.tuner.dvr;

import android.content.Context;
import android.media.tv.TvInputService;
import android.media.tv.tuner.Tuner;
import android.media.tv.tuner.dvr.DvrPlayback;
import android.media.tv.tuner.dvr.DvrSettings;
import android.media.tv.tuner.dvr.OnPlaybackStatusChangedListener;
import android.media.tv.tuner.filter.Filter;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.channel.Channel;
import com.droidlogic.tuner.player.TvPlayer;
import com.droidlogic.tuner.player.TvPlayerManager;
import com.droidlogic.tuner.scan.ScanManager;
import com.droidlogic.tuner.scan.TunerControl;
import com.droidlogic.tuner.utils.Constants;
import com.droidlogic.tuner.utils.ThreadManager;

import java.io.File;

public class DvrPlayer {
    private static final String TAG = Constants.TAG;
    private DvrPlayback mDvrPlayback;
    private TvPlayer mPlayer;
    private Context mContext;
    private OnPlaybackListener mListener;
    private Filter mVideoFilter;
    private Filter mAudioFilter;
    private boolean mIsStarted;

    public DvrPlayer(@NonNull Context context,
                     @NonNull Surface surface, OnPlaybackListener listener) {
        mContext = context;
        mPlayer = TvPlayerManager.getInstance().createPlayer();
        if (mPlayer != null) {
            mPlayer.setSurface(surface);
        }
        mListener = listener;
    }

    public synchronized boolean play(@NonNull RecorderDescriptor content) {
        File file = null;
        if (content.filePath != null) {
            file = new File(content.filePath);
            if (!file.exists()) {
                return false;
            }
        }
        if (mPlayer == null)
            return false;
        try {
            Tuner tuner = TunerControl.getInstance().acquireTuner(
                    mContext, null, TvInputService.PRIORITY_HINT_USE_CASE_TYPE_LIVE);
            if (tuner != null) {
                mDvrPlayback = tuner.openDvrPlayback(1024*188*10,
                        new ThreadManager.DvrExecutor(), getPlaybackListener());
                if (mDvrPlayback != null) {
                    mDvrPlayback.configure(DvrSettings
                            .builder()
                            .setStatusMask(Filter.STATUS_DATA_READY)
                            .setLowThreshold(128*188)
                            .setHighThreshold(512*188)
                            .setPacketSize(188L)
                            .setDataFormat(DvrSettings.DATA_FORMAT_TS)
                            .build()
                    );
                    ParcelFileDescriptor fd = ParcelFileDescriptor.open(file,
                            ParcelFileDescriptor.MODE_READ_ONLY);
                    if (fd == null || fd.getStatSize() == 0) {
                        mDvrPlayback.close();
                        mDvrPlayback = null;
                        return false;
                    }
                    mDvrPlayback.setFileDescriptor(fd);
                    mPlayer.playChannel(mContext, createVirtualChannel(content));
                    mVideoFilter = mPlayer.getDvrVideoFilter();
                    if (mVideoFilter != null)
                        mDvrPlayback.attachFilter(mVideoFilter);
                    mAudioFilter = mPlayer.getDvrAudioFilter();
                    if (mAudioFilter != null) {
                        mDvrPlayback.attachFilter(mAudioFilter);
                    }
                    mDvrPlayback.start();
                    mDvrPlayback.flush();
                    writeDvrData();
                    mIsStarted = true;
                    if (mListener != null) {
                        mListener.onStarted();
                    }
                    Log.i(TAG, "Start dvr play with file:" + content.filePath
                        + ", size:" + fd.getStatSize());
                }
            }
        } catch (Exception ignored) {
        }
        return false;
    }

    public synchronized void stop() {
        mIsStarted = false;
        if (mDvrPlayback != null) {
            if (mVideoFilter != null) {
                mDvrPlayback.detachFilter(mVideoFilter);
            }
            if (mAudioFilter != null) {
                mDvrPlayback.detachFilter(mAudioFilter);
            }
            mDvrPlayback.stop();
            mDvrPlayback.close();
            mDvrPlayback = null;
        }
        if (mPlayer != null) {
            mPlayer.stopPlaying(mContext);
            TvPlayerManager.getInstance().releasePlayer(mContext, mPlayer);
        }
        if (mListener != null) {
            mListener.onStopped();
        }
    }

    private synchronized void onReachEnd() {
        stop();
    }

    private void writeDvrData() {
        if (mDvrPlayback == null)
            return;

        new Thread(new Runnable() {
            @Override
            public void run() {
                while (mIsStarted) {
                    try {
                        Thread.sleep(16);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    long readSize = mDvrPlayback.read(188 * 1024);
                    if (readSize == 0) {
                        break;
                    } else {
                        if (mListener != null) {
                            mListener.onProgress(10);
                        }
                    }
                }
            }
        }).start();
    }

    private Channel createVirtualChannel(@NonNull RecorderDescriptor content) {
        return new Channel.Builder(
                content.programId, 0, ScanManager.SIGNAL_TYPE_NONE, new Bundle())
                .addVideoTrack(content.video().pid, content.video().streamType)
                .addAudioTrack(content.audio().pid, content.audio().streamType)
                .build();
    }

    private OnPlaybackStatusChangedListener getPlaybackListener() {
        return new OnPlaybackStatusChangedListener() {
            @Override
            public void onPlaybackStatusChanged(int status) {
                Log.d(TAG, "onPlaybackStatusChanged:" + status);
            }
        };
    }

    public interface OnPlaybackListener {
        public void onStarted();
        public void onStopped();
        public void onProgress(int percent);
    }
}