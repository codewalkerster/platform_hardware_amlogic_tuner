package com.droidlogic.tuner.utils;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;

import java.util.concurrent.Executor;

public class ThreadManager {
    private static ThreadManager mInstance;
    private HandlerThread mThreadScan;
    private HandlerThread mThreadEvent;
    private HandlerThread mThreadAsync;
    private Handler mHandlerMain;
    private Handler mHandlerScanMain;
    private Handler mHandlerEvent;
    private Handler mHandlerAsync;

    private ThreadManager() {
    }

    public static ThreadManager getInstance() {
        if (mInstance == null)
            mInstance = new ThreadManager();
        return mInstance;
    }

    private synchronized void acquireMainThread() {
        if (mHandlerMain == null) {
            mHandlerMain = new Handler(Looper.getMainLooper());
        }
    }

    private synchronized void acquireScanThread() {
        if (mThreadScan == null) {
            mThreadScan = new HandlerThread("tis-scan");
            mThreadScan.start();
        }
        if (mHandlerScanMain == null) {
            mHandlerScanMain = new Handler(mThreadScan.getLooper());
        }
    }

    private synchronized void acquireEventThread() {
        if (mThreadEvent == null) {
            mThreadEvent = new HandlerThread("tis-event");
            mThreadEvent.start();
        }
        if (mHandlerEvent == null) {
            mHandlerEvent = new Handler(mThreadEvent.getLooper());
        }
    }

    private synchronized void acquireAsyncThread() {
        if (mThreadAsync == null) {
            mThreadAsync = new HandlerThread("tis-async");
            mThreadAsync.start();
        }
        if (mHandlerAsync == null) {
            mHandlerAsync = new Handler(mThreadAsync.getLooper());
        }
    }

    public synchronized void release() {
        if (mHandlerMain != null) {
            mHandlerMain.removeCallbacksAndMessages(null);
            mHandlerMain = null;
        }
        if (mHandlerScanMain != null) {
            mHandlerScanMain.removeCallbacksAndMessages(null);
            mHandlerScanMain = null;
        }
        if (mThreadScan != null) {
            mThreadScan.quitSafely();
            mThreadScan = null;
        }
        if (mHandlerEvent != null) {
            mHandlerEvent.removeCallbacksAndMessages(null);
            mHandlerEvent = null;
        }
        if (mThreadEvent != null) {
            mThreadEvent.quitSafely();
            mThreadEvent = null;
        }
        if (mHandlerAsync != null) {
            mHandlerAsync.removeCallbacksAndMessages(null);
            mHandlerAsync = null;
        }
        if (mThreadAsync != null) {
            mThreadAsync.quitSafely();
            mThreadAsync = null;
        }
    }

    public void runOnMainThreadDelayed(Runnable r, int delayMs) {
        acquireMainThread();
        if (mHandlerMain != null) {
            mHandlerMain.postDelayed(r, delayMs);
        }
    }

    public void runOnScanThreadDelayed(Runnable r, int delayMs) {
        acquireScanThread();
        if (mHandlerScanMain != null) {
            mHandlerScanMain.postDelayed(r, delayMs);
        }
    }

    public void runOnEventThreadDelayed(Runnable r, int delayMs) {
        acquireEventThread();
        if (mHandlerEvent != null) {
            mHandlerEvent.postDelayed(r, delayMs);
        }
    }

    public void runOnAsyncThreadDelayed(Runnable r, int delayMs) {
        acquireAsyncThread();
        if (mHandlerAsync != null) {
            mHandlerAsync.postDelayed(r, delayMs);
        }
    }

    public static class TunerExecutor implements Executor {
        public void execute(Runnable r) {
            ThreadManager.getInstance().runOnScanThreadDelayed(r, 0);
        }
    }

    public static class MediaExecutor implements Executor {
        @Override
        public void execute(Runnable r) {
            ThreadManager.getInstance().runOnAsyncThreadDelayed(r, 0);
        }
    }

    public static class DvrExecutor implements Executor {
        @Override
        public void execute(Runnable r) {
            ThreadManager.getInstance().runOnEventThreadDelayed(r, 0);
        }
    }

    public static class EventExecutor implements Executor {
        @Override
        public void execute(Runnable r) {
            ThreadManager.getInstance().runOnEventThreadDelayed(r, 0);
        }
    }
}
