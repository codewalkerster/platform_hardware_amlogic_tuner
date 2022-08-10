package com.droidlogic.tunerframeworksetup;

import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaExtractor;
import android.media.MediaFormat;

import android.util.Log;
import android.view.Surface;

import org.json.JSONObject;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.LinkedBlockingQueue;

import android.os.Handler;

public class MediaCodecPlayer {

    private final static String TAG = MediaCodecPlayer.class.getSimpleName();

    public static final String TEST_MIME_TYPE = MediaFormat.MIMETYPE_VIDEO_AVC;//264
    private static final int APP_BUFFER_SIZE = 2 * 1024 * 1024;  // 2 MB

    //audio
    public static final String AUDIO_MIME_TYPE = MediaFormat.MIMETYPE_AUDIO_AAC;
    public static final int AUDIO_SAMPLE_RATE = 48000;
    public static final int AUDIO_AAC_PROFILE = 2; /* OMX_AUDIO_AACObjectLC */
    public static final int AUDIO_CHANNEL_COUNT = 2; // mono
    public static final int AUDIO_BIT_RATE = 128000;
    //play mode
    public static final String PLAYER_MODE_LOCAL = "local";
    public static final String PLAYER_MODE_TUNER = "tuner";

    private String mPlayerMode = null;
    private String mPlayerMimeType = null;
    private String mPlayerPath = null;
    private boolean mIsPassthrough = false;
    private boolean mIsSecurePlayback = false;
    private Context mContext = null;
    private Surface mSurface = null;
    private boolean mStarted = false;
    private boolean mStopping = false;
    private MediaCodecCallback mVideoMediaCodecCallback = null;
    private AudioMediaCodecCallback mAudioMediaCodecCallback = null;
    private LinearInputBlock mLinearInputBlock = null;
    private MediaCodec mMediaCodec = null;
    private MediaCodec mAudioMediaCodec = null;
    private MediaFormat mMediaFormat = null;
    private MediaFormat mAudioMediaFormat = null;
    private MediaExtractor mMediaExtractor = null;
    private LocalMediaCodecRunnable mLocalMediaCodecRunnable = null;
    private MediaCodecPlayerStatus mMediaCodecPlayerStatus = null;
    private InputSlotListener mInputSlotListener = null;
    private OutputSlotListener mOutputSlotListener = null;
    private Handler mRenderedHandler = null;
    private MediaCodec.OnFrameRenderedListener mRenderedListener = null;

    public MediaCodecPlayer(Context context, Surface surface, String playerMode, String playerMimeType, String playerPath, boolean isPassThrough, boolean isSecurePlayback) {
        this.mContext = context;
        this.mSurface = surface;
        this.mPlayerMode = playerMode;
        this.mPlayerMimeType = playerMimeType;
        this.mPlayerPath = playerPath;
        this.mIsPassthrough = isPassThrough;
        this.mIsSecurePlayback = isSecurePlayback;
    }

    public void setMediaCodecPlayerCallback(MediaCodecPlayerStatus mediaCodecPlayerStatus) {
        mMediaCodecPlayerStatus = mediaCodecPlayerStatus;
    }

    public boolean isStarted() {
        return mStarted;
    }

    public String getPlayerMode() {
        return mPlayerMode;
    }

    public boolean initPlayer() {
        boolean result = true;
        switch (mPlayerMode) {
            case PLAYER_MODE_LOCAL:
                break;
            case PLAYER_MODE_TUNER:
                break;
            default:
                result = false;
                break;
        }
        return result;
    }

    public void setExtractorInputSlotListener(InputSlotListener inputSlotListener) {
        mInputSlotListener = inputSlotListener;
    }

    public void setExtractorOutputSlotListener(OutputSlotListener outputSlotListener) {
        mOutputSlotListener = outputSlotListener;
    }

    public void setVideoMediaFormat(MediaFormat mediaFormat) {
        Log.d(TAG, "setVideoMediaFormat");
        mMediaFormat = mediaFormat;
    }

    public void setAudioMediaFormat(MediaFormat mediaFormat) {
        mAudioMediaFormat = mediaFormat;
    }

    public void setFrameRenderedListener(MediaCodec.OnFrameRenderedListener listener) {
        mRenderedListener = listener;
    }

    public void startPlayer() {
        Log.d(TAG, "startPlayer mPlayerMode:" + mPlayerMode);
        switch (mPlayerMode) {
            case PLAYER_MODE_LOCAL:
                startLocalPlayer();
                break;
            case PLAYER_MODE_TUNER:
                startTunerPlayer();
                break;
            default:
                Log.d(TAG, "startPlayer unknown " + mPlayerMode);
                break;
        }
    }

    public void stopPlayer() {
        Log.d(TAG, "stopPlayer mPlayerMode:" + mPlayerMode);
        switch (mPlayerMode) {
            case PLAYER_MODE_LOCAL:
                stopLocalPlayer();
                break;
            case PLAYER_MODE_TUNER:
                stopTunerPlayer();
                break;
            default:
                Log.d(TAG, "stopPlayer unknown " + mPlayerMode);
                break;
        }
    }

