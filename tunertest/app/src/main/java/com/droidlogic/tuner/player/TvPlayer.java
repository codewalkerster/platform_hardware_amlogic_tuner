package com.droidlogic.tuner.player;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.tv.TvInputService;
import android.media.tv.tuner.Tuner;
import android.media.tv.tuner.filter.AvSettings;
import android.media.tv.tuner.filter.Filter;
import android.media.tv.tuner.filter.FilterCallback;
import android.media.tv.tuner.filter.FilterConfiguration;
import android.media.tv.tuner.filter.FilterEvent;
import android.media.tv.tuner.filter.Settings;
import android.media.tv.tuner.filter.TsFilterConfiguration;
import android.media.tv.tuner.frontend.FrontendSettings;
import android.media.tv.tuner.frontend.OnTuneEventListener;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.channel.Channel;
import com.droidlogic.tuner.scan.ScanManager;
import com.droidlogic.tuner.scan.ScanManagerSession;
import com.droidlogic.tuner.scan.TunerControl;
import com.droidlogic.tuner.utils.Constants;
import com.droidlogic.tuner.utils.ThreadManager;

public class TvPlayer {
    public static final String TAG = Constants.TAG;
    private Surface mSurface;
    private int mId;
    private ThreadManager.MediaExecutor mExecutor;
    private final Object playerEnvLock = new Object();
    private PlayerEnv mEnv;
    private TvPlayerCallback mCallback;

    TvPlayer(int id) {
        mId = id;
        mExecutor = new ThreadManager.MediaExecutor();
    }

    public void setCallback(TvPlayerCallback callback) {
        mCallback = callback;
    }

    void release(Context context) {
        synchronized (playerEnvLock) {
            if (mEnv != null) {
                if (mEnv.mCodecPlayer != null) {
                    mEnv.mCodecPlayer.stop();
                }
                if (mEnv.mAudioTrack != null) {
                    mEnv.mAudioTrack.stop();
                    mEnv.mAudioTrack.release();
                    mEnv.mAudioTrack = null;
                }
                if (mEnv.mVideoFilter != null) {
                    mEnv.mVideoFilter.stop();
                    mEnv.mVideoFilter.close();
                    mEnv.mVideoFilter = null;
                }
                if (mEnv.mAudioFilter != null) {
                    mEnv.mAudioFilter.stop();
                    mEnv.mAudioFilter.close();
                    mEnv.mAudioFilter = null;
                }
                mEnv = null;
            }
            setSurface(null);
            try {
                Tuner tuner = TunerControl.getInstance().acquireTuner(
                        context, null,
                        TvInputService.PRIORITY_HINT_USE_CASE_TYPE_LIVE);
                tuner.cancelTuning();
                TunerControl.getInstance().releaseTuner(tuner);
            } catch (Exception ignored) {
            }
        }
    }

    int getPlayerId() {
        return mId;
    }

    public void setSurface(Surface surface) {
        mSurface = surface;
    }

