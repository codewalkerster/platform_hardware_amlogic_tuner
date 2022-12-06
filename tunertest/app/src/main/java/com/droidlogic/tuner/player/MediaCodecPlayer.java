package com.droidlogic.tuner.player;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import com.droidlogic.tuner.utils.Constants;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.LinkedBlockingQueue;

public class MediaCodecPlayer {
    private final static String TAG = Constants.TAG;
    private MediaFormat mVideoMediaFormat;
    private Surface mSurface;
    private MediaCodec.OnFrameRenderedListener mRenderedListener;
    private MediaCodecCallback mVideoMediaCodecCallback;
    private MediaCodec mMediaCodec;
    private MediaCodecPlayerCallback mPlayerCallback;

    MediaCodecPlayer(@NonNull Surface surface) {
        mSurface = surface;
        mVideoMediaFormat = null;
        mRenderedListener = null;
        mMediaCodec = null;
    }

    void setVideoMediaFormat(MediaFormat mediaFormat) {
        mVideoMediaFormat = mediaFormat;
    }

    void setAudioMediaFormat(MediaFormat mediaFormat) {
    }

    public void setFrameRenderedListener(MediaCodec.OnFrameRenderedListener listener) {
        mRenderedListener = listener;
    }

    public void setPlayerCallback(MediaCodecPlayerCallback callback) {
        mPlayerCallback = callback;
    }

    void startPlayer() {
        mVideoMediaCodecCallback = new MediaCodecCallback();
        String[] codecs = getCodecNames(mVideoMediaFormat);
        if (codecs.length > 0) {
            try {
                Log.d(TAG, "Create video mediacodec by codec name. codecs[0] = " + codecs[0]);
                mMediaCodec = MediaCodec.createByCodecName(codecs[0]);
            } catch (Exception e) {
                Log.d(TAG, "initTunerVideoMediaCodec video createByCodecName Exception = "
                        + e.getMessage());
            }
        } else {
            Log.d(TAG, "initTunerVideoMediaCodec No video decoder found for format= "
                    + mVideoMediaFormat);
        }
        if (mMediaCodec == null) return;
        mMediaCodec.setCallback(mVideoMediaCodecCallback);
        mMediaCodec.setOnFrameRenderedListener(mRenderedListener, null);
        mMediaCodec.configure(mVideoMediaFormat, mSurface, null, 0);
        mMediaCodec.start();
    }

    public void stop() {
        if (mMediaCodec != null) {
            mMediaCodec.stop();
            mMediaCodec.release();
            mMediaCodec = null;
        }
        if (mBufferQueue != null) {
            mBufferQueue.clear();
            mBufferQueue = null;
        }
        mVideoMediaCodecCallback = null;
    }

    private String[] getCodecNames(MediaFormat... formats) {
        List<String> result = new ArrayList<>();
        MediaCodecList list = new MediaCodecList(MediaCodecList.REGULAR_CODECS);
        Log.d(TAG, "getCodecNames Decoders: ");
        for (MediaCodecInfo codecInfo : list.getCodecInfos()) {
            if (codecInfo.isEncoder())
                continue;
            Log.d(TAG, "    " + codecInfo.getName());
            if (codecInfo.isAlias()) {
                continue;
            }
            if (!isAmlogic(codecInfo.getName())) {
                Log.d(TAG, "    Not support " + codecInfo.getName());
                continue;
            }
            for (MediaFormat format : formats) {
                String mime = format.getString(MediaFormat.KEY_MIME);

                MediaCodecInfo.CodecCapabilities caps;
                try {
                    caps = codecInfo.getCapabilitiesForType(mime);
                } catch (IllegalArgumentException e) {  // mime is not supported
                    continue;
                }
                if (caps.isFormatSupported(format)) {
                    result.add(codecInfo.getName());
                    break;
                }
            }
        }
        Log.d(TAG, "getCodecNames Encoders: ");
        for (MediaCodecInfo codecInfo : list.getCodecInfos()) {
            if (codecInfo.isEncoder()) {
                Log.d(TAG, "    " + codecInfo.getName());
            }
        }
        return result.toArray(new String[0]);
    }

    private LinkedBlockingQueue<SlotEvent> mBufferQueue = new LinkedBlockingQueue<>();
    private class MediaCodecCallback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(@NonNull MediaCodec codec, int index) {
            Log.d(TAG, "onInputBufferAvailable index = " + index);
            mBufferQueue.offer(new SlotEvent(true, index));
            if (mPlayerCallback != null) {
                mPlayerCallback.onFirstFrameReady();
            }
        }

        @Override
        public void onOutputBufferAvailable(
                MediaCodec codec, int index, @NonNull MediaCodec.BufferInfo info) {
            Log.d(TAG, "onOutputBufferAvailable = " + index);
            //mBufferQueue.offer(new SlotEvent(false, index));
            codec.releaseOutputBuffer(index, true);
        }

        @Override
        public void onOutputFormatChanged(@NonNull MediaCodec codec, @NonNull MediaFormat format) {
            Log.d(TAG, "onOutputFormatChanged = " + format);
        }

        @Override
        public void onError(@NonNull MediaCodec codec, MediaCodec.CodecException e) {
            Log.d(TAG, "onOutputFormatChanged = " + e.getMessage());
        }
    }

    private static class SlotEvent {
        SlotEvent(boolean input, int index) {
            this.input = input;
            this.index = index;
        }
        final boolean input;
        final int index;
    }

    private boolean isAmlogic(String codecName) {
        codecName = codecName.toLowerCase();
        return (codecName.startsWith("omx.amlogic.") || codecName.startsWith("c2.amlogic."));
    }

    public interface MediaCodecPlayerCallback {
        void onFirstFrameReady();
    }
}
