package com.droidlogic.tuner.player;

import android.util.Log;

import android.annotation.SuppressLint;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTimestamp;
import android.media.AudioTrack;
import android.media.MediaDescrambler;
import android.media.MediaFormat;
import android.os.Handler;
import android.os.SystemClock;
import java.nio.ByteBuffer;

import com.droidlogic.tuner.utils.TunerHelper;
import com.droidlogic.tuner.utils.Constants;

public class AudioCodecRenderer {
    public static final String TAG = Constants.TAG;
    private AudioTrack mAudioTrack = null;
    private static final int AUDIO_BUFFER_SIZE = 256;
    private final Object mLock;
    private DecoderThread mDecoderThread;
    private static final long AUDIO_TRACK_PREPARE_TIMEOUT = 200;
    private final ByteBuffer mEmptyPacket = ByteBuffer.allocate(AUDIO_BUFFER_SIZE);

    AudioCodecRenderer() {
        mLock = new Object();
    }
    public AudioTrack configure(AudioFormat format, int audioFilterId, int avSyncId) {
        releaseDecoderThread();
        releaseAudioTrack(mAudioTrack);
        mAudioTrack = null;

        Log.d(TAG, "source format:" + format);
        try {
            AudioAttributes audioAttributes = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .build();

            AudioTrack.Builder builder = new AudioTrack.Builder()
                    .setAudioAttributes(audioAttributes)
                    .setBufferSizeInBytes(AUDIO_BUFFER_SIZE * 10)
                    .setOffloadedPlayback(true);

            Log.d(TAG, "audio filter id:" + audioFilterId + ",av sync id:" + avSyncId);
            TunerHelper.AudioTrack.setTunerConfiguration(builder, audioFilterId, avSyncId);

            if (TunerHelper.TunerVersionChecker
                    .isHigherOrEqualVersionTo(TunerHelper.TunerVersionChecker.TUNER_VERSION_1_1)) {
                format = new AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_DEFAULT)
                        .setSampleRate(48000)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build();
                builder.setAudioFormat(format);
            } else {
                builder.setAudioFormat(format);
            }

            mAudioTrack = builder.build();
            prepareAudioTrack();
            mAudioTrack.play();

            startDecoderThread();
        } catch (Exception exception) {
            Log.e(TAG, "Failed to configure AudioTrack:" + exception);
            releaseDecoderThread();
            releaseAudioTrack(mAudioTrack);
            mAudioTrack = null;
        }

        return mAudioTrack;
    }

    private void prepareAudioTrack() {
        long timeout = SystemClock.elapsedRealtime() + AUDIO_TRACK_PREPARE_TIMEOUT;
        while (true) {
            if (SystemClock.elapsedRealtime() >= timeout || !write()) {
                break;
            }
        }
    }

    private boolean write() {
        int written;
        int expectedToWrite;

        if (mEmptyPacket.remaining() <= 0) {
            mEmptyPacket.rewind();
        }
        expectedToWrite = mEmptyPacket.remaining();
        written = mAudioTrack.write(mEmptyPacket, expectedToWrite,
                AudioTrack.WRITE_NON_BLOCKING);
        if (expectedToWrite != AUDIO_BUFFER_SIZE ||
                expectedToWrite != written && written > 0 ) {
            Log.d(TAG, "expected: " + expectedToWrite + ", written: " + written);
        }
        return written == expectedToWrite;
    }

    class DecoderThread extends Thread {
        @Override
        public void run() {
            super.run();
            Log.d(TAG, "DecoderThread started.");

            synchronized (mLock) {
                while (!isInterrupted()) {
                   write();
                    try {
                        mLock.wait(100);
                    } catch (InterruptedException ex) {
                        Thread.currentThread().interrupt();
                    }
                }
            }
            Log.d(TAG, "DecoderThread exiting..");
        }

    }

    private void startDecoderThread() {
        if (mDecoderThread != null) {
            releaseDecoderThread();
        }
        mDecoderThread = new DecoderThread();
        mDecoderThread.start();
    }

    private void releaseDecoderThread() {
        if (mDecoderThread != null) {
            mDecoderThread.interrupt();
            try {
                mDecoderThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
                Thread.currentThread().interrupt();
            }
            mDecoderThread = null;
        }
    }
    private void releaseAudioTrack(AudioTrack audioTrack) {
        if (audioTrack == null)
            return;

        try {
            audioTrack.pause();
            audioTrack.flush();
        } catch (IllegalStateException exception) {
            Log.e(TAG, "can't flush the audiotrack which is in bad state");
        }

        audioTrack.release();
    }

    public void release() {
        releaseDecoderThread();
        releaseAudioTrack(mAudioTrack);
    }
}