    public void playChannel(Context context, @NonNull Channel channel) {
        synchronized (playerEnvLock) {
            mEnv = new PlayerEnv();
            MediaFormat videoFormat;
            AudioFormat audioFormat = null;
            AudioManager audioManager =
                    (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

            Tuner tuner = TunerControl.getInstance().acquireTuner(
                    context, null, TvInputService.PRIORITY_HINT_USE_CASE_TYPE_LIVE);
            if (tuner == null || mSurface == null)
                return;

            if (channel.videos.size() > 0) {
                Channel.Video v = channel.videos.get(0);
                mEnv.mVideoFilter = openVideoFilter(tuner, v.pid);
                videoFormat = createVideoFormat(channel);
                if (mEnv.mVideoFilter == null || videoFormat == null)
                    return;
                videoFormat.setInteger("vendor.tunerhal.video-filter-id",
                        mEnv.mVideoFilter.getId());
            } else {
                return;
            }
            if (channel.audios.size() > 0) {
                Channel.Audio a = channel.audios.get(0);
                mEnv.mAudioFilter = openAudioFilter(tuner, a.pid);
                if (mEnv.mAudioFilter != null) {
                    audioFormat = createAudioFormat(channel);
                }
            }
            //openPcrFilter(tuner, channel.pcrId);
            int avSyncId = tuner.getAvSyncHwId((mEnv.mVideoFilter != null) ?
                    mEnv.mVideoFilter : mEnv.mAudioFilter);
            if (audioManager != null) {
                int audioSessionId = audioManager.generateAudioSessionId();
                videoFormat.setInteger(MediaFormat.KEY_AUDIO_SESSION_ID, audioSessionId);
            }
            videoFormat.setInteger("vendor.tunerhal.hw-av-sync-id", avSyncId);
            videoFormat.setFeatureEnabled(
                    MediaCodecInfo.CodecCapabilities.FEATURE_TunneledPlayback, true);

            mEnv.mCodecPlayer = new MediaCodecPlayer(mSurface);
            mEnv.mCodecPlayer.setPlayerCallback(mOnCodecPlayerCallback);
            mEnv.mCodecPlayer.setVideoMediaFormat(videoFormat);
            if (audioFormat != null) {
                //mEnv.mCodecPlayer.setAudioMediaFormat(audioFormat);
                mEnv.mAudioTrack =
                        CreateAudioTrack(audioFormat, mEnv.mAudioFilter.getId(), avSyncId);
            }
            mEnv.mCodecPlayer.startPlayer();
            if (mEnv.mAudioFilter != null) {
                mEnv.mAudioFilter.start();
            }
            if (channel.tp.signalType == ScanManager.SIGNAL_TYPE_NONE) {
                mEnv.mIsDvrMode = true;
            } else {
                mEnv.mIsDvrMode = false;
                startTunerForPlaying(tuner, channel);
            }
        }
    }

    private AudioTrack CreateAudioTrack(AudioFormat audioFormat, int audioFilterId, int syncId) {
        AudioTrack track = null;
        if (audioFormat != null) {
            try {
                track = new AudioTrack.Builder()
                        .setAudioAttributes(new AudioAttributes.Builder()
                                .setUsage(AudioAttributes.USAGE_MEDIA)
                                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                                .build())
                        .setAudioFormat(audioFormat)
                        .setBufferSizeInBytes(256)
                        .setEncapsulationMode(AudioTrack.ENCAPSULATION_MODE_HANDLE)
                        .setTunerConfiguration(
                                new AudioTrack.TunerConfiguration(audioFilterId, syncId))
                        .build();
                Log.d(TAG, "CreateAudioTrack done!");
            }
            catch (UnsupportedOperationException e) {
                Log.d(TAG, "CreateAudioTrack err:" + e.toString());
            }
        } else {
            Log.e(TAG, "mAudioformat is null!");
        }
        return track;
    }

    public void stopPlaying(@NonNull Context context) {
        synchronized (playerEnvLock) {
            if (mEnv != null) {
                if (mEnv.mCodecPlayer != null) {
                    mEnv.mCodecPlayer.stop();
                }
                if (mEnv.mAudioTrack != null) {
                    mEnv.mAudioTrack.stop();
                }
            }
        }
    }

    public Filter getDvrVideoFilter() {
        synchronized (playerEnvLock) {
            if (mEnv.mIsDvrMode) {
                return mEnv.mVideoFilter;
            }
        }
        return null;
    }

    public Filter getDvrAudioFilter() {
        synchronized (playerEnvLock) {
            if (mEnv.mIsDvrMode) {
                return mEnv.mAudioFilter;
            }
        }
        return null;
    }

    private Filter openVideoFilter(@NonNull Tuner tuner, int pid) {
        long vFilterBufSize = 1024 * 1024 * 10;
        if (pid == 0x1FFF || pid <= 0) {
            return null;
        }

        Filter filter = tuner.openFilter(
                Filter.TYPE_TS,
                Filter.SUBTYPE_VIDEO,
                vFilterBufSize,
                mExecutor, mMediaFilterCallback);
        if (filter == null) {
            return null;
        }

        Settings videoSettings = AvSettings
                .builder(Filter.TYPE_TS, false)
                .setPassthrough(true)
                .build();
        FilterConfiguration videoConfig = TsFilterConfiguration
                .builder()
                .setTpid(pid)
                .setSettings(videoSettings)
                .build();
        filter.configure(videoConfig);
        return filter;
    }

    private Filter openAudioFilter(@NonNull Tuner tuner, int pid) {
        long aFilterBufSize = 1024 * 1024;
        if (pid == 0x1FFF || pid <= 0) {
            return null;
        }

        Filter filter = tuner.openFilter(Filter.TYPE_TS,
                Filter.SUBTYPE_AUDIO,
                aFilterBufSize,
                mExecutor, mMediaFilterCallback);
        if (filter == null) {
            return null;
        }

        Settings audioSettings = AvSettings
                .builder(Filter.TYPE_TS, true)
                .setPassthrough(true)
                .build();
        FilterConfiguration audioConfig = TsFilterConfiguration
                .builder()
                .setTpid(pid)
                .setSettings(audioSettings)
                .build();
        filter.configure(audioConfig);
        return filter;
    }

    private Filter openPcrFilter(@NonNull Tuner tuner, int pid) {
        Filter filter = tuner.openFilter(
                Filter.TYPE_TS,
                Filter.SUBTYPE_PCR,
                1024* 1024,
                mExecutor, mMediaFilterCallback);
        if (filter != null) {
            FilterConfiguration pcrConfig = TsFilterConfiguration
                    .builder()
                    .setTpid(pid)
                    .setSettings(null)
                    .build();
            filter.configure(pcrConfig);
            filter.start();
        }
        return filter;
    }

    private MediaFormat createVideoFormat(@NonNull Channel channel) {
        String videoMimeType = null;
        if (channel.videos.size() > 0) {
            Channel.Video v = channel.videos.get(0);
            switch (v.streamType) {
                case 0x01:
                case 0x02:
                    videoMimeType = MediaFormat.MIMETYPE_VIDEO_MPEG2;
                    break;
                case 0x10:
                    videoMimeType = MediaFormat.MIMETYPE_VIDEO_MPEG4;
                    break;
                case 0x1b:
                    videoMimeType = MediaFormat.MIMETYPE_VIDEO_AVC;
                    break;
                case 0x24:
                    videoMimeType = MediaFormat.MIMETYPE_VIDEO_HEVC;
                    break;
                default:
                    break;
            }
        }
        if (videoMimeType != null) {
            return MediaFormat.createVideoFormat(videoMimeType,
                    1280, 720);
        }
        return null;
    }

    private AudioFormat createAudioFormat(@NonNull Channel channel) {
        AudioFormat format = null;
        if (channel.audios.size() > 0) {
            Channel.Audio a = channel.audios.get(0);
            switch (a.streamType) {
                case 0x03:
                case 0x04:
                    format = new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_MP3)
                            .setSampleRate(48000)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                            .build();
                    break;
                case 0x0e:
                    //Audio auxiliary data
                    break;
                case 0x0f:
                case 0x11:
                    format = new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_AAC_HE_V2)
                            .setSampleRate(48000)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                            .build();
                    break;
                case 0x81:
                    format = new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_AC3)
                            .setSampleRate(48000)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                            .build();
                    break;
                case 0x06:
                case 0x87:
                    format = new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_E_AC3)
                            .setSampleRate(48000)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                            .build();
                    break;
                default:
                    break;
            }
        }
        return format;
    }

    private void startTunerForPlaying(@NonNull Tuner tuner, @NonNull Channel channel) {
        int signalType = channel.tp.signalType;
        int freqMhz = channel.tp.freqMhz;
        ScanManagerSession tunerSession = ScanManager.getInstance().getSession(signalType);
        if (tunerSession != null) {
            tunerSession.onEarlyScan(tuner, channel.tp.tpArgs);
            FrontendSettings settings = tunerSession.createScanSettings(freqMhz, channel.tp.tpArgs);
            tuner.setOnTuneEventListener(mExecutor, mOnPlayerTunerEventListener);
            tuner.tune(settings);
            tunerSession.onLaterScan();
        }
    }

    private FilterCallback mMediaFilterCallback = new FilterCallback() {
        @Override
        public void onFilterEvent(Filter filter, FilterEvent[] events) {

        }

        @Override
        public void onFilterStatusChanged(Filter filter, int status) {

        }
    };

    private OnTuneEventListener mOnPlayerTunerEventListener = new OnTuneEventListener() {
        @Override
        public void onTuneEvent(int tuneEvent) {
            Log.d(TAG, "OnPlayerTunerEventListener: " + tuneEvent);
            if (mCallback != null) {
                if (tuneEvent == SIGNAL_LOCKED) {
                    mCallback.onTuneLock();
                } else {
                    mCallback.onTuneUnLocked();
                }
            }
        }
    };

    private MediaCodecPlayer.MediaCodecPlayerCallback mOnCodecPlayerCallback =
            new MediaCodecPlayer.MediaCodecPlayerCallback() {
                @Override
                public void onFirstFrameReady() {
                    if (mCallback != null) {
                        mCallback.onFirstFrameReady();
                    }
                }
    };

    static class PlayerEnv {
        Filter mVideoFilter;
        Filter mAudioFilter;
        Filter mPcrFilter;
        AudioTrack mAudioTrack;
        MediaCodecPlayer mCodecPlayer;
        boolean mIsDvrMode;
    }

    public interface TvPlayerCallback {
        void onTuneLock();
        void onTuneUnLocked();
        void onFirstFrameReady();
    }
}
