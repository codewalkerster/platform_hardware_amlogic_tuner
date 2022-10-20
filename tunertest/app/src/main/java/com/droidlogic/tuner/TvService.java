package com.droidlogic.tuner;

import android.content.Context;
import android.content.Intent;
import android.media.tv.TvInputManager;
import android.media.tv.TvInputService;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.view.KeyEvent;
import android.view.Surface;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.droidlogic.tuner.channel.ChannelManager;
import com.droidlogic.tuner.dvr.DvrPlayer;
import com.droidlogic.tuner.dvr.DvrRecorder;
import com.droidlogic.tuner.dvr.RecorderDescriptor;
import com.droidlogic.tuner.player.TvPlayer;
import com.droidlogic.tuner.player.TvPlayerManager;
import com.droidlogic.tuner.setup.SetupActivity;
import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.EventViewModelManager;
import com.droidlogic.tuner.utils.ThreadManager;


public class TvService extends TvInputService {
    private static final String TAG = Constans.TAG;
    private Handler mMainHandler = null;

    public TvService() {
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mMainHandler = new Handler(getMainLooper());
    }

    @Override
    public void onDestroy() {
        ThreadManager.getInstance().release();
        super.onDestroy();
    }

    @Nullable
    @Override
    public Session onCreateSession(@NonNull String s) {
        Log.i(TAG, "onCreateSession:" + s);
        return new TvServiceSession(this);
    }

    class TvServiceSession extends Session {
        private Surface mSurface;
        TvPlayer mLivePlayer;
        DvrPlayer mDvrPlayer;

        @Override
        public boolean onKeyDown(int keyCode, KeyEvent event) {
            if (keyCode == KeyEvent.KEYCODE_MENU) {
                Intent intent = new Intent(getApplicationContext(), SetupActivity.class);
                intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(intent);
            }
            return super.onKeyDown(keyCode, event);
        }

        TvServiceSession(Context context) {
            super(context);
        }

        @Override
        public void onRelease() {
            Log.i(TAG, "onRelease");
        }

        @Override
        public boolean onSetSurface(Surface surface) {
            Log.i(TAG, "onSetSurface " + surface);
            if (surface != null) {
                mSurface = surface;
                if (mLivePlayer != null) {
                    TvPlayerManager.getInstance()
                            .releasePlayer(getApplicationContext(), mLivePlayer);
                    mLivePlayer = null;
                }
            } else {
                if (mLivePlayer != null) {
                    mLivePlayer.stopPlaying(getApplicationContext());
                    TvPlayerManager.getInstance()
                            .releasePlayer(getApplicationContext(), mLivePlayer);
                    mLivePlayer = null;
                }
                mSurface = null;
            }
            return true;
        }

        @Override
        public void onSetStreamVolume(float v) {
            Log.i(TAG, "onSetStreamVolume " + v);
        }

        @Override
        public boolean onTune(Uri uri) {
            Log.i(TAG, "onTune " + uri.toString());
            return false;
        }

        @Override
        public void onSetCaptionEnabled(boolean b) {
            Log.i(TAG, "onSetCaptionEnabled " + b);
        }

        @Override
        public void onAppPrivateCommand(@NonNull String action, @NonNull Bundle data) {
            Log.d(TAG, "onAppPrivateCommand: " + action + ", " + data.toString());
            switch (action) {
                case "tune":
                    final int channelId = data.getInt("id");
                    playChannel(channelId);
                    break;
                case "stop_tune":
                    if (mLivePlayer != null) {
                        mLivePlayer.stopPlaying(getApplicationContext());
                        TvPlayerManager.getInstance()
                                .releasePlayer(getApplicationContext(), mLivePlayer);
                        mLivePlayer = null;
                    }
                    break;
                case "start_dvr": {
                    if (mSurface != null) {
                        mDvrPlayer = new DvrPlayer(getApplicationContext(),
                                mSurface, new DvrPlayer.OnPlaybackListener() {
                            @Override
                            public void onStarted() {
                                postTunerTestEvent("dvr_started", new Bundle());
                            }

                            @Override
                            public void onStopped() {
                                postTunerTestEvent("dvr_stopped", new Bundle());
                            }

                            @Override
                            public void onProgress(int percent) {
                                postTunerTestEvent("dvr_progress", new Bundle());
                            }
                        });
                        RecorderDescriptor content =
                                DvrRecorder.getInstance().getRecorderDescriptor();
                        mDvrPlayer.play(content);
                    }
                }
                break;
                case "stop_dvr":
                    if (mDvrPlayer != null) {
                        mDvrPlayer.stop();
                        mDvrPlayer = null;
                    }
                    break;
                default:
                    super.onAppPrivateCommand(action, data);
                    break;
            }
        }

        @Override
        public View onCreateOverlayView() {
            Log.i(TAG, "onCreateOverlayView");
            return super.onCreateOverlayView();
        }

        void playChannel(int id) {
            int channelNum = ChannelManager.getInstance().getChannelNumber();
            if (channelNum > id) {
                if (mLivePlayer != null) {
                    //Currently, we only design a simple player that cannot change to different
                    // channel, so we should release the exist player
                    TvPlayerManager.getInstance()
                            .releasePlayer(getApplicationContext(), mLivePlayer);
                }
                mLivePlayer = TvPlayerManager.getInstance().createPlayer();
                if (mLivePlayer != null) {
                    mLivePlayer.setSurface(mSurface);
                    mLivePlayer.setCallback(mPlayerCallback);
                    mLivePlayer.playChannel(getApplicationContext(),
                            ChannelManager.getInstance().getChannel(id));
                }
            }
        }

        private TvPlayer.TvPlayerCallback mPlayerCallback =
                new TvPlayer.TvPlayerCallback() {
                    @Override
                    public void onTuneLock() {
                    }

                    @Override
                    public void onTuneUnLocked() {
                        notifyVideoUnavailable(
                                TvInputManager.VIDEO_UNAVAILABLE_REASON_WEAK_SIGNAL);
                    }

                    @Override
                    public void onFirstFrameReady() {
                        notifyVideoAvailable();
                    }
        };

        private void postTunerTestEvent(@NonNull String action, @NonNull Bundle bundle) {
            ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
                @Override
                public void run() {
                    EventViewModelManager.EventViewModel model =
                            EventViewModelManager.getInstance().getModel();
                    if (model != null) {
                        model.getEvent().setValue(
                                new EventViewModelManager.TunerTestEvent(
                                        "dvr_started",
                                        new Bundle()
                                ));
                    }
                }
            }, 0);
        }
    }
}