    //return eos status if crash or end
    public boolean WriteInputData(MediaCodec.LinearBlock linearBlock, long timestampUs, int offset, int length) {
        boolean result = true;
        Log.d(TAG, "WriteInputData... ...");
        SlotEvent event = null;
        try {
            event = mBufferQueue.take();
        } catch (InterruptedException e) {
            Log.d(TAG, "LocalMediaCodecRunnable mBufferQueue.take InterruptedException = " + e.getMessage());
            result = false;
        }
        if (event != null) {
            if (event.input) {
                /*if (linearBlock == null) {
                    Log.d(TAG, "WriteInputData linearBlock null");
                    if (mLinearInputBlock != null && mLinearInputBlock.block != null) {
                        Log.d(TAG, "WriteInputData mLinearInputBlock.block inited");
                        try {
                            mLinearInputBlock.block.recycle();
                        } catch (Exception e) {
                            Log.d(TAG, "WriteInputData block.recycle() Exception = " + e.getMessage());
                        }
                        String[] codecNames = new String[]{ mMediaCodec.getName() };
                        mLinearInputBlock.block = MediaCodec.LinearBlock.obtain(
                                Math.toIntExact(APP_BUFFER_SIZE), codecNames);
                        assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                                mLinearInputBlock.block.isMappable());
                        mLinearInputBlock.buffer = mLinearInputBlock.block.map();
                        mLinearInputBlock.offset = 0;
                    } else {
                        Log.d(TAG, "WriteInputData mLinearInputBlock or block not inited");
                    }
                } else {
                    if (mLinearInputBlock != null) {
                        if (mLinearInputBlock.block != null) {
                            Log.d(TAG, "WriteInputData mLinearInputBlock.block inited");
                            //if (linearBlock != mLinearInputBlock.block) {
                                mLinearInputBlock.block = linearBlock;
                                mLinearInputBlock.buffer = linearBlock.map();
                                mLinearInputBlock.offset = 0;
                            //} else {
                            //    Log.d(TAG, "WriteInputData same mLinearInputBlock.block");
                            //}
                        } else {
                            Log.d(TAG, "WriteInputData mLinearInputBlock.block not init");
                            mLinearInputBlock.block = linearBlock;
                            mLinearInputBlock.buffer = mLinearInputBlock.block.map();
                            mLinearInputBlock.offset = 0;
                        }

                    }
                    assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                            mLinearInputBlock.block.isMappable());
                }*/

                mLinearInputBlock.block = linearBlock;
                mInputSlotListener.onInputSlot(mMediaCodec, event.index, timestampUs, offset, length,  mLinearInputBlock, null);
                try {
                    mLinearInputBlock.block.recycle();
                } catch (Exception e) {

                }
            } else {
                result = mOutputSlotListener.onOutputSlot(mMediaCodec, event.index);
            }
        }
        return result;
    }

    public boolean WriteTunerInputVideoData(SetupInstance.LinearInputBlock1 linearBlock, long timestampUs, int offset, int length) {
        boolean result = true;
        SlotEvent event = null;
        Log.d(TAG, "WriteTunerInputVideoData... ...");
        try {
            event = mBufferQueue.take();
        } catch (InterruptedException e) {
            Log.d(TAG, "LocalMediaCodecRunnable mBufferQueue.take InterruptedException = " + e.getMessage());
            result = false;
        }
        if (event != null) {
            Log.d(TAG, "event input:" + event.input);
            if (event.input) {
                mInputSlotListener.onInputSlot(mMediaCodec, event.index, timestampUs, offset, length,  null, linearBlock);
            } else {
                result = mOutputSlotListener.onOutputSlot(mMediaCodec, event.index);
            }
        }
        return result;
    }

    public boolean WriteTunerInputAudioData(SetupInstance.LinearInputBlock1 linearBlock, long timestampUs, int offset, int length) {
        boolean result = true;
        Log.d(TAG, "WriteTunerInputAudioData... ...");
        SlotEvent event = null;
        try {
            event = mAudioBufferQueue.take();
        } catch (InterruptedException e) {
            Log.d(TAG, "WriteTunerInputAudioData mAudioBufferQueue.take InterruptedException = " + e.getMessage());
            result = false;
        }
        if (event != null) {
            Log.d(TAG, "event input:" + event.input);
            if (event.input) {
                mInputSlotListener.onInputSlot(mAudioMediaCodec, event.index, timestampUs, offset, length,  null, linearBlock);
            } else {
                result = mOutputSlotListener.onOutputSlot(mAudioMediaCodec, event.index);
            }
        }
        return result;
    }

    public void startLocalPlayer() {
    Log.d(TAG, "Start local player");
        if (mStarted) {
            Log.d(TAG, "startLocalPlayer started already");
            return;
        }
        mStarted = true;
        if (mVideoMediaCodecCallback == null) {
            mVideoMediaCodecCallback = new MediaCodecCallback();
        }
        if (initLocalMediaExtractor() && initLocalMediaCodec(mIsPassthrough ? false : true)) {
            Log.d(TAG, "initPlayer inited");
            initLocalLinearBlock();
            startVideoMediaCodec();
            startFeedData();
        }
    }

    public void startFeedData() {
    Log.d(TAG, "Start feed data");
        List<Long> timestampList = Collections.synchronizedList(new ArrayList<Long>());
        mInputSlotListener = new ExtractorInputSlotListener.Builder()
                .setExtractor(mMediaExtractor)
                .setLastBufferTimestampUs(null)
                .setObtainBlockForEachBuffer(true)
                .setTimestampQueue(timestampList)
                .setContentEncrypted(mIsSecurePlayback)
                .build();

        mOutputSlotListener = new SurfaceOutputSlotListener(timestampList, null);
        setExtractorInputSlotListener(mInputSlotListener);
        setExtractorOutputSlotListener(mOutputSlotListener);
        if (mLocalMediaCodecRunnable == null) {
            mLocalMediaCodecRunnable = new LocalMediaCodecRunnable();
        }
        new Thread(mLocalMediaCodecRunnable).start();
    }

