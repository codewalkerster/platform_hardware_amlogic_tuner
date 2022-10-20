package com.droidlogic.tuner;

import android.content.Context;
import android.content.DialogInterface;
import android.media.tv.TvInputInfo;
import android.media.tv.TvInputManager;
import android.media.tv.TvView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

import androidx.annotation.RequiresApi;
import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.Observer;

import com.droidlogic.tuner.channel.Channel;
import com.droidlogic.tuner.channel.ChannelManager;
import com.droidlogic.tuner.dvr.DvrRecorder;
import com.droidlogic.tuner.dvr.RecorderDescriptor;
import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.EventViewModelManager;
import com.droidlogic.tuner.utils.ThreadManager;

import java.util.ArrayList;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;

@RequiresApi(api = 31)
public class FullscreenActivity extends AppCompatActivity {
    private static final String TAG = Constans.TAG;
    private TvView tvView;
    private FrameLayout tipsView;
    private Handler mMainHandler = new Handler(Looper.getMainLooper());
    private TvInputManager mTvInputManager;
    private PlayingContext mPlayingContext;
    private TextView mTvPlayType;
    private TextView mDvrReady;
    private TextView mTvRecorderTips;
    private TextView mTvRecorderTime;
    private Timer timer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.hide();
        }
        setContentView(R.layout.activity_fullscreen);
        tvView = findViewById(R.id.tv_view);
        tvView.setCallback(mTvInputCallbacks);
        tipsView = findViewById(R.id.tips);
        mTvPlayType = findViewById(R.id.tvPlayType);
        mDvrReady = findViewById(R.id.playbackReady);
        mTvRecorderTips = findViewById(R.id.tvRecorder);
        mTvRecorderTime = findViewById(R.id.tvRecorderTime);
        mTvInputManager = (TvInputManager) getSystemService(Context.TV_INPUT_SERVICE);
        mMainHandler.post(switchSourceRunnable);
        EventViewModelManager.getInstance().createModel(this);
        final Observer<EventViewModelManager.TunerTestEvent> eventObserver =
                new Observer<EventViewModelManager.TunerTestEvent>() {
            @Override
            public void onChanged(EventViewModelManager.TunerTestEvent tunerTestEvent) {
            }
        };
        EventViewModelManager.getInstance()
                .getModel().getEvent().observe(this, eventObserver);
    }

    @Override
    protected void onResume() {
        tipsView.setVisibility(View.GONE);
        mPlayingContext = new PlayingContext();
        super.onResume();
        timer = new Timer();
        TimerTask timerTask = new TimerTask() {
            @Override
            public void run() {
                updateStatusInfo();
            }
        };
        timer.schedule(timerTask, 1000, 1000);
    }

    private void showTips(boolean show) {
        if (show) {
            tipsView.setVisibility(View.VISIBLE);
        } else {
            tipsView.setVisibility(View.GONE);
        }
    }

    @Override
    protected void onPause() {
        timer.cancel();
        handleStopAction();
        super.onPause();
    }

    @Override
    protected void onStop() {
        ThreadManager.getInstance().release();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
    }

    private void showChannelList() {
        List<String> channels = new ArrayList<>();
        int num = ChannelManager.getInstance().getChannelNumber();
        for (int i = 0; i < num; i ++) {
            Channel channel = ChannelManager.getInstance().getChannel(i);
            if (channel != null) {
                channels.add(channel.programId + ". " + channel.name);
            }
        }
        String[] items = channels.toArray(new String[0]);
        AlertDialog.Builder builder
                = new AlertDialog.Builder(this).setIcon(R.mipmap.ic_launcher)
                .setTitle("Channels")
                .setItems(items, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int which) {
                        playChannel(which);
                    }
                });
        builder.create().show();
    }

    private void switchSource() {
        List<TvInputInfo> inputList = mTvInputManager.getTvInputList();
        for (TvInputInfo info : inputList) {
            String mTunePackage = "com.droidlogic.tuner";
            if (mTunePackage.equals(info.getServiceInfo().packageName)) {
                tvView.tune(info.getId(), null);
            }
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        synchronized (this) {
            if (keyCode == KeyEvent.KEYCODE_MENU) {
                if (mPlayingContext.mode == Play_Mode.Mode_None)
                    return tvView.dispatchKeyEvent(event);
            } else if (keyCode == KeyEvent.KEYCODE_DPAD_CENTER) {
                if (mPlayingContext.mode == Play_Mode.Mode_None
                || mPlayingContext.mode == Play_Mode.Mode_Live)
                    showChannelList();
                return true;
            } else if (keyCode == KeyEvent.KEYCODE_PROG_RED) {
                startRecorder();
                return true;
            } else if (keyCode == KeyEvent.KEYCODE_MEDIA_STOP) {
                handleStopAction();
                return true;
            } else if (keyCode == KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE) {
                startDvrPlayback();
                return true;
            }
        }
        return super.onKeyDown(keyCode, event);
    }

    private synchronized void playChannel(final int id) {
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                Bundle bundle = new Bundle();
                bundle.putInt("id", id);
                tvView.sendAppPrivateCommand("tune", bundle);
            }
        }, 0);
        mPlayingContext.mode = Play_Mode.Mode_Live;
        mPlayingContext.playingChannelId = id;
    }

    private synchronized void stopLivePlaying() {
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                tvView.sendAppPrivateCommand("stop_tune", new Bundle());
            }
        }, 0);
        mPlayingContext.mode = Play_Mode.Mode_None;
        mPlayingContext.playingChannelId = -1;
    }

    private synchronized void stopDvrPlaying() {
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                tvView.sendAppPrivateCommand("stop_dvr", new Bundle());
            }
        }, 0);
        mPlayingContext.mode = Play_Mode.Mode_None;
        mPlayingContext.playbackId = -1;
    }

    private synchronized void startRecorder() {
        if (mPlayingContext.mode == Play_Mode.Mode_Live
                && mPlayingContext.videoAvailable) {
            try {
                Channel channel =
                        ChannelManager.getInstance().getChannel(mPlayingContext.playingChannelId);
                Log.d(TAG, "start Recorder");
                DvrRecorder.getInstance().startRecorder(
                        getApplicationContext(), channel, new ThreadManager.DvrExecutor());
                mPlayingContext.recordingChannelId = mPlayingContext.playingChannelId;
                mPlayingContext.mode = Play_Mode.Mode_Recording;
                mPlayingContext.recorderStartTime = SystemClock.uptimeMillis();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    private synchronized void handleStopAction() {
        if (mPlayingContext.mode == Play_Mode.Mode_Recording) {
            try {
                Channel channel =
                        ChannelManager.getInstance().getChannel(mPlayingContext.recordingChannelId);
                if (channel != null) {
                    DvrRecorder.getInstance().stopRecorder(channel);
                }
                mPlayingContext.mode = Play_Mode.Mode_Live;
                mPlayingContext.recordingChannelId = -1;
                mPlayingContext.recorderStartTime = 0;
            } catch (Exception ignored) {
            }
        } else if (mPlayingContext.mode == Play_Mode.Mode_Playback) {
            stopDvrPlaying();
        } else if (mPlayingContext.mode == Play_Mode.Mode_Live) {
            stopLivePlaying();
        }
    }

    private synchronized void startDvrPlayback() {
        if (mPlayingContext.mode == Play_Mode.Mode_Recording
                || mPlayingContext.mode == Play_Mode.Mode_Playback) {
            return;
        }
        if (mPlayingContext.mode == Play_Mode.Mode_Live) {
            stopLivePlaying();
        }
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                tvView.sendAppPrivateCommand("start_dvr", new Bundle());
            }
        }, 1000);
        RecorderDescriptor content = DvrRecorder.getInstance().getRecorderDescriptor();
        if (content != null) {
            tipsView.setVisibility(View.GONE);
            mPlayingContext.mode = Play_Mode.Mode_Playback;
            mPlayingContext.playbackId = content.programId;
        }
    }

    private void updateStatusInfo() {
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                synchronized (this) {
                    mTvRecorderTips.setVisibility(View.INVISIBLE);
                    mTvRecorderTime.setText("");
                    if (DvrRecorder.getInstance().getRecorderDescriptor() != null) {
                        mDvrReady.setVisibility(View.VISIBLE);
                    } else {
                        mDvrReady.setVisibility(View.INVISIBLE);
                    }
                    if (mPlayingContext.mode == Play_Mode.Mode_None) {
                        mTvPlayType.setText("");
                    } else if (mPlayingContext.mode == Play_Mode.Mode_Live) {
                        mTvPlayType.setText(R.string.play_mode_live);
                    } else if (mPlayingContext.mode == Play_Mode.Mode_Recording) {
                        mTvPlayType.setText(R.string.play_mode_live);
                        mTvRecorderTips.setVisibility(View.VISIBLE);
                        long duration
                                = (SystemClock.uptimeMillis()
                                    - mPlayingContext.recorderStartTime)/1000;
                        mTvRecorderTime.setText("" + duration + "s");
                    } else if (mPlayingContext.mode == Play_Mode.Mode_Playback) {
                        mTvPlayType.setText(R.string.play_mode_playback);
                        mDvrReady.setVisibility(View.INVISIBLE);
                    }
                    if (mPlayingContext.mode == Play_Mode.Mode_None) {
                        showTips(true);
                    } else {
                        showTips(false);
                    }
                }
            }
        }, 0);
    }

    private enum Play_Mode{
        Mode_None,
        Mode_Live,
        Mode_Recording,
        Mode_Playback
    }

    private static class PlayingContext {
        Play_Mode mode;
        int playingChannelId;
        int recordingChannelId;
        int playbackId;
        boolean videoAvailable;
        long recorderStartTime;
        PlayingContext() {
            mode = Play_Mode.Mode_None;
            playingChannelId = -1;
            recordingChannelId = -1;
            playbackId = -1;
            videoAvailable = false;
            recorderStartTime = 0;
        }
    }

    private final Runnable switchSourceRunnable = new Runnable() {
        @Override
        public void run() {
            switchSource();
        }
    };

    private TvView.TvInputCallback mTvInputCallbacks = new TvView.TvInputCallback() {
        @Override
        public void onVideoAvailable(String inputId) {
            super.onVideoAvailable(inputId);
            synchronized (this) {
                mPlayingContext.videoAvailable = true;
            }
        }

        @Override
        public void onVideoUnavailable(String inputId, int reason) {
            super.onVideoUnavailable(inputId, reason);
            synchronized (this) {
                mPlayingContext.videoAvailable = false;
            }
        }
    };
}