    public void stopLocalPlayer() {
    Log.d(TAG, "Stop local player");
        if (!mStarted || mStopping) {
            Log.d(TAG, "startLocalPlayer started already");
            return;
        }
        mStopping = true;
        if (mLocalMediaCodecRunnable != null/* && mLocalMediaCodecRunnable.mRunning && !mLocalMediaCodecRunnable.mStopped*/) {
            mLocalMediaCodecRunnable.stopRun();
            mLocalMediaCodecRunnable = null;
        }
        if (mMediaExtractor != null) {
            mMediaExtractor.release();
            mMediaExtractor = null;
        }
        if (mMediaCodec != null) {
            mMediaCodec.release();
            mMediaCodec = null;
        }
        if (mLinearInputBlock != null && mLinearInputBlock.block != null) {
            mLinearInputBlock.block.recycle();
            mLinearInputBlock.block = null;
            mLinearInputBlock = null;
        }
        if (mBufferQueue != null) {
            mBufferQueue.clear();
            mBufferQueue = null;
        }
        mStarted = false;
        mStopping = false;
    }

    public void startTunerPlayer() {
    Log.d(TAG, "Start tuner player and av mediacodec");
        if (mStarted) {
            Log.d(TAG, "Tuner player started already");
            return;
        }
        mStarted = true;
        if (mVideoMediaCodecCallback == null) {
            mVideoMediaCodecCallback = new MediaCodecCallback();
        }

        if (!mIsPassthrough && mAudioMediaCodecCallback == null) {
            mAudioMediaCodecCallback = new AudioMediaCodecCallback();
        }
        //setMediaFormat(mMediaFormat);
        if (!initTunerVideoMediaCodec(mIsPassthrough ? false : true)) {
            Log.e(TAG, "initTunerVideoMediaCodec failed!");
            return;
        } else {
            mMediaCodec.setCallback(mVideoMediaCodecCallback);
            mMediaCodec.setOnFrameRenderedListener(mRenderedListener, mRenderedHandler);
        }

        if (!mIsPassthrough) {
            if (!initTunerAudioMediaCodec(true)) {
                Log.e(TAG, "initTunerAudioMediaCodec failed!");
                return;
            } else {
                mAudioMediaCodec.setCallback(mAudioMediaCodecCallback);
            }
        }
        if (!mIsPassthrough) {
            startFixedTunerInAndOutListener();
            initTunerLinearBlock();
        }

        if (!mIsPassthrough)
            startAudioMediaCodec();
        startVideoMediaCodec();
    }

    public void stopTunerPlayer() {
    Log.d(TAG, "Ready to stop tuner player");
        if (!mStarted || mStopping) {
            Log.d(TAG, "Can not stop tuner player!");
            return;
        }
        mStopping = true;
        if (mMediaCodec != null) {
            mMediaCodec.stop();
            mMediaCodec.release();
            mMediaCodec = null;
        }
        if (!mIsPassthrough && mAudioMediaCodec != null) {
            mAudioMediaCodec.stop();
            mAudioMediaCodec.release();
            mAudioMediaCodec = null;
        }
        if (mLinearInputBlock != null && mLinearInputBlock.block != null) {
            mLinearInputBlock.block.recycle();
            mLinearInputBlock.block = null;
            mLinearInputBlock = null;
        }
        if (mBufferQueue != null) {
            mBufferQueue.clear();
            mBufferQueue = null;
        }
        mStarted = false;
        mStopping = false;
    }

    private void startFixedTunerInAndOutListener() {
        Log.d(TAG, "New TunerInputSlotListener & SurfaceOutputSlotListener");
        List<Long> timestampList = Collections.synchronizedList(new ArrayList<Long>());
        mInputSlotListener = new TunerInputSlotListener.Builder()
                .setMediaFormat(mMediaFormat)
                .setLastBufferTimestampUs(null)
                .setObtainBlockForEachBuffer(true)
                .setTimestampQueue(timestampList)
                .setContentEncrypted(mIsSecurePlayback)
                .build();

        mOutputSlotListener = new SurfaceOutputSlotListener(timestampList, null);
        setExtractorInputSlotListener(mInputSlotListener);
        setExtractorOutputSlotListener(mOutputSlotListener);
        Log.d(TAG, "setExtractorInputSlotListener & setExtractorOutputSlotListener");
    }

    public static final String STARTED = "started";
    public static final String STOPPED = "stopped";

    public interface MediaCodecPlayerStatus {
        void onMediaCodecPlayerStatus(JSONObject status);
    }

    private LinkedBlockingQueue<SlotEvent> mAudioBufferQueue = new LinkedBlockingQueue<SlotEvent>();
    private class AudioMediaCodecCallback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(MediaCodec codec, int index) {
            Log.d(TAG, "audio onInputBufferAvailable index = " + index);
            if (!mStopping && mStarted) {
                mAudioBufferQueue.offer(new SlotEvent(true, index));
            }
        }

        @Override
        public void onOutputBufferAvailable(
                MediaCodec codec, int index, MediaCodec.BufferInfo info) {
            Log.d(TAG, "audio onOutputBufferAvailable = " + index);
            //mBufferQueue.offer(new SlotEvent(false, index));
            if (!mStopping && mStarted) {
                codec.releaseOutputBuffer(index, true);
            }
        }

        @Override
        public void onOutputFormatChanged(MediaCodec codec, MediaFormat format) {
            Log.d(TAG, "onOutputFormatChanged = " + format);
        }

        @Override
        public void onError(MediaCodec codec, MediaCodec.CodecException e) {
            Log.d(TAG, "onOutputFormatChanged = " + e.getMessage());
        }
    }

    private LinkedBlockingQueue<SlotEvent> mBufferQueue = new LinkedBlockingQueue<SlotEvent>();

    private class MediaCodecCallback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(MediaCodec codec, int index) {
            Log.d(TAG, "onInputBufferAvailable index = " + index);
            if (!mStopping && mStarted) {
                mBufferQueue.offer(new SlotEvent(true, index));
            }
        }

        @Override
        public void onOutputBufferAvailable(
                MediaCodec codec, int index, MediaCodec.BufferInfo info) {
            Log.d(TAG, "onOutputBufferAvailable = " + index);
            //mBufferQueue.offer(new SlotEvent(false, index));
            if (!mStopping && mStarted) {
                codec.releaseOutputBuffer(index, true);
            }
        }

        @Override
        public void onOutputFormatChanged(MediaCodec codec, MediaFormat format) {
            Log.d(TAG, "onOutputFormatChanged = " + format);
        }

        @Override
        public void onError(MediaCodec codec, MediaCodec.CodecException e) {
            Log.d(TAG, "onOutputFormatChanged = " + e.getMessage());
        }
    }

    private class SlotEvent {
        SlotEvent(boolean input, int index) {
            this.input = input;
            this.index = index;
        }
        final boolean input;
        final int index;
    }

    private class LinearInputBlock {
        MediaCodec.LinearBlock block;
        ByteBuffer buffer;
        int offset;
    }

    private boolean initLocalMediaExtractor() {
        Log.d(TAG, "initMediaExtractor mPlayerPath = " + mPlayerPath);
        boolean result = true;
        //if (mMediaExtractor == null) {
            mMediaExtractor = new MediaExtractor();
        //}
        try {
            mMediaExtractor.setDataSource(mPlayerPath, null);
        } catch (Exception e) {
            result = false;
            Log.d(TAG, "initMediaExtractor Exception = " + e.getMessage());
        }
        return result;
    }

    /*private boolean initMediaCodec(MediaFormat format, boolean isGoogle, boolean isLinearBlock) {
        Log.d(TAG, "initMediaCodec format = " + format + ", isGoogle = " + isGoogle + ", isLinearBlock = " + isLinearBlock);
        boolean result = true;
        switch (mPlayerMode) {
            case PLAYER_MODE_LOCAL:
                initLocalMediaCodec(true);
                break;
            case PLAYER_MODE_TUNER:
                break;
            default:
                result = false;
                break;
        }

        return result;
    }*/

    private boolean initLocalMediaCodec(boolean isGoogle) {
        Log.d(TAG, "initLocalMediaCodec isGoogle = " + isGoogle);
        boolean result = true;
        int trackIndex;
        MediaFormat trackMediaFormat;
        int selectedTrackIndex = -1;
        MediaFormat selectedTrackMediaFormat = null;
        for (trackIndex = 0; trackIndex < mMediaExtractor.getTrackCount(); trackIndex++) {
            trackMediaFormat = mMediaExtractor.getTrackFormat(trackIndex);
            Log.d(TAG, "initLocalMediaCodec trackMediaFormat = " + trackMediaFormat);
            if (selectedTrackIndex == -1 && trackMediaFormat.getString(MediaFormat.KEY_MIME).startsWith("video/")) {
                selectedTrackIndex = trackIndex;
                selectedTrackMediaFormat = trackMediaFormat;
                mMediaExtractor.selectTrack(trackIndex);
            }
        }
        Log.d(TAG, "initLocalMediaCodec selectedTrackIndex = " + selectedTrackIndex);
        if (selectedTrackIndex == -1) {
            Log.d(TAG, "initLocalMediaCodec couldn't get a video track");
            result = false;
        }
        if (selectedTrackMediaFormat != null) {
            String[] codecs = getCodecNames(true, selectedTrackMediaFormat);
            if (codecs != null && codecs.length > 0) {
                try {
                    Log.d(TAG, "initLocalMediaCodec codecs[0] = " + codecs[0]);
                    mMediaCodec = MediaCodec.createByCodecName(codecs[0]);
                    mMediaFormat = selectedTrackMediaFormat;
                } catch (Exception e) {
                    Log.d(TAG, "initLocalMediaCodec createByCodecName Exception = " + e.getMessage());
                    result = false;
                }
            } else {
                Log.d(TAG, "initLocalMediaCodec No decoder found for format= " + selectedTrackMediaFormat);
                result = false;
            }
        }
        return result;
    }

    private boolean initTunerVideoMediaCodec(boolean isGoogle) {
        Log.d(TAG, "initTunerVideoMediaCodec isGoogle = " + isGoogle);
        boolean result = true;
        //video
        String[] codecs = getCodecNames(isGoogle, mMediaFormat);
        if (codecs != null && codecs.length > 0) {
            try {
                Log.d(TAG, "Create video mediacodec by codec name. codecs[0] = " + codecs[0]);
                mMediaCodec = MediaCodec.createByCodecName(codecs[0]);
            } catch (Exception e) {
                Log.d(TAG, "initTunerVideoMediaCodec video createByCodecName Exception = " + e.getMessage());
                result = false;
            }
        } else {
            Log.d(TAG, "initTunerVideoMediaCodec No video decoder found for format= " + mMediaFormat);
            result = false;
        }
        return result;
    }

    private boolean initTunerAudioMediaCodec(boolean isGoogle) {
        //audio
        boolean result = true;
        String[] audioCodecs = getCodecNames(true, mAudioMediaFormat);
        if (audioCodecs != null && audioCodecs.length > 0) {
            try {
                Log.d(TAG, "Create audio mediacodec by codec name. audioCodecs[0] = " + audioCodecs[0]);
                mAudioMediaCodec = MediaCodec.createByCodecName(audioCodecs[0]);
            } catch (Exception e) {
                Log.d(TAG, "initTunerMediaCodec audio createByCodecName Exception = " + e.getMessage());
                result = false;
            }
        } else {
                Log.d(TAG, "initTunerMediaCodec No audio decoder found for format= " + mAudioMediaFormat);
                result = false;
        }
        return result;
    }

    private void initLocalLinearBlock() {
        Log.d(TAG, "initLocalLinearBlock->set mVideoMediaCodecCallback");
        mMediaCodec.setCallback(mVideoMediaCodecCallback);
        String[] codecNames = new String[]{ mMediaCodec.getName() };
        if (!mMediaCodec.getCodecInfo().isVendor() && mMediaCodec.getName().startsWith("c2.")) {
            assertTrue("Google default c2.* codecs are copy-free compatible with LinearBlocks",
                    MediaCodec.LinearBlock.isCodecCopyFreeCompatible(codecNames));
        }
        mLinearInputBlock = new LinearInputBlock();
        mLinearInputBlock.block = MediaCodec.LinearBlock.obtain(APP_BUFFER_SIZE, codecNames);
        assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                mLinearInputBlock.block.isMappable());
        mLinearInputBlock.buffer = mLinearInputBlock.block.map();
        mLinearInputBlock.offset = 0;
    }

    private void initTunerLinearBlock() {
    Log.d(TAG, "Create LinearInputBlock");
        String[] codecNames = new String[]{ mMediaCodec.getName() };
        if (!mMediaCodec.getCodecInfo().isVendor() && mMediaCodec.getName().startsWith("c2.")) {
            assertTrue("Google default c2.* codecs are copy-free compatible with LinearBlocks",
                    MediaCodec.LinearBlock.isCodecCopyFreeCompatible(codecNames));
        }
        mLinearInputBlock = new LinearInputBlock();
    }

    private void startVideoMediaCodec() {
        Log.d(TAG, "Start video MediaCodec mMediaFormat = " + mMediaFormat);
        //mMediaFormat = MediaFormat.createVideoFormat(MediaCodecPlayer.TEST_MIME_TYPE, 720, 576);
        //If this codec is to be used with LinearBlock and/or HardwareBuffer, pass CONFIGURE_FLAG_USE_BLOCK_MODEL flag

        if (!mIsPassthrough)
            mMediaCodec.configure(mMediaFormat, mSurface, null, MediaCodec.CONFIGURE_FLAG_USE_BLOCK_MODEL);
        else
            mMediaCodec.configure(mMediaFormat, mSurface, null, 0);
        mMediaCodec.start();
    }

    private void startAudioMediaCodec() {
        //mAudioMediaFormat.setInteger(MediaFormat.KEY_AAC_PROFILE, AUDIO_AAC_PROFILE);
        //mAudioMediaFormat.setInteger(MediaFormat.KEY_BIT_RATE, AUDIO_BIT_RATE);
        Log.d(TAG, "Start audio MediaCodec mAudioMediaFormat = " + mAudioMediaFormat);
        if (!mIsPassthrough)
            mAudioMediaCodec.configure(mAudioMediaFormat, null, null, MediaCodec.CONFIGURE_FLAG_USE_BLOCK_MODEL);
        else
            mAudioMediaCodec.configure(mAudioMediaFormat, null, null, 0);
        mAudioMediaCodec.start();
    }

    private String[] getCodecNames(boolean isGoog, MediaFormat... formats) {
        ArrayList<String> result = new ArrayList<>();
        MediaCodecList list = new MediaCodecList(MediaCodecList.REGULAR_CODECS);
        Log.d(TAG, "getCodecNames Decoders: ");
        for (MediaCodecInfo codecInfo : list.getCodecInfos()) {
            if (codecInfo.isEncoder())
                continue;
            Log.d(TAG, "    " + codecInfo.getName());
            String codecName = codecInfo.getName().toLowerCase();
            if (codecInfo.isAlias()) {
                continue;
            }
            if (isAmlogic(codecInfo.getName())) {
                if (isGoog)
                    continue;
            } else if (isGoogle(codecInfo.getName())) {
                if (!isGoog)
                    continue;
            } else {
                Log.d(TAG, "    Not support " + codecInfo.getName());
                continue;
            }

            for (MediaFormat format : formats) {
                String mime = format.getString(MediaFormat.KEY_MIME);

                MediaCodecInfo.CodecCapabilities caps = null;
                try {
                    caps = codecInfo.getCapabilitiesForType(mime);
                } catch (IllegalArgumentException e) {  // mime is not supported
                    continue;
                }
                if (caps.isFormatSupported(format)) {
                    if (!mIsSecurePlayback || !mIsPassthrough)
                        result.add(codecInfo.getName());
                    else
                        result.add(codecInfo.getName() + ".secure");
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
        return result.toArray(new String[result.size()]);
    }

    private boolean isGoogle(String codecName) {
        codecName = codecName.toLowerCase();
        return codecName.startsWith("omx.google.")
                || codecName.startsWith("c2.android.")
                || codecName.startsWith("c2.google.");
    }

    private boolean isAmlogic(String codecName) {
        codecName = codecName.toLowerCase();
        return (codecName.startsWith("omx.amlogic.") || codecName.startsWith("c2.amlogic."));
    }

    private static void assertTrue(String message, boolean condition) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private class LocalMediaCodecRunnable implements Runnable {

        private boolean mRunning = false;
        private boolean mStopped = false;

        public LocalMediaCodecRunnable() {

        }

        @Override
        public void run() {
            Log.d(TAG, "LocalMediaCodecRunnable run");
            mRunning = true;
            mStopped = false;
            startRun();
        }

        public void stopRun() {
            mRunning = false;
            while (!mStopped);
            Log.d(TAG, "LocalMediaCodecRunnable stopRun");
        }

        private void startRun() {
            boolean eos = false;
            boolean signaledEos = false;
            SlotEvent event;
            Log.d(TAG, "LocalMediaCodecRunnable startRun");
            while (mRunning) {
                if (!WriteInputData(mLinearInputBlock.block, 0, 0, 0)) {
                    Log.d(TAG, "WriteInputData... ... interrupt");
                    break;
                }
            }
            if (mRunning) {
                Log.d(TAG, "LocalMediaCodec stop mPlayerMode:" + mPlayerMode);
                switch (mPlayerMode) {
                    case PLAYER_MODE_LOCAL:
                        stopLocalPlayer();
                        break;
                    case PLAYER_MODE_TUNER:
                        stopTunerPlayer();
                        break;
                    default:
                        break;
                }
            } else {
                mRunning = false;
                mStopped = true;
            }
        }
    }

    private interface InputSlotListener {
        void onInputSlot(MediaCodec codec, int index, long timestampUs, int offset, int length, LinearInputBlock input, SetupInstance.LinearInputBlock1 linearBlock);
    }

    private static class ExtractorInputSlotListener implements InputSlotListener {
        public static class Builder {
            public ExtractorInputSlotListener.Builder setExtractor(MediaExtractor extractor) {
                mExtractor = extractor;
                return this;
            }

            public ExtractorInputSlotListener.Builder setLastBufferTimestampUs(Long timestampUs) {
                mLastBufferTimestampUs = timestampUs;
                return this;
            }

            public ExtractorInputSlotListener.Builder setObtainBlockForEachBuffer(boolean enabled) {
                mObtainBlockForEachBuffer = enabled;
                return this;
            }

            public ExtractorInputSlotListener.Builder setTimestampQueue(List<Long> list) {
                mTimestampList = list;
                return this;
            }

            public ExtractorInputSlotListener.Builder setContentEncrypted(boolean encrypted) {
                mContentEncrypted = encrypted;
                return this;
            }

            public ExtractorInputSlotListener build() {
                return new ExtractorInputSlotListener(
                        mExtractor, mLastBufferTimestampUs,
                        mObtainBlockForEachBuffer, mTimestampList,
                        mContentEncrypted);
            }

            private MediaExtractor mExtractor = null;
            private Long mLastBufferTimestampUs = null;
            private boolean mObtainBlockForEachBuffer = false;
            private List<Long> mTimestampList = null;
            private boolean mContentEncrypted = false;
        }

        private ExtractorInputSlotListener(
                MediaExtractor extractor,
                Long lastBufferTimestampUs,
                boolean obtainBlockForEachBuffer,
                List<Long> timestampList,
                boolean contentEncrypted) {
            mExtractor = extractor;
            mLastBufferTimestampUs = lastBufferTimestampUs;
            mObtainBlockForEachBuffer = obtainBlockForEachBuffer;
            mTimestampList = timestampList;
            mContentEncrypted = contentEncrypted;
        }

        @Override
        public void onInputSlot(MediaCodec codec, int index, long timestampUs, int offset, int length, LinearInputBlock input, SetupInstance.LinearInputBlock1 linearBlock) {
            // Try to feed more data into the codec.
            if (mExtractor.getSampleTrackIndex() == -1 || mSignaledEos) {
                Log.d(TAG, "onInputSlot getSampleTrackIndex = " + mExtractor.getSampleTrackIndex() + ", mSignaledEos = " + mSignaledEos);
                return;
            }
            long size = mExtractor.getSampleSize();
            Log.d(TAG, "Extractor onInputSlot index = " + index + " getSampleSize:" + size);
            String[] codecNames = new String[]{ codec.getName() };
            if (mContentEncrypted) {
                codecNames[0] = codecNames[0] + ".secure";
            }
            if (mObtainBlockForEachBuffer) {
                input.block.recycle();
                input.block = MediaCodec.LinearBlock.obtain(Math.toIntExact(size), codecNames);
                assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                        input.block.isMappable());
                input.buffer = input.block.map();
                input.offset = 0;
            }
            if (input.buffer.capacity() < size) {
                input.block.recycle();
                input.block = MediaCodec.LinearBlock.obtain(
                        Math.toIntExact(size * 2), codecNames);
                assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                        input.block.isMappable());
                input.buffer = input.block.map();
                input.offset = 0;
            } else if (input.buffer.capacity() - input.offset < size) {
                long capacity = input.buffer.capacity();
                input.block.recycle();
                input.block = MediaCodec.LinearBlock.obtain(
                        Math.toIntExact(capacity), codecNames);
                assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                        input.block.isMappable());
                input.buffer = input.block.map();
                input.offset = 0;
            }
            /*long */timestampUs = mExtractor.getSampleTime();
            int written = mExtractor.readSampleData(input.buffer, input.offset);
            boolean encrypted =
                    (mExtractor.getSampleFlags() & MediaExtractor.SAMPLE_FLAG_ENCRYPTED) != 0;
            if (encrypted) {
                mExtractor.getSampleCryptoInfo(mCryptoInfo);
            }
            mExtractor.advance();
            mSignaledEos = mExtractor.getSampleTrackIndex() == -1
                    || (mLastBufferTimestampUs != null && timestampUs >= mLastBufferTimestampUs);
            if (true) {
                ByteBuffer buffer = input.block.map();
                buffer.mark();
                int count = 20;
                byte[] printByte = new byte[count];
                int capacity = buffer.capacity();
                int remainling = buffer.remaining();
                int position = buffer.position();
                Log.d(TAG, "print capacity " + capacity + ", position = " + position + ", remainling = " + remainling);
                for (int i = 0; i < count; i++) {
                    printByte[i] = buffer.get();
                }
                Log.d(TAG, "print Byte:" + Arrays.toString(printByte));
                buffer.reset();
            }
            MediaCodec.QueueRequest request = codec.getQueueRequest(index);
            if (encrypted) {
                request.setEncryptedLinearBlock(
                        input.block, input.offset, written, mCryptoInfo);
            } else {
                request.setLinearBlock(input.block, input.offset, written);
            }
            boolean print = true;
            int count = 100;
            ByteBuffer buffer = input.block.map();
            Log.d(TAG, "running bytePrint capacity = " + buffer.capacity());
            if (print && buffer.capacity() > count) {
                byte[] bytePrint = new byte[count];
                buffer.get(bytePrint, 0, count);
                if (bytePrint != null && bytePrint.length > 0) {
                    Log.d(TAG, "running bytePrint = " + Arrays.toString(bytePrint));
                } else {

                }
                //return;
            }
            request.setPresentationTimeUs(timestampUs);
            Log.d(TAG, "onInputSlot timestampUs = " + timestampUs + ", input.offset = " + input.offset + ", written = " + written + ", mSignaledEos = " + mSignaledEos);
            request.setFlags(mSignaledEos ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0);
            if (mSetParams) {
                request.setIntegerParameter("vendor.int", 0);
                request.setLongParameter("vendor.long", 0);
                request.setFloatParameter("vendor.float", (float)0);
                request.setStringParameter("vendor.string", "str");
                request.setByteBufferParameter("vendor.buffer", ByteBuffer.allocate(1));
                mSetParams = false;
            }
            request.queue();
            input.offset += written;
            if (mTimestampList != null) {
                mTimestampList.add(timestampUs);
            }
        }

        private final MediaExtractor mExtractor;
        private final Long mLastBufferTimestampUs;
        private final boolean mObtainBlockForEachBuffer;
        private final List<Long> mTimestampList;
        private boolean mSignaledEos = false;
        private boolean mSetParams = true;
        private final MediaCodec.CryptoInfo mCryptoInfo = new MediaCodec.CryptoInfo();
        private final boolean mContentEncrypted;
    }

    private static class TunerInputSlotListener implements InputSlotListener {
        public static class Builder {
            public TunerInputSlotListener.Builder setExtractor(MediaExtractor extractor) {
                tExtractor = extractor;
                return this;
            }

            public TunerInputSlotListener.Builder setMediaFormat(MediaFormat mediaFormat) {
                tMediaFormat= mediaFormat;
                return this;
            }

            public TunerInputSlotListener.Builder setLastBufferTimestampUs(Long timestampUs) {
                tLastBufferTimestampUs = timestampUs;
                return this;
            }

            public TunerInputSlotListener.Builder setObtainBlockForEachBuffer(boolean enabled) {
                tObtainBlockForEachBuffer = enabled;
                return this;
            }

            public TunerInputSlotListener.Builder setTimestampQueue(List<Long> list) {
                tTimestampList = list;
                return this;
            }

            public TunerInputSlotListener.Builder setContentEncrypted(boolean encrypted) {
                tContentEncrypted = encrypted;
                return this;
            }

            public TunerInputSlotListener build() {
                return new TunerInputSlotListener(
                        tExtractor, tMediaFormat, tLastBufferTimestampUs,
                        tObtainBlockForEachBuffer, tTimestampList,
                        tContentEncrypted);
            }

            private MediaExtractor tExtractor = null;
            private MediaFormat tMediaFormat = null;
            private Long tLastBufferTimestampUs = null;
            private boolean tObtainBlockForEachBuffer = false;
            private List<Long> tTimestampList = null;
            private boolean tContentEncrypted = false;
        }

        private TunerInputSlotListener(
                MediaExtractor extractor,
                MediaFormat mediaFormat,
                Long lastBufferTimestampUs,
                boolean obtainBlockForEachBuffer,
                List<Long> timestampList,
                boolean contentEncrypted) {
            tExtractor = extractor;
            tMediaFormat = mediaFormat;
            tLastBufferTimestampUs = lastBufferTimestampUs;
            tObtainBlockForEachBuffer = obtainBlockForEachBuffer;
            tTimestampList = timestampList;
            tContentEncrypted = contentEncrypted;
        }

        @Override
        public void onInputSlot(MediaCodec codec, int index, long timestampUs, int offset, int length, LinearInputBlock input, SetupInstance.LinearInputBlock1 linearBlock) {
            // Try to feed more data into the codec.
            if (tSignaledEos) {
                Log.d(TAG, "onInputSlot tSignaledEos = " + tSignaledEos);
                return;
            }
            Log.d(TAG, "Tuner onInputSlot index = " + index + " tContentEncrypted:" + tContentEncrypted);
            String[] codecNames = new String[]{ codec.getName() };
            if (tContentEncrypted) {
                codecNames[0] = codecNames[0] + ".secure";
            }

            if (false) {
                ByteBuffer buffer = linearBlock.buffer;
                buffer.mark();
                int count = 20;
                byte[] printByte = new byte[count];
                int capacity = buffer.capacity();
                int remainling = buffer.remaining();
                int position = buffer.position();
                Log.d(TAG, "print capacity " + capacity + ", position = " + position + ", remainling = " + remainling);
                for (int i = 0; i < count; i++) {
                    printByte[i] = buffer.get();
                    //Integer.toHexString(i);
                }
                Log.d(TAG, "print Byte:" + Arrays.toString(printByte));
                buffer.reset();
            }

            MediaCodec.QueueRequest request = codec.getQueueRequest(index);
            //input.offset = offset;
            if (tContentEncrypted) {
                request.setEncryptedLinearBlock(
                        linearBlock.block, 0, length, mCryptoInfo);
            } else {
                request.setLinearBlock(linearBlock.block, 0, length);
            }

            request.setPresentationTimeUs(timestampUs);
            Log.d(TAG, "onInputSlot timestampUs = " + timestampUs + ", input.offset = " + 0 + ", length = " + length + ", tSignaledEos = " + tSignaledEos);
            request.setFlags(tSignaledEos ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0);
            request.queue();

          if (linearBlock.block != null) {
              linearBlock.block.recycle();
              linearBlock.block = null;
              linearBlock = null;
          }

            //linearBlock.offset += length;
            if (tTimestampList != null) {
                tTimestampList.add(timestampUs);
            }
        }

        private final MediaExtractor tExtractor;
        private final Long tLastBufferTimestampUs;
        private final boolean tObtainBlockForEachBuffer;
        private final List<Long> tTimestampList;
        private boolean tSignaledEos = false;
        private boolean tSetParams = true;
        private final MediaCodec.CryptoInfo mCryptoInfo = new MediaCodec.CryptoInfo();
        private final boolean tContentEncrypted;
        private MediaFormat tMediaFormat;
    }

    private static interface OutputSlotListener {
        // Returns true if EOS is met
        boolean onOutputSlot(MediaCodec codec, int index);
    }

    private static class SurfaceOutputSlotListener implements OutputSlotListener {
        public SurfaceOutputSlotListener(
                List<Long> timestampList,
                List<FormatChangeEvent> events) {
            mTimestampList = timestampList;
            mEvents = (events != null) ? events : (new ArrayList<FormatChangeEvent>());
        }

        @Override
        public boolean onOutputSlot(MediaCodec codec, int index) {
            Log.d(TAG, "SurfaceOutputSlotListener onOutputSlot index = " + index);
            MediaCodec.OutputFrame frame = codec.getOutputFrame(index);
            boolean eos = (frame.getFlags() & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;

            boolean render = false;
            if (frame.getHardwareBuffer() != null) {
                frame.getHardwareBuffer().close();
                render = true;
            }

            mTimestampList.remove(frame.getPresentationTimeUs());

            if (!frame.getChangedKeys().isEmpty()) {
                mEvents.add(new FormatChangeEvent(
                        frame.getPresentationTimeUs(), frame.getChangedKeys(), frame.getFormat()));
            }
            Log.d(TAG, "SurfaceOutputSlotListener onOutputSlot render = " + render);
            codec.releaseOutputBuffer(index, true);

            return !eos;
        }
        private final List<Long> mTimestampList;
        private final List<FormatChangeEvent> mEvents;
    }

    private static class FormatChangeEvent {
        FormatChangeEvent(long ts, Set<String> keys, MediaFormat fmt) {
            timestampUs = ts;
            changedKeys = new HashSet<String>(keys);
            format = new MediaFormat(fmt);
        }

        long timestampUs;
        Set<String> changedKeys;
        MediaFormat format;

        @Override
        public String toString() {
            return Long.toString(timestampUs) + "us: changed keys=" + changedKeys
                    + " format=" + format;
        }
    }

    public interface onLicenseReceiveListener {
        public void onLicenseReceive();
    }
}
