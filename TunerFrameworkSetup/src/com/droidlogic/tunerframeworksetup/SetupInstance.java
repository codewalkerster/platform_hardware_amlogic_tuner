package com.droidlogic.tunerframeworksetup;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecInfo.CodecCapabilities;
import android.media.MediaCodecList;
import android.media.MediaCrypto;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;
import android.os.Bundle;

//https://source.android.com/devices/tv/tuner-framework
import android.media.tv.tuner.Tuner;
import android.media.tv.tuner.frontend.FrontendSettings;
import android.media.tv.tuner.frontend.FrontendStatus;
import android.media.tv.tuner.frontend.DvbtFrontendSettings;
import android.media.tv.tuner.frontend.DvbcFrontendSettings;
import android.media.tv.tuner.frontend.DvbsFrontendSettings;
import android.media.tv.tuner.frontend.DvbsCodeRate;
import android.media.tv.tuner.frontend.AtscFrontendSettings;
import android.media.tv.tuner.frontend.AnalogFrontendSettings;
import android.media.tv.tuner.frontend.IsdbtFrontendSettings;
import android.media.tv.tuner.frontend.OnTuneEventListener;
import android.media.tv.tuner.frontend.ScanCallback;
import android.media.tv.tuner.frontend.Atsc3PlpInfo;
import android.media.tv.tuner.filter.Filter;
import android.media.tv.tuner.filter.FilterCallback;
import android.media.tv.TvInputService;
import android.media.tv.tuner.filter.FilterEvent;
//import android.annotation.CallbackExecutor;
import android.media.tv.tuner.filter.AvSettings;
import android.media.tv.tuner.filter.Settings;
import android.media.tv.tuner.filter.TsFilterConfiguration;
import android.media.tv.tuner.filter.FilterConfiguration;
import android.media.tv.tuner.filter.MediaEvent;
import android.media.tv.tuner.filter.SectionSettingsWithTableInfo;
import android.media.tv.tuner.filter.SectionEvent;
import android.media.tv.tuner.dvr.OnPlaybackStatusChangedListener;
import android.media.tv.tuner.dvr.OnRecordStatusChangedListener;
import android.media.tv.tuner.dvr.DvrSettings;
import android.media.tv.tuner.dvr.DvrPlayback;
import android.media.MediaFormat;
import android.media.tv.tuner.filter.TsRecordEvent;
import android.media.tv.tuner.dvr.DvrRecorder;
import android.media.tv.tuner.filter.RecordSettings;
import android.media.tv.tuner.filter.SectionSettingsWithSectionBits;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;

import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;

import java.util.concurrent.Executor;
import java.io.File;
import java.io.RandomAccessFile;
import android.os.ParcelFileDescriptor;

import android.media.MediaCas;
import android.media.MediaCasException;
import android.media.tv.tuner.Descrambler;
import android.util.Base64;
import android.system.OsConstants;
import android.system.Os;
import android.system.ErrnoException;

import java.nio.channels.FileChannel;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.FileNotFoundException;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicInteger;

public class SetupInstance implements OnTuneEventListener,
    ScanCallback,
    OnPlaybackStatusChangedListener,
    OnRecordStatusChangedListener,
    MediaCodecPlayer.onLicenseReceiveListener,
    MediaCodec.OnFrameRenderedListener {

    private String TAG = SetupInstance.class.getSimpleName();

    public static final int DEFAULT_DVR_MQ_SIZE_MB = 100;
    public static final int SECTION_FILTER_BUFFER_SIZE = 32 * 1024;
    public static final int DEFAULT_DVR_READ_TS_PKT_NUM = 100;
    public static final int DEFAULT_DVR_READ_DURATION_MS = 2;
    public static final int MAX_DVR_READ_DURATION_MS = 4096;
    public static final int MIN_DECODER_BUFFER_FREE_THRESHOLD = 60;
    public static final int MAX_DECODER_BUFFER_FREE_THRESHOLD = 80;

    private int mInstance = -1;
    private SetupActivity mActivity = null;
    private boolean mPlayAudio = true;
    private SurfaceView mPlayerView = null;
    private Surface mSurface = null;

    private int mFrequency = 0;
    private int mSymbol = 0;
    private int mChannelId = 1;
    private String mLocalTsFileForPlayback = null;

    private Handler mUiHandler = null;
    private Handler mCasHandler = null;
    private HandlerThread mHandlerThread = null;
    private Handler mTaskHandler = null;

    private MediaCodecPlayer mMediaCodecPlayer = null;
    private AudioTrack mAudioTrack = null;
    private AudioFormat mAudioformat = null;
    private MediaCodec mMediaCodec = null;

    private Tuner mTuner = null;
    private TunerExecutor mExecutor = null;
    private DvrPlayback mDvrPlayback = null;
    private DvrRecorder mDvrRecorder = null;

    private Filter mPatSectionFilter = null;
    private Filter mPmtSectionFilter = null;
    private Filter mVideoFilter = null;
    private Filter mAudioFilter = null;
    private Filter mDvrFilter   = null;
    private Filter mPcrFilter = null;

    private ArrayList<ProgramInfo> programs = new ArrayList<ProgramInfo>();
    private int[] videoStreamTypes = {0x01, 0x02, 0x1b, 0x24};
    private int[] audioStreamTypes = {0x03, 0x04, 0x06, 0x0e, 0x0f, 0x011, 0x81, 0x87};
    private AlertDialog.Builder builder = null;
    private ProgramInfo mCurrentProgram = null;

    private File tmpFile  = null;
    private ParcelFileDescriptor fd = null;
    private AtomicBoolean mDvrReadStart = new AtomicBoolean(false);
    private AtomicBoolean mDvrReadThreadExit = new AtomicBoolean(true);

    /**
     * The event to indicate that the status of CAS system is changed by the removal or insertion of
     * physical CAS modules.
     */
    public static final int CAS_PLUGIN_STATUS_PHYSICAL_MODULE_CHANGED = 0;
    /**
     * The event to indicate that the number of CAS system's session is changed.
     */
    public static final int CAS_PLUGIN_STATUS_SESSION_NUMBER_CHANGED = 1;

    private static final byte[] EMPTY_PSSH = new byte[0];
    //private static final byte[] GOOGLE_TEST_PSSH = new byte[0];//Use for test streams no private data

    private static final String CERT_URL = "https://www.googleapis.com/certificateprovisioning/v1/devicecertificates/create?key=AIzaSyB-5OLKTx2iU5mko18DfdwK5611JIjbUhE";
    private static final String UAT_PROXY_URL = "https://proxy.uat.widevine.com/proxy?provider=widevine_test";
    public static String mCasLicenseServer = null;
    public static String mCustomerData = null;
    public static String mContentType = null;

    private Looper mLooper;
    private MediaCas mMediaCas = null;
    private MediaCas.EventListener mListener = null;
    private Descrambler mDescrambler = null;

    private boolean resolutionSupported = false;

    private final int PROVISION_COMPLETE_MSG = 0x01;
    private final int OPEN_SESSION_COMPLETE_MSG = 0x02;

    private final int LICENSED_REV_MSG = 0x11;
    private final int LICENSED_TIMEOUT_MSG = 0x12;

    private MediaCodecPlayer.onLicenseReceiveListener mLicenseLister = null;

    private static final int MAX_DSC_CHANNEL_NUM = 2;
    private static final int MAX_CAS_ECM_TID_NUM = 4;
    private static final int CAS_KEY_TOKEN_SIZE = 4;

    private PmtInfo mPmtInfo = null;
    private PatInfo mPatInfo = null;

    public static final int PAT_PID = 0x0;
    public static final int PAT_TID = 0x0;
    public static final int PMT_TID = 0x2;
    public static final int WVCAS_ECM_TID_128 = 0x80;
    public static final int WVCAS_ECM_TID_129 = 0x81;
    public static final int WVCAS_TEST_ECM_TID_176 = 0xb0;
    public static final int WVCAS_TEST_ECM_TID_177 = 0xb1;
    public static final int VIDEO_CHANNEL_INDEX = 0;
    public static final int AUDIO_CHANNEL_INDEX = 1;

    private File mTestLocalFile = null;
    private ParcelFileDescriptor mTestFd = null;
    private FileDescriptor mTestFileDescriptor = null;
    private int mProgramId = 1;

    private File mDumpFile = null;

    private AtomicBoolean mPlayerStart = new AtomicBoolean(false);
    private AtomicBoolean mNeedSeekToBegin = new AtomicBoolean(false);
    private AtomicBoolean mTuneStart = new AtomicBoolean(false);
    private AtomicBoolean mCasProvisioned = new AtomicBoolean(false);
    private AtomicBoolean mCasLicenseReceived = new AtomicBoolean(false);
    private AtomicInteger mCurCasIdx = new AtomicInteger(-1);
    private AtomicBoolean mHasSetPrivateData = new AtomicBoolean(false);

    private boolean mIsCasPlayback = false;
    private int mEcmPidNum = 0;
    private DscChannelInfo[] mEsCasInfo = new DscChannelInfo[MAX_DSC_CHANNEL_NUM];
    private AtomicInteger mCasSessionNum = new AtomicInteger(0);
    private String mVideoMimeType = MediaCodecPlayer.TEST_MIME_TYPE;
    private String mAudioMimeType = MediaCodecPlayer.AUDIO_MIME_TYPE;
    private int mAvSyncHwId = 0;
    private int mDemuxId = 0;
    public int mVideoFilterId = 0;
    public int mAudioFilterId = 0;
    private AtomicLong mDecoderFreeBufPercentage = new AtomicLong(100);
    private AtomicInteger mDvrPlaybackStatus = new AtomicInteger(0);

    private MediaFormat mVideoMediaFormat = null;
    private MediaFormat mAudioMediaFormat = null;

    private boolean mDebugFilter = false;
    private boolean mDebugTsSection = false;

    public static boolean mEnableLocalPlay = false;
    public static boolean mPassthroughMode = true;
    public static boolean mEnableFlowCtl = false;
    public static boolean mDumpVideoEs = false;
    public static int mDvrMQSize_MB = DEFAULT_DVR_MQ_SIZE_MB;
    public static long mDvrLowThreshold = DEFAULT_DVR_MQ_SIZE_MB * 1024 * 1024 * 2 / 10;
    public static long mDvrHighThreshold = DEFAULT_DVR_MQ_SIZE_MB * 1024 * 1024 * 8 / 10;
    private int mDvrReadTsPktNum = DEFAULT_DVR_READ_TS_PKT_NUM;
    private int mDvrReadDataDuration = DEFAULT_DVR_READ_DURATION_MS;
    public static boolean mEnableDvr = false;
    public static boolean mEnableExtendEcmTid = false;
    public static boolean mUseFixedOpVault = false;

    private boolean mEnableSmp = false;

    private static final String VIDEO_FILTER_ID_KEY = "vendor.tunerhal.video-filter-id";
    private static final String HW_AV_SYNC_ID_KEY = "vendor.tunerhal.hw-av-sync-id";//MediaFormat.KEY_HARDWARE_AV_SYNC_ID

    private String mScanMode = "Dvbt";

    public SetupInstance(int instance, SetupActivity activity, SurfaceView playerView) {
        TAG += "["+instance+"]";
        Log.d(TAG, "SetupInstance");
        mInstance = instance;
        mActivity = activity;
        mUiHandler = mActivity.getUiHandler();
        mPlayerView = playerView;
        mPlayerView.getHolder().addCallback(mSurfaceHolderCallback);

        if (mInstance == 0)
            mPlayAudio = true;
        else
            mPlayAudio = false;

        initHandler();
        Log.d(TAG, "New tuner executor");
        mExecutor = new TunerExecutor();

        try {
            if (mEnableDvr) {
                Log.d(TAG, "Create temp file");
                tmpFile = File.createTempFile("tuner", "dvr_test");
                fd = ParcelFileDescriptor.open(tmpFile, ParcelFileDescriptor.MODE_READ_WRITE);
            }
        } catch (Exception e) {
            Log.e(TAG, "message :" + e);
        }
    }

    public void pause() {
        Log.d(TAG, "pause");
        if (mPlayerStart.get()) {
            stopDvrPlayback();
            mPlayerView.setVisibility(View.GONE);
        }
    }

    public void destroy() {
    Log.d(TAG, "destroy");
        releaseInstance();
    }

    public void setLocalTsFileForPlayback(String file) {
        mLocalTsFileForPlayback = file;
    }

    public void setFrequency(int frequency) {
        mFrequency = frequency;
    }

    public void setProgramId(int id) {
        mProgramId = id;
    }

    public void setSymbol(int symbol) {
        mSymbol = symbol;
    }

    public void setEnableLocalPlay(boolean localPlay) {
        Log.d(TAG, "setEnableLocalPlay localPlay:" + localPlay);
        mEnableLocalPlay = localPlay;
    }

    private class TaskHandlerCallback implements Handler.Callback {

        @Override
        public boolean handleMessage(Message message) {
            Log.d(TAG, "TaskHandlerCallback handleMessage:" + message.what);
            boolean result = true;
            switch (message.what) {
                case TaskMsg.TASK_MSG_START_SEARCH:
                    searchStart();
                    break;
                case TaskMsg.TASK_MSG_STOP_SEARCH:
                    searchStop();
                    break;
                case TaskMsg.TASK_MSG_START_PLAY:
                    playStart(true);
                    break;
                case TaskMsg.TASK_MSG_STOP_PLAY:
                    stopDvrPlayback();
                    break;
                case TaskMsg.TASK_MSG_PULL_SECTION:
                    startSectionFilter(message.arg1, message.arg2);
                    break;
                case TaskMsg.TASK_MSG_STOP_SECTION:
                    searchStop();
                    mTaskHandler.sendEmptyMessageDelayed(TaskMsg.TASK_MSG_SHOW_CHNLIST, 200);
                    break;
                case TaskMsg.TASK_MSG_SHOW_CHNLIST:
                    showChannelList();
                    break;
                case TaskMsg.TASK_MSG_LOCAL_PLAY_START:
                    try {
                        startDvrPlayback();
                    } catch (Exception e) {
                        Log.e(TAG, "message:" + e);
                        e.printStackTrace();
                    }
                    break;
                case TaskMsg.TASK_MSG_LOCAL_PLAY_STOP:
                    stopDvrPlayback();
                    break;
                default:
                    result = false;
                    break;
            }
            return result;
        }
    }

    private void initHandler() {
        Log.d(TAG, "Create mTaskHandler with mHandlerThread and TaskHandlerCallback");
        mHandlerThread = new HandlerThread("task");
        mHandlerThread.start();
        mTaskHandler = new TaskHandler(mHandlerThread.getLooper(), new TaskHandlerCallback());
    }

    public Handler getTaskHandler() {
        return mTaskHandler;
    }

    private void releaseInstance() {
        Log.d(TAG, "Remove callback for handlers and quit handler thread");
        if (mTaskHandler != null) {
            mTaskHandler.removeCallbacksAndMessages(null);
            mTaskHandler = null;
        }
        if (mCasHandler != null) {
            mCasHandler.removeCallbacksAndMessages(null);
            mCasHandler = null;
        }

        if (mHandlerThread != null) {
            mHandlerThread.quitSafely();
            mHandlerThread = null;
        }
        mPlayerView.getHolder().removeCallback(mSurfaceHolderCallback);
        mPlayerView = null;
        mSurface = null;
        mSurfaceHolderCallback = null;
        if (tmpFile != null)
            tmpFile.delete();
        mActivity = null;
    }

    public void setLicenseListener(MediaCodecPlayer.onLicenseReceiveListener l) {
        mLicenseLister = l;
    }

    private byte[] sendProvisionRequest(byte[] data) throws Exception {
        Exception ex = null;
        Post.Response response = null;
        try {
            String uri = CERT_URL + "&signedRequest=" + (new String(data, "UTF-8"));
            Log.i(TAG, "Send provision request: URI=" + uri);
            final Post post = new Post(uri, null);
            post.setProperty("Connection", "close");
            post.setProperty("Content-Length", "0");

            response = post.send();
            if (response.code != 200) {
                ex = new Exception("Provision server returned HTTP error code " + response.code);
            }
            if (response.body == null) {
                ex = new Exception("No provision response!");
            }
            return response.body;
        } catch (Exception e) {
            ex = e;
        }
        if (ex != null) throw ex;
        return null;
    }

    private byte[] sendLicenseRequest(byte[] data) throws Exception {
        Exception ex = null;
        Post.Response response = null;
        mCasLicenseServer = Utils.getPropString("getprop " + DBGProp.WVCAS_PROP_LICSERVER);
        mCustomerData = Utils.getPropString("getprop " + DBGProp.WVCAS_PROP_CUSTOMER_DATA);
        mContentType = Utils.getPropString("getprop " + DBGProp.WVCAS_PROP_CONTENT_TYPE);
        if (mCasLicenseServer == null)
            mCasLicenseServer = UAT_PROXY_URL;
        if (mCasLicenseServer.equals(UAT_PROXY_URL)) {
            Log.d(TAG, "Use google proxy license server");
            mCustomerData = null;
            mContentType = null;
        }
        try {
            Log.d(TAG, "License Request: URI=" + mCasLicenseServer + ", Body="
                    + Base64.encodeToString(data, Base64.NO_WRAP));
            final Post post = new Post(mCasLicenseServer, data);
            post.setProperty("User-Agent", "Widevine CDM v1.0");
            post.setProperty("Connection", "close");
            if (mCustomerData != null)
                post.setProperty("dt-custom-data", "eyJtZXJjaGFudCI6Ind2X2NhcyIsInVzZXJJZCI6InB1cmNoYXNlIiwic2Vzc2lvbklkIjoicDEifQ==");

            response = post.send();
            if (response.code != 200) {
                ex = new Exception("License server returned HTTP error code " + response.code);
            }
            if (response.body == null) {
                ex = new Exception("No license response!");
            }
            return response.body;
        } catch (Exception e) {
            ex = e;
        }
        if (ex != null) throw ex;
        return null;
    }

    @Override
    public void onLicenseReceive() {
        mCasLicenseReceived.compareAndSet(false, true);
        Log.d(TAG, "License received");
    }

    private MediaCas startMediaCas() {
        Log.d(TAG, "Create MediaCas mCaSystemId:" + mPmtInfo.mCaSystemId);
        try {
            if (mMediaCas == null)
                mMediaCas = new MediaCas(mPmtInfo.mCaSystemId);
        } catch (MediaCasException e) {
            Log.e(TAG, "MediaCasException:" + Log.getStackTraceString(e));
        }

        mListener = new MediaCas.EventListener() {
            @Override
            public void onEvent(MediaCas mMediaCas, int event, int arg, byte[] data) {
                String data_str = new String(data);
                Log.d(TAG, "MediaCas event id: " + event);
                Message message = Message.obtain();
                switch (event) {
                    case WvCasEventId.WVCAS_INDIVIDUALIZATION_REQUEST :
                        Log.d(TAG, "WVCAS_INDIVIDUALIZATION_REQUEST");
                        try {
                            byte[] response = sendProvisionRequest(data);
                            mMediaCas.sendEvent(WvCasEventId.WVCAS_INDIVIDUALIZATION_RESPONSE, 0, response);
                            //TimeUnit.SECONDS.sleep(2);
                        } catch (Exception ex) {
                            Log.e(TAG, "Provision Exception: " + ex.toString());
                        }
                        break;
                    case WvCasEventId.WVCAS_LICENSE_REQUEST :
                    case WvCasEventId.WVCAS_LICENSE_RENEWAL_REQUEST :
                        try {
                            byte[] response = sendLicenseRequest(data);
                            if (response == null) {
                                Log.e(TAG, "License response is null!");
                                break;
                            } else {
                                Log.d(TAG, "License Response: " + Base64.encodeToString(response, Base64.NO_WRAP));
                                mMediaCas.sendEvent(event == WvCasEventId.WVCAS_LICENSE_REQUEST ?
                                    WvCasEventId.WVCAS_LICENSE_RESPONSE : WvCasEventId.WVCAS_LICENSE_RENEWAL_RESPONSE, 0, response);
                            }
                        } catch (Exception ex) {
                            Log.e(TAG, "License Request Exception: " + ex.toString());
                        }
                        break;
                    case WvCasEventId.WVCAS_INDIVIDUALIZATION_COMPLETE :
                        Log.d(TAG, "WVCAS_INDIVIDUALIZATION_COMPLETE");
                        mCasProvisioned.compareAndSet(false, true);
                        mCasHandler.removeMessages(PROVISION_COMPLETE_MSG);
                        message.what = PROVISION_COMPLETE_MSG;
                        mCasHandler.sendMessage(message);
                        break;
                    case WvCasEventId.WVCAS_SESSION_ID :
                        message.arg1 = mCurCasIdx.get();
                        Log.d(TAG, "WVCAS_SESSION_ID " + "mCurCasIdx:" +  message.arg1 + " arg:" + arg + " data:" + data_str);
                        mEsCasInfo[message.arg1].mCasSessionId = arg;
                        mCasSessionNum.incrementAndGet();
                        Log.d(TAG, "mCasSessionNum: " + mCasSessionNum.get());
                        mCasHandler.removeMessages(OPEN_SESSION_COMPLETE_MSG);
                        message.what = OPEN_SESSION_COMPLETE_MSG;
                        mCasHandler.sendMessage(message);
                        break;
                    case WvCasEventId.WVCAS_LICENSE_CAS_READY :
                    case WvCasEventId.WVCAS_LICENSE_CAS_RENEWAL_READY :
                        Log.d(TAG, "License Ready " + "arg:" + arg + " data:" + data_str);
                        mCasHandler.removeMessages(LICENSED_REV_MSG);
                        message.what = LICENSED_REV_MSG;
                        mCasHandler.sendMessage(message);
                        break;
                    case WvCasEventId.WVCAS_LICENSE_RENEWAL_URL :
                    case WvCasEventId.WVCAS_LICENSE_REMOVED :
                    case WvCasEventId.WVCAS_LICENSE_NEW_EXPIRY_TIME :
                    case WvCasEventId.WVCAS_UNIQUE_ID :
                        Log.d(TAG, "Event Info " + "arg:" + arg + " data:" + data_str);
                        break;
                    case WvCasEventId.WVCAS_ERROR :
                        Log.e(TAG, "WVCAS ERROR!");
                        break;
                    default:
                        Log.e(TAG, "MediaCas event: Not supported: " + event);
                        break;
                }
                return;
            }

            @Override
            public void onSessionEvent(MediaCas mMediaCas, MediaCas.Session mCasSession, int event, int arg, byte[] data) {
                String data_str = new String(data);
                Log.d(TAG, "MediaCas session event id: " + event + " mCasSession:" + mCasSession
                    + " arg:" + arg + " data:" + data_str);
                if (event == WvCasEventId.WVCAS_ACCESS_DENIED_BY_PARENTAL_CONTROL) {
                    Log.d(TAG, "WVCAS_ACCESS_DENIED_BY_PARENTAL_CONTROL");
                } else if (event == WvCasEventId.WVCAS_AGE_RESTRICTION_UPDATED) {
                    Log.d(TAG, "WVCAS_AGE_RESTRICTION_UPDATED " + data[0]);
                    try {
                        mCasSession.sendSessionEvent(WvCasEventId.WVCAS_SET_PARENTAL_CONTROL_AGE, 0, data);
                    } catch (Exception ex) {
                        Log.e(TAG, "License Renewal Request: Exception: " + ex.toString());
                    }
                } else if (event == WvCasEventId.WVCAS_ERROR) {
                    Log.e(TAG, "WVCAS_ERROR");
                } else {
                    Log.e(TAG, "MediaCas session event not supported: " + event);
                }
            }

            @Override
            public void onPluginStatusUpdate(MediaCas mMediaCas, int status, int arg) {
                if (status == CAS_PLUGIN_STATUS_PHYSICAL_MODULE_CHANGED) {
                    Log.d(TAG, "WVCAS_PLUGIN_STATUS_PHYSICAL_MODULE_CHANGED " + "status:" + status + "arg:" + arg);
                } else if (status == CAS_PLUGIN_STATUS_SESSION_NUMBER_CHANGED) {
                    Log.d(TAG, "WVCAS_PLUGIN_STATUS_SESSION_NUMBER_CHANGED " + "status:" + status + "arg:" + arg);
                } else {
                    Log.e(TAG, "MediaCas status not supported: " + status);
                }
            }

            @Override
            /**
             * Notify the listener that the session resources was lost.
             *
             * @param mediaCas the MediaCas object to receive this event.
             */
            public void onResourceLost(MediaCas mMediaCas) {
                Log.w(TAG, "Received Cas Resource Reclaim event");
                mMediaCas.close();
            }
        };
        mMediaCas.setEventListener(mListener, null);
        mCasHandler = new Handler() {
            public void handleMessage(Message msg) {
                int mCasIdx = -1;
                switch (msg.what) {
                    case PROVISION_COMPLETE_MSG:
                        Log.d(TAG, "PROVISION_COMPLETE_MSG");
                        for (int i = 0; i < MAX_DSC_CHANNEL_NUM; i++) {
                            int casIdx = i;
                            int ecmPid = mEsCasInfo[casIdx].getEcmPid();
                            Log.d(TAG, "casIdx:" + casIdx + " ecmPid:0x" + Integer.toHexString(ecmPid));
                            if (ecmPid != 0x1fff) {
                                mCurCasIdx.getAndSet(casIdx);
                                try {
                                    mEsCasInfo[casIdx].mCasSession = mMediaCas.openSession(mEsCasInfo[casIdx].mSessionUsage, mEsCasInfo[casIdx].mScramblingMode);
                                } catch (MediaCasException e) {
                                  Log.e(TAG, "MediaCasException:" + Log.getStackTraceString(e));
                                }
                                Log.d(TAG, "Open cas session casIdx:" + casIdx);
                                startEcmSectionFilter(casIdx, ecmPid, WVCAS_ECM_TID_128, 0);
                                startEcmSectionFilter(casIdx, ecmPid, WVCAS_ECM_TID_129, 1);
                                if (mEnableExtendEcmTid) {
                                    startEcmSectionFilter(casIdx, ecmPid, WVCAS_TEST_ECM_TID_176, 2);
                                    startEcmSectionFilter(casIdx, ecmPid, WVCAS_TEST_ECM_TID_177, 3);
                                }
                            }
                        }

                        break;
                    case OPEN_SESSION_COMPLETE_MSG:
                        mCasIdx = msg.arg1;
                        Log.d(TAG, "OPEN_SESSION_COMPLETE_MSG mCasIdx: " + mCasIdx);
                        mEsCasInfo[mCasIdx].mSessionOpened.compareAndSet(false, true);
                        try {
                            if (mPmtInfo.mPrivateData != null && !mHasSetPrivateData.get()) {
                                Log.i(TAG, "setPrivateData");
                                mEsCasInfo[mCasIdx].mCasSession.setPrivateData(mPmtInfo.mPrivateData);
                                mHasSetPrivateData.compareAndSet(false, true);
                            }
                            break;
                        } catch (MediaCasException e) {
                                Log.e(TAG, "mHandler:exception:" + Log.getStackTraceString(e));
                        }
                    case LICENSED_REV_MSG:
                        if (mLicenseLister != null) {
                            mLicenseLister.onLicenseReceive();
                        }
                        break;
                    case LICENSED_TIMEOUT_MSG:
                        Log.e(TAG, "mHandler:Get license timeout!");
                        break;
                    default:
                        Log.e(TAG, "mHandler:Unkonwn msg!");
                        break;
                }
            }
        };

        new Thread() {
            @Override
            public void run() {
                if (mMediaCas == null) {
                    Log.e(TAG, "mMediaCas is null!");
                    return;
                }
                // Set up a looper to handle events
                Looper.prepare();
                // Save the looper so that we can terminate this thread
                // after we are done with it.
                mLooper = Looper.myLooper();
                try {
                    if (mPmtInfo.mPrivateData != null) {
                        Log.d(TAG, "MediaCas provision with empty pssh");
                        mMediaCas.provision((new String(EMPTY_PSSH, "UTF-8")));
                    } else {
                        Log.w(TAG, "Widevine cas stream no private data!");
                        //mMediaCas.provision((new String(GOOGLE_TEST_PSSH, "UTF-8")));
                    }
                } catch (MediaCasException e) {
                    Log.e(TAG, "startMediaCas MediaCasException: " + e.getMessage());
                    return;
                } catch (Exception e) {
                    Log.e(TAG, "startMediaCas Exception: " + e.getMessage());
                    if (e.getMessage() == null) {
                        Log.d(TAG, "startMediaCas StackTrace: " + Log.getStackTraceString(e));
                    }
                    return;
                }
                Looper.loop();  // Blocks forever until Looper.quit() is called.
            }
        }.start();

        return mMediaCas;
    }

    private boolean isResolutionSupported(String mime, String[] features,
            int videoWidth, int videoHeight) {
        MediaFormat format = MediaFormat.createVideoFormat(mime, videoWidth, videoHeight);
        for (String feature: features) {
            format.setFeatureEnabled(feature, true);
        }
        MediaCodecList mcl = new MediaCodecList(MediaCodecList.ALL_CODECS);
        if (mcl.findDecoderForFormat(format) == null) {
            Log.e(TAG, "could not find codec for " + format);
            return false;
        }
        return true;
    }

    private boolean isNagraCasId(int caSysId) {
        if ((caSysId >= 0x18B0 && caSysId <= 0x18B5) || caSysId == 0x18B8 || caSysId == 0x1865 || caSysId == 0x1870 || caSysId == 0x1871)
            return true;
        else
            return false;
    }

    private boolean isWvCasId(int caSysId) {
        if (caSysId == 0x4AD4 || (caSysId >= 0x56C0 && caSysId <= 0x56C9))
            return true;
        else
            return false;
    }

    private boolean setUpCasPlayback(int caSysId) throws Exception {
        Log.i(TAG, "setUpCasPlayback caSysId:0x" + Integer.toHexString(caSysId));
        if (mDescrambler == null) {
            if (mTuner != null)
                mDescrambler = mTuner.openDescrambler();
            if (mDescrambler == null) {
                Log.d(TAG, "openDescrambler failed!");
                return false;
            }
            Log.d(TAG, "openDescrambler ok");
        }
        if (caSysId == 0x4AD4)
            return setUpWVCasPlayback(caSysId);
        else if (isNagraCasId(caSysId))
            return setUpNagraCasPlayback(caSysId);
        else
            return false;
    }

    private boolean setUpWVCasPlayback(int caSysId) throws Exception {
        Log.i(TAG, "setUpWVCasPlayback");
        if (!MediaCas.isSystemIdSupported(mPmtInfo.mCaSystemId)) {
            Log.e(TAG, "mCaSystemId is not supported! " + mPmtInfo.mCaSystemId);
            return false;
        }
        if (startMediaCas() == null)
            return false;
        setLicenseListener(SetupInstance.this);
        Log.i(TAG, "setUpWVCasPlayback mMediaCas: "  + mMediaCas);
        return true;
    }

    private boolean setUpNagraCasPlayback(int caSysId) throws Exception {
        Log.i(TAG, "setUpNagraCasPlayback");

        return false;
    }


    private SurfaceHolder.Callback mSurfaceHolderCallback = new SurfaceHolder.Callback() {
        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            Log.d(TAG, "surfaceChanged(holder=" + holder + ", format=" + format + ", width="
                    + width + ", height=" + height + ")");
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            Log.d(TAG, "surfaceCreated");
            mSurface = holder.getSurface();
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            Log.d(TAG, "surfaceDestroyed");
            mSurface = null;
        }
    };

    private class TaskHandler extends Handler {
        public TaskHandler(Looper looper, Callback callback) {
            super(looper, callback);
        }
    }

    private void showChannelList() {
        Log.d(TAG, "showChannelList");

        ArrayList<String> progStrs = new ArrayList<String>();
        for (PmtStreamInfo prg: mPmtInfo.mPmtStreams) {
            progStrs.add("" + mPatInfo.mPrograms.get(0).mPmtPid + ": esPid[" + prg.mEsPid + "] isVideo[" + prg.mIsVideo + "]");
        }
        String[] items = (String[])progStrs.toArray(new String[progStrs.size()]);
        builder = new AlertDialog.Builder(mActivity).setIcon(R.mipmap.ic_launcher)
                  .setTitle("Channels")
                  .setItems(items, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialogInterface, int i) {
                        Log.d(TAG, "Close channel list window");
                        if (!mEnableLocalPlay) {
                            if (mEsCasInfo[VIDEO_CHANNEL_INDEX] != null) {
                                 mVideoFilter = openVideoFilter(mEsCasInfo[VIDEO_CHANNEL_INDEX].mEsPid);
                             }
                             if (mEsCasInfo[AUDIO_CHANNEL_INDEX] != null) {
                                 mAudioFilter = openAudioFilter(mEsCasInfo[AUDIO_CHANNEL_INDEX].mEsPid);
                             }
                             if (mPassthroughMode) {
                                 passthroughSetup();
                             } else {
                                 //For non-passthrough clear playback, start av filter in tuner hal
                                 if (mVideoFilter != null) {
                                     mVideoFilter.start();
                                     if (mEnableDvr) {                                     //start dvr recorder
                                         try {
                                             Log.d(TAG, "start dvr recorder");
                                             startDvrRecorder(mEsCasInfo[VIDEO_CHANNEL_INDEX].mEsPid);
                                         } catch (Exception e) {
                                             Log.e(TAG, "message"  + e);
                                         }
                                     }
                                 }
                                 if (mAudioFilter != null) {
                                     //mAudioFilter.start();
                                 }
                             }
                            playStart(!mEnableLocalPlay);
                            if (mIsCasPlayback) {
                                //For non-passthrough/passthrough cas playback
                                //Create media cas instance
                                //Provision and request license
                                //Start ecm section filter
                                try{
                                    if (setUpCasPlayback(mPmtInfo.mCaSystemId) == false) {
                                        Log.e(TAG, "setUpCasPlayback failed!");
                                        return;
                                    }
                                } catch (Exception e) {
                                    Log.e(TAG, "setUpCasPlayback Exception: " + e.getMessage());
                                    return;
                                }
                            }
                        }
                    }
                });
        builder.create().show();
    }

    private void searchStart() {
        FrontendSettings feSettings = null;
        int freqMhz = 0;
        try {
            freqMhz = mFrequency;
            mProgramId = mChannelId;
            Log.i(TAG, "mProgramId:" + mProgramId);
        } catch (Exception e) {
        }
        if (freqMhz == 0) {
            Log.w(TAG, "No frequency");
            mUiHandler.sendMessage(mUiHandler.obtainMessage(SetupActivity.UI_MSG_STATUS, "Frequency invalid"));
            return;
        }
        switch (mScanMode) {
            case "Dvbt":
            case "Dvbt2": {
                feSettings = DvbtFrontendSettings
                .builder()
                .setFrequency(freqMhz  * 1000000)
                .setStandard("Dvbt2".equals(mScanMode) ?
                                DvbtFrontendSettings.STANDARD_T2 : DvbtFrontendSettings.STANDARD_T)
                .setPlpMode(DvbtFrontendSettings.PLP_MODE_AUTO)
                .build();
            }
            break;
            case "Dvbc": {
                int symbol = 6900;
                try {
                    symbol = mSymbol;
                } catch (Exception e) {
                }
                feSettings = DvbcFrontendSettings
                .builder()
                .setFrequency(freqMhz  * 1000000)
                .setAnnex(DvbcFrontendSettings.ANNEX_A)
                .setSymbolRate(symbol*1000)
                .build();
            }
            break;
            case "Dvbs":
            case "Dvbs2": {
                int symbol = 27500;
                try {
                    symbol = mSymbol;
                } catch (Exception e) {
                }
                DvbsCodeRate codeRate = DvbsCodeRate.builder()
                    .setInnerFec(FrontendSettings.FEC_AUTO)
                    .setLinear(false)
                    .setShortFrameEnabled(true)
                    .setBitsPer1000Symbol(0)
                    .build();
                feSettings = DvbsFrontendSettings
                .builder()
                .setFrequency(freqMhz  * 1000000)
                .setSymbolRate(symbol*1000)
                .setStandard("Dvbs2".equals(mScanMode) ?
                                DvbsFrontendSettings.STANDARD_S2 : DvbsFrontendSettings.STANDARD_S)
                .setCodeRate(codeRate)
                .build();
            }
            break;
            case "Analog": {
                feSettings = AnalogFrontendSettings
                .builder()
                .setFrequency(freqMhz  * 1000000)
                .setSignalType(AnalogFrontendSettings.SIGNAL_TYPE_NTSC)
                .setSifStandard(AnalogFrontendSettings.SIF_M)
                .build();
            }
            break;
            case "Atsc": {
                feSettings = AtscFrontendSettings
                .builder()
                .setFrequency(freqMhz  * 1000000)
                .setModulation(AtscFrontendSettings.MODULATION_MOD_8VSB)
                .build();
            }
            break;
            case "Isdbt": {
                feSettings = IsdbtFrontendSettings
                .builder()
                .setFrequency(freqMhz  * 1000000)
                .setModulation(IsdbtFrontendSettings.MODULATION_AUTO)
                .setBandwidth(IsdbtFrontendSettings.BANDWIDTH_6MHZ)
                .build();
            }
            break;
        }
        if (feSettings != null) {
            boolean createTuner = false;
            if (mTuner == null) {
                createTuner = true;
            } else {
                searchStop();
                if (mPatInfo != null) {
                    mPatInfo.mPrograms.clear();
                }
                if (mPmtInfo != null) {
                    mPmtInfo.mPmtStreams.clear();
                }
                programs.clear();
                if ((mTuner.getFrontendInfo() == null)
                    || (mTuner.getFrontendInfo().getType() != feSettings.getType())) {
                    mTuner.setOnTuneEventListener(mExecutor, null);
                    mTuner.close();
                    if (mDvrPlayback != null) {
                        mDvrPlayback.stop();
                        mDvrPlayback.close();
                        mDvrPlayback = null;
                        mDvrFilter = null;
                    }
                    mPatSectionFilter = null;
                    mPmtSectionFilter = null;
                    mVideoFilter = null;
                    mAudioFilter = null;
                }
            }
            if (createTuner) {
                if (mTuner == null) {
                    mTuner = new Tuner(mActivity.getApplicationContext(),
                                       null/*tvInputSessionId*/,
                                       200/*PRIORITY_HINT_USE_CASE_TYPE_SCAN*/);
                    Log.d(TAG, "mTuner created:" + mTuner);
                    mTuner.setOnTuneEventListener(mExecutor, this);
                }
            }
            mTuner.scan(feSettings, Tuner.SCAN_TYPE_AUTO, mExecutor, this);
        } else {
            Log.e(TAG, "feSettings is null");
            mUiHandler.sendMessage(mUiHandler.obtainMessage(SetupActivity.UI_MSG_STATUS, "Error with fe settings"));
        }
        Log.d(TAG, "searchStart");
    }

    private void searchStop() {
        if (mPatSectionFilter != null) {
            mPatSectionFilter.stop();
        }
        if (mPmtSectionFilter != null) {
            mPmtSectionFilter.stop();
        }

        if (mTuner != null && !mEnableLocalPlay) {
            mTuner.cancelScanning();
        }
        Log.d(TAG, "searchStop");
    }

    private void playStart(boolean bTuner) {
        Log.d(TAG, "playStart, new MediaCodecPlayer and set av media format");
        if (mMediaCodecPlayer == null) {
            if (isNagraCasId(mPmtInfo.mCaSystemId))
                mMediaCodecPlayer = new MediaCodecPlayer(mActivity, mSurface, MediaCodecPlayer.PLAYER_MODE_TUNER, mVideoMimeType, null, mPassthroughMode, mEnableSmp);
            else
                mMediaCodecPlayer = new MediaCodecPlayer(mActivity, mSurface, MediaCodecPlayer.PLAYER_MODE_TUNER, mVideoMimeType, null, mPassthroughMode, mIsCasPlayback);
        }
        if (mMediaCodecPlayer.isStarted()) {
            Log.d(TAG, "playStart already");
            return;
        }

        if (MediaCodecPlayer.PLAYER_MODE_TUNER.equals(mMediaCodecPlayer.getPlayerMode())) {
            if (mVideoMediaFormat == null)
                mVideoMediaFormat = MediaFormat.createVideoFormat(MediaCodecPlayer.TEST_MIME_TYPE, 720, 576);
            if (mAudioMediaFormat == null)
                mAudioMediaFormat = MediaFormat.createAudioFormat(MediaCodecPlayer.AUDIO_MIME_TYPE, MediaCodecPlayer.AUDIO_SAMPLE_RATE, MediaCodecPlayer.AUDIO_CHANNEL_COUNT);
            mMediaCodecPlayer.setVideoMediaFormat(mVideoMediaFormat);
            mMediaCodecPlayer.setAudioMediaFormat(mAudioMediaFormat);
            mMediaCodecPlayer.setFrameRenderedListener(SetupInstance.this);
            //please use tuner data to mMediaCodecPlayer.WriteInputData(mBlocks, timestampUs, 0, written);
           // if (initSource()) {
            //    sendData();
            //}
        }

        Log.d(TAG, "MediaCodecPlayer start");
        mMediaCodecPlayer.startPlayer();
        CreateAudioTrack();

        if (bTuner)
            startTuner();
        mPlayerStart.compareAndSet(false, true);
    }

//TASK_MSG_STOP_PLAY
    private void playStop() {
        Log.d(TAG, "playStop");
        if (mDvrPlayback != null) {
            if (mVideoFilter != null)
                mDvrPlayback.detachFilter(mVideoFilter);
            if (mAudioFilter != null)
                mDvrPlayback.detachFilter(mAudioFilter);
        }

        if (mMediaCodecPlayer != null) {
            mMediaCodecPlayer.stopPlayer();
            if (MediaCodecPlayer.PLAYER_MODE_TUNER.equals(mMediaCodecPlayer.getPlayerMode())) {
                //comment it when using tuner data
                //clearResource();
            }
        }
        if (mVideoFilter != null) {
            mVideoFilter.stop();
        }
        if (mAudioFilter != null) {
            mAudioFilter.stop();
        }
        stopTuner();
    }

    private FilterCallback mfilterCallback = new FilterCallback() {
        @Override
        public void onFilterEvent(Filter filter, FilterEvent[] events) {
            if (mDebugFilter)
                Log.d(TAG, "onFilterEvent" + " filter id:" + filter.getId());
            for (FilterEvent event: events) {
                if (event instanceof MediaEvent) {
                    MediaEvent mediaEvent = (MediaEvent) event;
                    Log.d(TAG, "MediaEvent length =" +  mediaEvent.getDataLength() + " \n\" + MediaEvent audio =" + mediaEvent.getExtraMetaData());
                    LinearInputBlock1 mLinearInputBlock = null;

                    if (mMediaCodecPlayer != null) {
                        if (mLinearInputBlock == null) {
                            mLinearInputBlock = new LinearInputBlock1();
                            Log.d(TAG, "New LinearInputBlock1");
                        }
                        mLinearInputBlock.block = mediaEvent.getLinearBlock();
                        if (mediaEvent.getExtraMetaData() != null) {
                            mMediaCodecPlayer.WriteTunerInputAudioData(mLinearInputBlock, 0, 0, (int)mediaEvent.getDataLength());
                        } else {
                            if (mDumpVideoEs) {
                                ByteBuffer buffer = mLinearInputBlock.block.map();
                                int capacity = buffer.capacity();
                                int remainling = buffer.remaining();
                                int position = buffer.position();
                                //Log.d(TAG, "print capacity " + capacity + ", position = " + position + ", remainling = " + remainling);
                                if (mDumpFile == null)
                                    mDumpFile = new File("/data/dump/dump.es");
                                try {
                                  FileChannel wChannel = new FileOutputStream(mDumpFile, true).getChannel();
                                  wChannel.write(buffer);
                                  wChannel.close();
                                } catch (IOException e) {
                                }
                            }

                            mMediaCodecPlayer.WriteTunerInputVideoData(mLinearInputBlock, 0, 0, (int)mediaEvent.getDataLength());
                        }
                    }
                } else if (event instanceof SectionEvent) {
                    SectionEvent sectionEvent = (SectionEvent) event;
                    if (mDebugTsSection)
                        Log.d(TAG, "Receive section data, size=" + sectionEvent.getDataLength() + "version =" + sectionEvent.getVersion() + "sectionNum =" + sectionEvent.getSectionNumber());
                    byte[] data = new byte[sectionEvent.getDataLength()];
                    if (mEnableLocalPlay && mDvrReadThreadExit.get() == true)
                        return;
                    if (filter != null) {
                        filter.read(data, 0, sectionEvent.getDataLength());
                        parseSectionData(data);
                    }
                } else if (event instanceof TsRecordEvent) {
                    TsRecordEvent tsRecEvent = (TsRecordEvent) event;
                    Log.d(TAG, "Receive tsRecord data, size=" + tsRecEvent.getDataLength());
                    long datalength = tsRecEvent.getDataLength();
                    mDvrRecorder.write(datalength);
                }
            }
        }

        @Override
        public void onFilterStatusChanged(Filter filter,        int status) {
            Log.d(TAG, "onFilterStatusChanged status->" + status);
        }
    };

    private FilterCallback mEcmFilterCallback = new FilterCallback() {
        @Override
        public void onFilterEvent(Filter filter, FilterEvent[] events) {
            int secLen = 0;
            byte[] data = null;
            SectionEvent sectionEvent = null;
            if (mDebugFilter)
                Log.d(TAG, "onEcmFilterEvent" + " filter id:" + filter.getId());

            for (FilterEvent event: events) {
                if (event instanceof SectionEvent) {
                    sectionEvent = (SectionEvent)event;
                } else {
                    Log.e(TAG, "Invalid filter event type!");
                    continue;
                }
            }
            if (sectionEvent == null) {
                Log.e(TAG, "sectionEvent is null!");
                return;
            }

            secLen = sectionEvent.getDataLength();
            if (mDebugFilter)
                Log.d(TAG, "Receive ecm section data size:" + secLen);
            if (secLen <= 0) {
                Log.e(TAG, "Invalid secLen:" + secLen);
                return;
            }
            data = new byte[secLen];
            if (mEnableLocalPlay && mDvrReadThreadExit.get() == true)
                return;
            if (filter != null) {
                filter.read(data, 0, secLen);
                parseEcmSectionData(data, filter.getId());
            }
        }

        @Override
        public void onFilterStatusChanged(Filter filter,        int status) {
            //Log.d(TAG, "Ecm onFilterStatusChanged status->" + status);//status 1
        }
    };

    public class CryptoMode{
        public static final int kInvalid = -1;
        public static final int kAesCBC = 0;
        public static final int kAesCTR = 1;
        public static final int kDvbCsa2 = 2;
        public static final int kDvbCsa3 = 3;
        public static final int kAesOFB = 4;
        public static final int kAesSCTE = 5;
    }

    public class WvCasEventId{
        private static final int WVCAS_INDIVIDUALIZATION_REQUEST = 1000;
        private static final int WVCAS_INDIVIDUALIZATION_RESPONSE = 1001;
        private static final int WVCAS_INDIVIDUALIZATION_COMPLETE = 1002;

        private static final int WVCAS_LICENSE_REQUEST = 2000;
        private static final int WVCAS_LICENSE_RESPONSE = 2001;
        private static final int WVCAS_ERROR_DEPRECATED = 2002;
        private static final int WVCAS_LICENSE_RENEWAL_REQUEST = 2003;
        private static final int WVCAS_LICENSE_RENEWAL_RESPONSE = 2004;
        private static final int WVCAS_LICENSE_RENEWAL_URL = 2005;
        private static final int WVCAS_LICENSE_CAS_READY = 2006;
        private static final int WVCAS_LICENSE_CAS_RENEWAL_READY = 2007;
        private static final int WVCAS_LICENSE_REMOVAL = 2008;
        private static final int WVCAS_LICENSE_REMOVED = 2009;
        private static final int WVCAS_ASSIGN_LICENSE_ID = 2010;
        private static final int WVCAS_LICENSE_ID_ASSIGNED = 2011;
        private static final int WVCAS_LICENSE_NEW_EXPIRY_TIME = 2012;
        private static final int WVCAS_MULTI_CONTENT_LICENSE_INFO = 2013;
        private static final int WVCAS_GROUP_LICENSE_INFO = 2014;
        private static final int WVCAS_LICENSE_ENTITLEMENT_PERIOD_UPDATE_REQUEST = 2015;
        private static final int WVCAS_LICENSE_ENTITLEMENT_PERIOD_UPDATE_RESPONSE = 2016;
        private static final int WVCAS_LICENSE_ENTITLEMENT_PERIOD_UPDATED = 2017;

        private static final int WVCAS_SESSION_ID   = 3000;
        private static final int WVCAS_SET_CAS_SOC_ID   = 3001;
        private static final int WVCAS_SET_CAS_SOC_DATA = 3002;

        private static final int WVCAS_UNIQUE_ID = 4000;
        private static final int WVCAS_QUERY_UNIQUE_ID = 4001;
        private static final int WVCAS_WV_CAS_PLUGIN_VERSION = 4002;
        private static final int WVCAS_QUERY_WV_CAS_PLUGIN_VERSION = 4003;

        private static final int WVCAS_ERROR = 5000;

        private static final int WVCAS_SET_PARENTAL_CONTROL_AGE = 6000;
        private static final int WVCAS_DEPRECATED_PARENTAL_CONTROL_AGE_UPDATED = 6001;
        private static final int WVCAS_ACCESS_DENIED_BY_PARENTAL_CONTROL = 6002;
        private static final int WVCAS_AGE_RESTRICTION_UPDATED = 6003;
        private static final int WVCAS_FINGERPRINTING_INFO = 6100;
        private static final int WVCAS_SESSION_FINGERPRINTING_INFO = 6101;
        private static final int WVCAS_SERVICE_BLOCKING_INFO = 6200;
        private static final int WVCAS_SESSION_SERVICE_BLOCKING_INFO = 6201;
    }

    public class EcmDescriptor {
        public byte[] mCaId = new byte[2];
        public byte[] mVersion = new byte[1];
        public byte[] mCipherRotationFlags = new byte[1];
        public byte[] mIVAgeFlags = new byte[1];

        public int getEcmDescriptorLen() {
            mEcmDescriptorLen = 0;
            mEcmDescriptorLen += mCaId.length;
            mEcmDescriptorLen += mVersion.length;
            mEcmDescriptorLen += mCipherRotationFlags.length;
            mEcmDescriptorLen += mIVAgeFlags.length;
            return mEcmDescriptorLen;
        }
        private int mEcmDescriptorLen;
    }

    // ECM constants
    private static final byte kSequenceCountMask = (byte)0xF8;      // Mode bits 3..7
    private static final byte kCryptoModeFlags = (byte)(0x3 << 1);  // Mode bits 1..2
    private static final byte kCryptoModeFlagsV2 = (byte)(0xF << 1);    // Mode bits 1..4
    private static final byte kRotationFlag = (byte)(0x1 << 0);     // Mode bit 0

    // Two values of the table_id field (0x80 and 0x81) are reserved for
    // transmission of ECM data. A change of these two table_id values signals
    // that a change of ECM contents has occurred.
    private static final byte kSectionHeader1 = (byte)0x80;
    private static final byte kSectionHeader2 = (byte)0x81;
    private static final int kSectionHeaderSize = 3;
    private static final int kSectionHeaderWithPointerSize = 4;
    private static final byte kPointerFieldZero = (byte)0x00;
    private static final int kMaxEcmSizeBytes = 184;
    private static final int kWidevineCasId = 0x4AD4;
    // New Widevine CAS IDs 0x56C0 to 0x56C9 (all inclusive).
    private static final int kWidevineNewCasIdLowerBound = 0x56C0;
    private static final int kWidevineNewCasIdUpperBound = 0x56C9;

    private int findEcmStartIndex(byte[] cas_ecm, int ecm_size) {
        Log.d(TAG, "findEcmStartIndex");
        if (ecm_size <= 0) {
            Log.e(TAG, "ecm_size:" + ecm_size);
            return -1;
        }
        // Case 1: Pointer field (always set to 0); section header; ECM.
        if (cas_ecm[0] == kPointerFieldZero) {
            Log.d(TAG, "Case 1:kSectionHeaderWithPointerSize:" + kSectionHeaderWithPointerSize);
            return kSectionHeaderWithPointerSize;
        }
        // Case 2: Section header (3 bytes), ECM.
        if (cas_ecm[0] == kSectionHeader1 || cas_ecm[0] == kSectionHeader2) {
            Log.d(TAG, "Case 2:kSectionHeaderSize:" + kSectionHeaderSize);
            return kSectionHeaderSize;
        }
        // Case 3: ECM.
        Log.d(TAG, "Case 3:findEcmStartIndex offest is 0");
        return 0;
    }

    public int getCasCryptoMode(byte[] cas_ecm, int ecm_size) {
        Log.d(TAG, "getCasCryptoMode");
        int mCryptoMode;
        //Detect and strip optional section header.
        int offset = findEcmStartIndex(cas_ecm, ecm_size);
        EcmDescriptor mEcmDescriptor = new EcmDescriptor();
        if (offset < 0 || (ecm_size - offset < mEcmDescriptor.getEcmDescriptorLen())) {
            return CryptoMode.kInvalid;
        }
        Log.d(TAG, "EcmDescriptor len:" + mEcmDescriptor.getEcmDescriptorLen());
        byte[] ecm = new byte[ecm_size - offset];
        System.arraycopy(cas_ecm, offset, ecm, 0, ecm_size - offset);
        //Reconfirm ecm data should start with valid Widevine CAS ID.
        //ecm byte[0]:4a byte[1]:d4 byte[2]:1 byte[3]:7 byte[4]:80
        int cas_id_val = ((((int)(ecm[0])) & 0x00ff) << 8) + (((int)ecm[1]) & 0xff);
        Log.d(TAG, "CAS ID:" + Integer.toHexString(cas_id_val));
        if (cas_id_val != kWidevineCasId &&
            (cas_id_val < kWidevineNewCasIdLowerBound ||
            cas_id_val > kWidevineNewCasIdUpperBound)) {
            Log.e(TAG, "Supported Widevine CAS IDs not found at the start of ECM. Found:0x" + Integer.toHexString(cas_id_val));
            return CryptoMode.kInvalid;
        }
        if ((ecm[2] & 0x000000ff) == 1) {
            Log.d(TAG, "Use kCryptoModeFlags");
            mCryptoMode = (int)((ecm[3] & kCryptoModeFlags) >> 1);
        } else {
            Log.d(TAG, "Use kCryptoModeFlagsV2");
            mCryptoMode = (int)((ecm[3] & kCryptoModeFlagsV2) >> 1);
        }
        Log.d(TAG, "mCryptoMode:" + mCryptoMode);

        return mCryptoMode;
    }

    public int getCasIdx(int filter_id) {
        int mCasIdx = -1;
        for (int idx = 0; idx < MAX_CAS_ECM_TID_NUM; idx++) {
            if (mEsCasInfo[VIDEO_CHANNEL_INDEX].mEcmSectionFilter[idx] != null) {
                if (filter_id == mEsCasInfo[VIDEO_CHANNEL_INDEX].mEcmSectionFilter[idx].getId()) {
                    mCasIdx = VIDEO_CHANNEL_INDEX;
                    break;
                }
            }
            if (mEsCasInfo[AUDIO_CHANNEL_INDEX].mEcmSectionFilter[idx] != null) {
                if (filter_id == mEsCasInfo[AUDIO_CHANNEL_INDEX].mEcmSectionFilter[idx].getId()) {
                    mCasIdx = AUDIO_CHANNEL_INDEX;
                    break;
                }
            }
        }
        return mCasIdx;
    }

    public void setupDescrambler(int mCasIdx) {
        if (mEsCasInfo[mCasIdx].mCasKeyToken != null)
            return;
        mEsCasInfo[mCasIdx].mCasKeyToken = new byte[4];
        mEsCasInfo[mCasIdx].mCasKeyToken[0] = (byte) (mEsCasInfo[mCasIdx].mCasSessionId & 0xff);
        mEsCasInfo[mCasIdx].mCasKeyToken[1] = (byte) (mEsCasInfo[mCasIdx].mCasSessionId >> 8 & 0xff);
        mEsCasInfo[mCasIdx].mCasKeyToken[2] = (byte) (mEsCasInfo[mCasIdx].mCasSessionId >> 16 & 0xff);
        mEsCasInfo[mCasIdx].mCasKeyToken[3] = (byte) (mEsCasInfo[mCasIdx].mCasSessionId >> 24 & 0xff);
        if (mEcmPidNum == 1) {
            mDescrambler.addPid(Descrambler.PID_TYPE_T, mEsCasInfo[VIDEO_CHANNEL_INDEX].mEsPid, mVideoFilter);
            mDescrambler.addPid(Descrambler.PID_TYPE_T, mEsCasInfo[AUDIO_CHANNEL_INDEX].mEsPid, mAudioFilter);
        } else {
            mDescrambler.addPid(Descrambler.PID_TYPE_T, mEsCasInfo[mCasIdx].mEsPid, mCasIdx == VIDEO_CHANNEL_INDEX ? mVideoFilter : mAudioFilter);
        }
        Log.d(TAG, "Set KeyToken:0x" + Integer.toHexString(mEsCasInfo[mCasIdx].mCasSessionId) + " mEcmPidNum:" + mEcmPidNum);
        mDescrambler.setKeyToken(mEsCasInfo[mCasIdx].mCasKeyToken);
    }

    private void parseEcmSectionData(byte[] data, int filter_id) {
        int mCasIdx = -1;
        int mTableId = (int)(data[0] & 0x000000ff);
        int mSectionLen = ((((int)(data[1]))&0x0003) << 8) + (((int)data[2]) & 0xff);
        //int mProgramId = ((((int)(data[3])) & 0x00ff) << 8) + (((int)data[4]) & 0xff);
        byte[] ecm_data = null;

        if (mSectionLen > 4096) {
            Log.d(TAG, "Invalid mSectionLen: " + mSectionLen);
            return;
        }

        if (mTableId != WVCAS_TEST_ECM_TID_176 && mTableId != WVCAS_TEST_ECM_TID_177 && mTableId != WVCAS_ECM_TID_128 && mTableId != WVCAS_ECM_TID_129) {
            Log.d(TAG, "Invalid mTableId: 0x" + Integer.toHexString(mTableId));
            return;
        }

        if (mDebugTsSection) {
            Log.d(TAG, "parseEcmSectionData mTableId:0x" + Integer.toHexString(mTableId) +" mSectionLen: " + mSectionLen);
        }

        mCasIdx = getCasIdx(filter_id);
        if (mCasIdx != VIDEO_CHANNEL_INDEX && mCasIdx != AUDIO_CHANNEL_INDEX)
            return;

        if (!mEsCasInfo[mCasIdx].mSessionOpened.get() || !mCasLicenseReceived.get()) {
            return;
        }

        if (isWvCasId(mPmtInfo.mCaSystemId)) {
            ecm_data = new byte[mSectionLen];
            System.arraycopy(data, 3, ecm_data, 0, mSectionLen);
        }

//        if (mEsCasInfo[mCasIdx].mGetCryptoMode == false) {
//            mEsCasInfo[mCasIdx].mCryptoMode = getCasCryptoMode(ecm_data, mSectionLen);
//            if (mEsCasInfo[mCasIdx].mCryptoMode == CryptoMode.kInvalid) {
//                Log.e(TAG, "getCasCryptoMode failed! mCasIdx: " + mCasIdx + " filter_id: " + filter_id);
//                return;
//            }
//            mEsCasInfo[mCasIdx].mGetCryptoMode = true;
//            if (mEsCasInfo[mCasIdx].mCryptoMode == CryptoMode.kAesCBC) {
//                mEsCasInfo[mCasIdx].mScramblingMode = MediaCas.SCRAMBLING_MODE_AES128;
//            } else if (mEsCasInfo[mCasIdx].mCryptoMode == CryptoMode.kDvbCsa2
//            || mEsCasInfo[mCasIdx].mCryptoMode == CryptoMode.kDvbCsa3) {
//                mEsCasInfo[mCasIdx].mScramblingMode = MediaCas.SCRAMBLING_MODE_DVB_CSA2;
//            } else if (mEsCasInfo[mCasIdx].mCryptoMode == CryptoMode.kAesSCTE) {
//                mEsCasInfo[mCasIdx].mScramblingMode = MediaCas.SCRAMBLING_MODE_AES_SCTE52;
//            }
//        }

        if (mEsCasInfo[mCasIdx].mCasKeyToken == null) {
            setupDescrambler(mCasIdx);
            try {
                Log.d(TAG, "Process first ecm");
                if (isWvCasId(mPmtInfo.mCaSystemId))
                    mEsCasInfo[mCasIdx].mCasSession.processEcm(ecm_data);
                else if (isNagraCasId(mPmtInfo.mCaSystemId))
                    Log.e(TAG, "Not implemented!");
                else
                    return;
            } catch (Exception ex) {
                Log.e(TAG, "mCasIdx: " + mCasIdx + " processEcm: Exception: " + ex.toString());
                return;
            }
            //Start AV Playback after processing the first ecm
            mNeedSeekToBegin.compareAndSet(false, true);
        }

        try {
            if (isWvCasId(mPmtInfo.mCaSystemId))
                mEsCasInfo[mCasIdx].mCasSession.processEcm(ecm_data);
            else if (isNagraCasId(mPmtInfo.mCaSystemId))
                Log.e(TAG, "Not implemented!");
            else
                return;
        } catch (Exception ex) {
            Log.e(TAG, "mCasIdx: " + mCasIdx + "processEcm: Exception: " + ex.toString());
            return;
        }
        ecm_data = null;

        return;
    }

    private void parseSectionHeader(SectionHeaderInfo section, byte[] data) {
        section.mTableId = data[0];
        section.mSectionSyntaxIndicator = data[1] >> 7;
        section.mZero = data[1] >> 6 & 0x1;
        section.mReserved_1 = data[1] >> 4 & 0x3;
        //section.mSectionLength = (data[1] & 0x0f) << 8 | data[2];
        section.mSectionLength = ((((int)(data[1])) & 0x03 << 8) & 0xff) + (((int)data[2]) & 0xff);
        section.mTransportStreamId = data[3] << 8 | data[4];
        section.mReserved_2 = data[5] >> 6;
        section.mVersionNumber = (data[5] & 0x3e) >> 1;
        section.mCurrentNextIndicator = (data[5] << 7) >> 7;
        section.mSectionNumber = data[6];
        section.mLastSectionNumber = data[7];

        return;
    }

    private void parsePATSection(PatInfo patInfo, byte[] data) {
        Log.i(TAG, "[parsePATSection]");
        parseSectionHeader(patInfo, data);
        if (mDebugTsSection) {
            Log.d(TAG, "parseSectionHeader for pat ok.");
            Log.d(TAG, "PAT mSectionLength is " + patInfo.mSectionLength);
        }
        int len = 3 + patInfo.mSectionLength;
        patInfo.mCRC_32 = (data[len - 4] & 0x000000ff) << 24
                        | (data[len - 3] & 0x000000ff) << 16
                        | (data[len - 2] & 0x000000ff) << 8
                        | (data[len - 1] & 0x000000ff);
        int pos = 8;
        for (; pos < data.length - 4; pos += 4)
        {
            if (pos > data.length - 4 - 4) {
                Log.e(TAG, "Broken PAT.");
                return;
            }
            int programNo = ((data[pos] & 0xff) << 8) | (data[pos + 1] & 0xff);
            int pmtPid = ((data[pos + 2] & 0x1f) << 8) | (data[pos + 3] & 0xff);

            patInfo.mReserved_3 = (data[pos + 2] & 0xff) >> 5;
            if (programNo == 0x00) {
                patInfo.mNetworkPid = pmtPid;
                if (patInfo.mNetworkPid != 0x0)
                    Log.d(TAG, "PAT mNetworkPid: " + patInfo.mNetworkPid);
                continue;
            }
            patInfo.mPrograms.add(new PatProgramInfo(programNo, pmtPid));
        }
        Log.d(TAG, "PAT program size: " + patInfo.mPrograms.size());
    }

    private void parsePMTSection(PmtInfo pmtInfo, byte[] data) {
        Log.i(TAG, "[parsePMTSection]");
        parseSectionHeader(pmtInfo, data);
        if (mDebugTsSection) {
            Log.d(TAG, "parseSectionHeader for pmt ok.");
            Log.d(TAG, "PMT mSectionLength is " + pmtInfo.mSectionLength);
        }
        pmtInfo.mReserved_3 = (data[8] & 0xff) >> 5;
        int pcrPid = ((data[8] & 0x1f) << 8) | (int)(data[9] & 0xff);
        //PCR_PID ((data[8] << 8) | data[9]) & 0x1FFF
        pmtInfo.mPcrPid = pcrPid;
        Log.d(TAG, "pcrPid: 0x" + Integer.toHexString(pmtInfo.mPcrPid));

        pmtInfo.mReserved_4 = (data[10] & 0xff) >> 4;
        int secDataLen = pmtInfo.mSectionLength + 3;
        pmtInfo.mCRC_32 = (data[secDataLen - 4] & 0x000000ff) << 24
                        | (data[secDataLen - 3] & 0x000000fff) << 16
                        | (data[secDataLen - 2] & 0x000000fff) << 8
                        | (data[secDataLen - 1] & 0x000000fff);
        //ProgramInfoLen (int)((data[10] & 0x0F) << 8) + ((int)(data[11]) & 0xff)
        pmtInfo.mProgramInfoLength = (data[10] & 0x0f) << 8 | data[11];
        if (mDebugTsSection)
            Log.d(TAG, "mProgramInfoLength is " + pmtInfo.mProgramInfoLength);
        int pos = 12;
        if (pmtInfo.mProgramInfoLength > 0) {
            int mDescriptorRemaining = pmtInfo.mProgramInfoLength;
            int mCaDesIdx = pos;
            while (mDescriptorRemaining > 0) {
                int mDescTag = data[mCaDesIdx];
                int mDescLen = data[mCaDesIdx + 1];
                if (mDescLen > 0) {
                    Log.d(TAG, "Found pmt descriptor. Tag: 0x" + Integer.toHexString(mDescTag) + " Len: " + mDescLen);
                    if (mDescTag == 0x09 && mDescLen >= 4) {
                        pmtInfo.mCaSystemId = ((((int)(data[mCaDesIdx + 2])) & 0x00ff) << 8) + (((int)data[mCaDesIdx + 3]) & 0xff);
                        pmtInfo.mCaPid = ((data[mCaDesIdx + 4] << 8) | data[mCaDesIdx + 5]) & 0x1fff;
                        Log.d(TAG, "PMT CaSystemId is 0x" + Integer.toHexString(pmtInfo.mCaSystemId)
                            + " CaPid is 0x" + Integer.toHexString(pmtInfo.mCaPid));
                        int mPrivateDataLen = mDescLen - 4;
                        if (mPrivateDataLen > 0) {
                            //pmtInfo.mPrivateDataLen = data[mCaDesIdx + 6];//10
                            pmtInfo.mPrivateDataLen = mPrivateDataLen;
                            Log.e(TAG, "mPrivateDataLen:" + pmtInfo.mPrivateDataLen);
                            if (pmtInfo.mPrivateData == null)
                                pmtInfo.mPrivateData = new byte[pmtInfo.mPrivateDataLen];
                            System.arraycopy(data, mCaDesIdx + 6, pmtInfo.mPrivateData, 0, mPrivateDataLen);
                            int byte1 = (int)(pmtInfo.mPrivateData[0] & 0xff);
                            int byte2 = (int)(pmtInfo.mPrivateData[1] & 0xff);
                            Log.d(TAG, "Found private data, byte1: 0x" + Integer.toHexString(byte1)
                            + " byte2: 0x" + Integer.toHexString(byte2));
                        } else {
                            Log.d(TAG, "No private data, use google test pssh");
                        }
                        mIsCasPlayback = true;
                        Log.d(TAG, "Found CaSystemId, cas playback");
                    } else if (mDescTag == 0x65) {
                        pmtInfo.mDscMode = data[mCaDesIdx + 2];
                        Log.d(TAG, "mDscMode: " + pmtInfo.mDscMode);
                    }
                } else {
                    Log.e(TAG, "Empty descriptor.");
                }
                mCaDesIdx += (1 + 1 + mDescLen);
                mDescriptorRemaining = mDescriptorRemaining - (mDescLen + 1 + 1);
                if (mDebugTsSection)
                    Log.d(TAG, "mDescriptorRemaining: " + mDescriptorRemaining);
            }
        } else {
            Log.d(TAG, "No program descriptor, clear playback");
            mIsCasPlayback = false;
        }
        pos += pmtInfo.mProgramInfoLength;
        if (mDebugTsSection)
            Log.d(TAG, "pos: " + pos + " pmt sec len: " + pmtInfo.mSectionLength);
        for ( ; pos < data.length/*(mPmtInfo.mSectionLength + 3)*/ - 4;)
         {
           if (pos < 0) {
               Log.e(TAG, "Broken PMT.");
               break;
           }
           boolean mIsVideo = false;
           int streamType = data[pos] & 0xff;
           Log.d(TAG, "streamType is 0x" + Integer.toHexString(streamType));
           for (int vType : videoStreamTypes) {
               if (vType == streamType) {
                   mIsVideo = true;
                   switch (streamType) {
                    case 0x01:
                    case 0x02:
                        mVideoMimeType = MediaFormat.MIMETYPE_VIDEO_MPEG2;
                        break;
                    case 0x10:
                        mVideoMimeType = MediaFormat.MIMETYPE_VIDEO_MPEG4;
                        break;
                    case 0x1b:
                        mVideoMimeType = MediaFormat.MIMETYPE_VIDEO_AVC;
                        break;
                    case 0x24:
                        mVideoMimeType = MediaFormat.MIMETYPE_VIDEO_HEVC;
                        break;
                    default:
                        Log.e(TAG, "unknown video format!");
                   }
                   Log.d(TAG, "mVideoMimeType:" + mVideoMimeType);
                   break;
               }
           }
           if (mIsVideo == false) {
               for (int aType : audioStreamTypes) {
                   if (aType == streamType) {
                       mIsVideo = false;
                       switch (streamType) {
                        case 0x03:
                        case 0x04:
                            mAudioMimeType = MediaFormat.MIMETYPE_AUDIO_MPEG;
                            if (mAudioformat == null)
                                mAudioformat = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_MP3).setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                                                .build();
                            break;
                        case 0x0e:
                            Log.w(TAG, "Audio auxiliary data.");
                            break;
                        case 0x0f:
                        case 0x11:
                            mAudioMimeType = MediaFormat.MIMETYPE_AUDIO_AAC;
                            if (mAudioformat == null)
                                mAudioformat = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_AAC_HE_V2).setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                                                .build();
                            break;
                        case 0x81:
                            mAudioMimeType = MediaFormat.MIMETYPE_AUDIO_AC3;
                            if (mAudioformat == null)
                                mAudioformat = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_AC3).setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                                                .build();
                            break;
                        case 0x06:
                        case 0x87:
                            mAudioMimeType = MediaFormat.MIMETYPE_AUDIO_EAC3;
                            if (mAudioformat == null)
                                mAudioformat = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_E_AC3).setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                                                .build();
                            break;
                        default:
                            Log.e(TAG, "unknown audio format!");
                       }
                       Log.d(TAG, "mAudioMimeType:" + mAudioMimeType);
                       break;
                   }
               }
           }
           pmtInfo.mReserved_5 = (data[pos + 1] & 0xff) >> 5;
           //ES_PID ((((int)(data[pos + 1])) & 0x001f) << 8) + (((int)(data[pos + 2])) & 0xff);
           int esPid = (data[pos + 1] & 0x1f) << 8 | (data[pos + 2] & 0xff);
           Log.d(TAG, "PMT elementary_PID is 0x" + Integer.toHexString(esPid) + " (" + esPid + ")" + " mIsVideo: " + mIsVideo);
            pmtInfo.mReserved_6  = (data[pos + 3] & 0xff) >> 4;
            int esInfoLen =  (data[pos + 3] & 0xf) << 8 | (data[pos + 4] & 0xff);
             if (data.length < pos + esInfoLen + 5) {
               Log.e(TAG, "Broken PMT. esInfoLen: " + esInfoLen + " pos: " + pos);
               break;
            }
            if (mIsVideo && mEsCasInfo[VIDEO_CHANNEL_INDEX] == null) {
                mEsCasInfo[VIDEO_CHANNEL_INDEX] = new DscChannelInfo(esPid, mIsVideo);
                mEsCasInfo[VIDEO_CHANNEL_INDEX].setEcmPid(pmtInfo.mCaPid);
            } else if (!mIsVideo && mEsCasInfo[AUDIO_CHANNEL_INDEX] == null) {
                mEsCasInfo[AUDIO_CHANNEL_INDEX] = new DscChannelInfo(esPid, mIsVideo);
            }

            if (esInfoLen >= 4) {
               int mEsDescTag = data[pos + 5];
               int mEsDescLen = data[pos + 6];
               Log.d(TAG, "esInfoLen: " + esInfoLen + " mEsDescTag:0x" + Integer.toHexString(mEsDescTag) + " mEsDescLen: " + mEsDescLen);
                if (mEsDescTag == 0x09 && mEsDescLen >= 4) {
                    pmtInfo.mCaSystemId = ((((int)(data[pos + 7])) & 0x00ff) << 8) + (((int)data[pos + 8]) & 0xff);
                    //pmtInfo.mCaPid = ((data[pos + 9] << 8) | data[pos + 10]) & 0x1fff;
                    pmtInfo.mCaPid = ((((int)(data[pos + 9])) & 0x001f) << 8) + (((int)data[pos + 10]) & 0xff);
                    Log.d(TAG, "mCaSystemId:0x" + Integer.toHexString(pmtInfo.mCaSystemId) + " mCaPid:0x" + Integer.toHexString(pmtInfo.mCaPid));
                    if (mIsVideo && mEsCasInfo[VIDEO_CHANNEL_INDEX].getEcmPid() == 0x1fff) {
                        mEsCasInfo[VIDEO_CHANNEL_INDEX].setEcmPid(pmtInfo.mCaPid);
                    } else if (!mIsVideo && mEsCasInfo[AUDIO_CHANNEL_INDEX].getEcmPid() == 0x1fff) {
                        mEsCasInfo[AUDIO_CHANNEL_INDEX].setEcmPid(pmtInfo.mCaPid);
                    }
                    mEsDescLen -= 4;
                    if (mEsDescLen > 0) {
                        pmtInfo.mPrivateDataLen = mEsDescLen;
                        pmtInfo.mPrivateData = new byte[pmtInfo.mPrivateDataLen];
                        System.arraycopy(data, pos + 11, pmtInfo.mPrivateData, 0, mEsDescLen);
                        int byte1 = (int)(pmtInfo.mPrivateData[0] & 0xff);
                        int byte2 = (int)(pmtInfo.mPrivateData[1] & 0xff);
                        Log.d(TAG, "Found es private data, byte1: 0x" + Integer.toHexString(byte1)
                        + " byte2: 0x" + Integer.toHexString(byte2));
                    } else {
                        Log.d(TAG, "No private data, use google test pssh");
                    }
                    mIsCasPlayback = true;
                    Log.d(TAG, "Found mCaSystemId, cas playback");
                }
           }
           //skip the es descriptors' info length
           pos += esInfoLen;
           if (mIsVideo) {
               pmtInfo.mPmtStreams.add(new PmtStreamInfo(streamType, esPid, mEsCasInfo[VIDEO_CHANNEL_INDEX].getEcmPid(), mIsVideo));
               Log.d(TAG, "mPmtStreams add a video track");
           } else {
               pmtInfo.mPmtStreams.add(new PmtStreamInfo(streamType, esPid, mEsCasInfo[AUDIO_CHANNEL_INDEX].getEcmPid(), mIsVideo));
               Log.d(TAG, "mPmtStreams add a audio track, streamType is " + streamType);
           }
           Log.d(TAG, "mPmtInfo.mPmtStreams.size: " + mPmtInfo.mPmtStreams.size());
           //skip the previous es basic info length
           pos += 5;
          }
        if (mEsCasInfo[VIDEO_CHANNEL_INDEX] != null) {
            if (mEsCasInfo[VIDEO_CHANNEL_INDEX].getEcmPid() != 0x1fff)
                mEcmPidNum += 1;
        }
        if (mEsCasInfo[AUDIO_CHANNEL_INDEX] != null) {
            if (mEsCasInfo[AUDIO_CHANNEL_INDEX].getEcmPid() != 0x1fff)
                mEcmPidNum += 1;
        }
        return;
    }

    private void passthroughSetup() {
        Log.d(TAG, "passthroughSetup");
        // for tunneled playing
        AudioManager audioManager =
                (AudioManager) mActivity.getApplicationContext().getSystemService(Context.AUDIO_SERVICE);
        if (audioManager != null) {
            int audioSessionId = audioManager.generateAudioSessionId();
            mVideoMediaFormat.setInteger(
                    MediaFormat.KEY_AUDIO_SESSION_ID, audioSessionId);
        }
        if (mVideoFilter != null) {
            mVideoFilterId = mVideoFilter.getId();
            Log.d(TAG, "mVideoFilterId:" + mVideoFilterId);
        }
        if (mAudioFilter != null) {
            mAudioFilterId = mAudioFilter.getId();
            Log.d(TAG, "mAudioFilterId:" + mAudioFilterId);
        }
        mAvSyncHwId = mTuner.getAvSyncHwId(mVideoFilter != null ? mVideoFilter : mAudioFilter);
        Log.d(TAG, "mAvSyncHwId:" + mAvSyncHwId);
        mVideoMediaFormat.setInteger(VIDEO_FILTER_ID_KEY, mVideoFilterId);
        mVideoMediaFormat.setInteger(HW_AV_SYNC_ID_KEY, mAvSyncHwId);
        mVideoMediaFormat.setFeatureEnabled(CodecCapabilities.FEATURE_TunneledPlayback, true);
    }

    private void CreateAudioTrack() {
    Log.d(TAG, "CreateAudioTrack mPlayAudio:" + mPlayAudio);
        if (mPlayAudio == false)
            return;
        if (mAudioformat != null) {
            try {
                mAudioTrack = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build())
                    .setAudioFormat(mAudioformat)
                    .setBufferSizeInBytes(256)
                    .setEncapsulationMode(AudioTrack.ENCAPSULATION_MODE_HANDLE)
                    .setTunerConfiguration(new AudioTrack.TunerConfiguration(mAudioFilterId /* contentId */, mAvSyncHwId /* syncId */))
                    .build();
                Log.d(TAG, "CreateAudioTrack done!");
            }
            catch (UnsupportedOperationException e) {
                Log.d(TAG, "CreateAudioTrack err:" + e.toString());
            }
        } else {
            Log.e(TAG, "mAudioformat is null!");
        }
    }

    private void parseSectionData(byte[] data) {
        int mTableId = data[0];
        int mSectionLen = ((((int)(data[1])) & 0x03 << 8) & 0xff) + (((int)data[2]) & 0xff);
        if (mDebugTsSection) {
            Log.d(TAG, "parseSectionData mTableId: " + mTableId + " mSectionLen:" + mSectionLen);
        }
        if (mTableId == PAT_TID) {
            if (mPatInfo == null) {
                mPatInfo = new PatInfo();
                Log.d(TAG, "Create new PatInfo" + " PAT section len = " + mSectionLen);
            }
            if (!mPatInfo.mPrograms.isEmpty()) {
                //Log.d(TAG, "programs is not empty!");
                if (mPatSectionFilter != null) {
                    Log.d(TAG, "Stop pat section filter");
//                    mPatSectionFilter.stop();
                }
                return;
            }
            parsePATSection(mPatInfo, data);
            Message mFoundPmtMsg = mTaskHandler.obtainMessage(TaskMsg.TASK_MSG_PULL_SECTION);
            if (mProgramId > mPatInfo.mPrograms.size()) {
                Log.e(TAG, "mProgramId error! mProgramId:" + mProgramId + " mPrograms size:" + mPatInfo.mPrograms.size());
                return;
            }
            mFoundPmtMsg.arg1 = mPatInfo.mPrograms.get(mProgramId - 1).mPmtPid;
            mFoundPmtMsg.arg2 = PMT_TID;
            Log.d(TAG, "Send message TASK_MSG_PULL_SECTION");
            mTaskHandler.sendMessage(mFoundPmtMsg);
        } else if (mTableId == PMT_TID) {
            int programId = ((((int)(data[3])) & 0x00ff) << 8) + (((int)data[4]) & 0xff);
            if (data.length <= 11) {
                Log.e(TAG, "Broken PMT.");
                return;
            }
            //Use pmt parser
            if (mPmtInfo == null) {
                mPmtInfo = new PmtInfo();
                Log.d(TAG, "Create new PmtInfo" + " PMT section len = " + mSectionLen);
            }
            if (!mPmtInfo.mPmtStreams.isEmpty()) {
                if (mDebugTsSection)
                    Log.d(TAG, "PMT has already been parsed!");
                return;
            } else {
                Log.d(TAG, "mPmtStreams is empty.");
            }
            parsePMTSection(mPmtInfo, data);
            Log.d(TAG, "Find programs down. mIsCasPlayback:" + mIsCasPlayback);

            mPcrFilter = openPcrFilter(mPmtInfo.mPcrPid);
            //Prepare av mediaformats for passthrough
            mVideoMediaFormat = MediaFormat.createVideoFormat(mVideoMimeType, 1280, 720);
            mAudioMediaFormat = MediaFormat.createAudioFormat(mAudioMimeType, MediaCodecPlayer.AUDIO_SAMPLE_RATE, MediaCodecPlayer.AUDIO_CHANNEL_COUNT);

            mTaskHandler.sendEmptyMessage(TaskMsg.TASK_MSG_STOP_SECTION);

            if (!mEnableLocalPlay) {
                Log.d(TAG, "RF Mode");
                return;
            }

            if (mEnableLocalPlay) {
                //Open av filters after parsing pmt section
                mVideoFilter = openVideoFilter(mEsCasInfo[VIDEO_CHANNEL_INDEX].mEsPid);
                mAudioFilter = openAudioFilter(mEsCasInfo[AUDIO_CHANNEL_INDEX].mEsPid);

                if (mPassthroughMode)
                    passthroughSetup();

                if (!mIsCasPlayback) {
                    if (!mPassthroughMode) {
                        //For non-passthrough playback, start av filter here
                        //For non-passthrough cas playback, start av filter after getting the ecm section
                        if (mVideoFilter != null) {
                            mVideoFilter.start();
                        }
                        if (mAudioFilter != null) {
                            //mAudioFilter.start();
                        }
                    }
                    //For non-passthrough/passthrough clear playback, config and start mediacodec
                    mNeedSeekToBegin.compareAndSet(false, true);
                } else {
                    //For non-passthrough/passthrough cas playback
                    //Create media cas instance
                    //Provision and request license
                    //Start ecm section filter
                    try {
                        if (setUpCasPlayback(mPmtInfo.mCaSystemId) == false) {
                            Log.e(TAG, "setUpCasPlayback failed!");
                            return;
                        }
                    } catch (Exception e) {
                        Log.e(TAG, "setUpCasPlayback Exception: " + e.getMessage());
                        return;
                    }
                    Log.d(TAG, "Ready to start Player");
                    if (!mPassthroughMode) {
                        if (mVideoFilter != null) {
                            mDvrPlayback.attachFilter(mVideoFilter);
                            mVideoFilter.start();
                        }
                        if (mAudioFilter != null) {
                            mDvrPlayback.attachFilter(mAudioFilter);
                            //mAudioFilter.start();
                        }
                    }
                }
            }
        }
    }

    private void startSectionFilter(int pid, int tid) {
        Log.d(TAG, "Start section filter pid: 0x" + Integer.toHexString(pid) + " table id: 0x" + Integer.toHexString(tid));
        if (mTuner == null) {
            Log.e(TAG, "mTuner is null!");
            return;
        }
        byte tableId = (byte)(tid & 0xff);
        /*
        Settings settings = SectionSettingsWithTableInfo
        .builder(Filter.TYPE_TS)
        .setTableId(tableId)
        .setCrcEnabled(true)
        .setRaw(false)
        .setRepeat(false)
        .build();*/
        byte mask = (byte)255;
        SectionSettingsWithSectionBits settings =
        SectionSettingsWithSectionBits
                .builder(Filter.TYPE_TS)
                .setCrcEnabled(false)
                .setRepeat(false)
                .setRaw(false)
                .setFilter(new byte[]{tableId, 0, 0})
                .setMask(new byte[]{mask, 0, 0, 0})
                .setMode(new byte[]{0, 0, 0})
                .build();
        FilterConfiguration config = TsFilterConfiguration
        .builder()
        .setTpid(pid)
        .setSettings(settings)
        .build();
        if (tid == PAT_TID) {
            if (mPatSectionFilter != null) {
                mPatSectionFilter.stop();
            }
            Log.d(TAG, "Open mPatSectionFilter");
            mPatSectionFilter = mTuner.openFilter(Filter.TYPE_TS, Filter.SUBTYPE_SECTION, SECTION_FILTER_BUFFER_SIZE, mExecutor, mfilterCallback);
            mPatSectionFilter.configure(config);
            mPatSectionFilter.start();
        } else if (tid == PMT_TID) {
            if (mPmtSectionFilter != null) {
                mPmtSectionFilter.stop();
            }
            Log.d(TAG, "Open mPmtSectionFilter");
            if (mPmtSectionFilter == null) {
                mPmtSectionFilter = mTuner.openFilter(Filter.TYPE_TS, Filter.SUBTYPE_SECTION, SECTION_FILTER_BUFFER_SIZE, mExecutor, mfilterCallback);
                mPmtSectionFilter.configure(config);
                mPmtSectionFilter.start();
            } else {
                Log.w(TAG, "Already get the pmt info!");
                return;
            }
        }
        Log.d(TAG, "Section filter(0x" + Integer.toHexString(pid) + ") start");
    }

    private void startEcmSectionFilter(int esIdx, int pid, int tid, int ecmFilterIdx) {
        Log.d(TAG, "Start ecm section filter esIdx: " + esIdx + " pid: 0x" + Integer.toHexString(pid)
            + " table id: 0x" + Integer.toHexString(tid) + " ecmFilterIdx: " + ecmFilterIdx);
        if (mTuner == null) {
            Log.e(TAG, "mTuner is null!");
            return;
        }
        byte tableId = (byte)(tid & 0xff);
        /*
        Settings settings = SectionSettingsWithTableInfo
        .builder(Filter.TYPE_TS)
        .setTableId(tableId)
        .setCrcEnabled(false)
        .setRaw(false)
        .setRepeat(false)
        .build();*/
//      byte[] codes = new byte[1];
//      byte[] masks = new byte[1];
//      byte[] modes = new byte[1];
//      codes[0] = tableId;
//      masks[0] = (byte) 0xFF;
//      modes[0] = (byte) 0x00;

        byte mask = (byte)255;
        SectionSettingsWithSectionBits settings =
        SectionSettingsWithSectionBits
                .builder(Filter.TYPE_TS)
                .setCrcEnabled(false)
                .setRepeat(true)
                .setRaw(false)
//              .setFilter(codes)
//              .setMask(masks)
//              .setMode(modes)
                .setFilter(new byte[]{tableId, 0, 0})
                .setMask(new byte[]{mask, 0, 0, 0})
                .setMode(new byte[]{0, 0, 0})
                .build();
        FilterConfiguration config = TsFilterConfiguration
        .builder()
        .setTpid(pid)
        .setSettings(settings)
        .build();

        mEsCasInfo[esIdx].mEcmSectionFilter[ecmFilterIdx] =
            mTuner.openFilter(Filter.TYPE_TS, Filter.SUBTYPE_SECTION, SECTION_FILTER_BUFFER_SIZE, mExecutor, mEcmFilterCallback);
        mEsCasInfo[esIdx].mEcmSectionFilter[ecmFilterIdx].configure(config);
        mEsCasInfo[esIdx].mEcmSectionFilter[ecmFilterIdx].start();
        Log.d(TAG, "Section ecm filter(0x" + Integer.toHexString(pid) + ") start");
    }

    private void stopEcmSectionFilter(int esIdx, int filter_id) {
        for (int idx = 0; idx < MAX_CAS_ECM_TID_NUM; idx++) {
            if (mEsCasInfo[esIdx] != null && mEsCasInfo[esIdx].mEcmSectionFilter[idx] != null) {
                if (mEsCasInfo[esIdx].mEcmSectionFilter[idx].getId() == filter_id) {
                    mEsCasInfo[esIdx].mEcmSectionFilter[idx].stop();
                    Log.d(TAG, "Stop ecm filter " + idx + " for mCasIdx " + esIdx + " filter_id: " + filter_id);
                    break;
                } else if (filter_id == -1) {
                    mEsCasInfo[esIdx].mEcmSectionFilter[idx].stop();
                    Log.d(TAG, "Stop ecm filter " + idx + " for mCasIdx " + esIdx);
                }
            }
        }
    }

    private Filter openVideoFilter(int pid) {
        Log.d(TAG, "Open video filter pid: 0x" + Integer.toHexString(pid));
        long vFilterBufSize = 1024 * 1024 * 10;
        if (pid == 0x1FFF || pid <= 0) {
            Log.e(TAG, "Invalid pid: 0x" + Integer.toHexString(pid));
            return null;
        }

        Filter filter = mTuner.openFilter(Filter.TYPE_TS, Filter.SUBTYPE_VIDEO, vFilterBufSize, mExecutor, mfilterCallback);
        if (filter == null) {
            Log.e(TAG, "Video filter is null!");
            return filter;
        }

        Settings videoSettings = AvSettings
        .builder(Filter.TYPE_TS, false)
        .setPassthrough(mPassthroughMode)
        .build();

        FilterConfiguration videoConfig = TsFilterConfiguration
        .builder()
        .setTpid(pid)
        .setSettings(videoSettings)
        .build();
        filter.configure(videoConfig);
        return filter;
    }

    private Filter openAudioFilter(int pid) {
        Log.d(TAG, "Open audio filter pid: 0x" + Integer.toHexString(pid));
        long aFilterBufSize = 1024 * 1024;
        if (pid == 0x1FFF || pid <= 0) {
            Log.e(TAG, "Invalid pid: 0x" + Integer.toHexString(pid));
            return null;
        }

        Filter filter = mTuner.openFilter(Filter.TYPE_TS, Filter.SUBTYPE_AUDIO, aFilterBufSize, mExecutor, mfilterCallback);
        if (filter == null) {
            Log.e(TAG, "Audio filter is null!");
            return filter;
        }

         Settings audioSettings = AvSettings
        .builder(Filter.TYPE_TS, true)
        .setPassthrough(mPassthroughMode)
        .build();

         FilterConfiguration audioConfig = TsFilterConfiguration
        .builder()
        .setTpid(pid)
        .setSettings(audioSettings)
        .build();
         filter.configure(audioConfig);
         return filter;
    }

    private Filter openPcrFilter(int pcrPid) {
        if (mEnableLocalPlay)
            pcrPid = 0x1fff;
        Log.d(TAG, "openPcrFilter pcrPid:0x" + Integer.toHexString(pcrPid));
        Filter filter = mTuner.openFilter(Filter.TYPE_TS, Filter.SUBTYPE_PCR, 1024 * 1024, mExecutor, mfilterCallback);
        if (filter != null) {
            FilterConfiguration pcrConfig = TsFilterConfiguration
                    .builder()
                    .setTpid(pcrPid)
                    .setSettings(null)
                    .build();
            filter.configure(pcrConfig);
            filter.start();
        } else {
            Log.e(TAG, "Pcr filter is null!");
        }
        return filter;
    }

    private DvrSettings getDvrSettings() {
        return DvrSettings
                .builder()
                .setStatusMask(Filter.STATUS_DATA_READY)
                .setLowThreshold(mDvrLowThreshold)
                .setHighThreshold(mDvrHighThreshold)
                .setPacketSize(188L)
                .setDataFormat(DvrSettings.DATA_FORMAT_TS)
                .build();
    }

    private Filter openDvrFilter(int vpid) {
        Log.d(TAG, "openDvrFilter");
        Filter filter = mTuner.openFilter(Filter.TYPE_TS, Filter.SUBTYPE_RECORD, 1024 * 1024 * 10 * 20, mExecutor, mfilterCallback);
        if (filter != null) {
            Settings settings = RecordSettings
                    .builder(Filter.TYPE_TS)
                    .setTsIndexMask(RecordSettings.TS_INDEX_FIRST_PACKET)
                    .build();
            FilterConfiguration config = TsFilterConfiguration
                    .builder()
                    .setTpid(vpid)
                    .setSettings(settings)
                    .build();
            filter.configure(config);
            filter.start();
            filter.flush();
        }
        return filter;
    }

    private void startDvrRecorder(int vpid) throws Exception {
        Log.d(TAG, "startDvrRecorder");
        ParcelFileDescriptor fd = ParcelFileDescriptor.open(tmpFile, ParcelFileDescriptor.MODE_READ_WRITE);
        if (mTuner == null) {
            mTuner = new Tuner(mActivity.getApplicationContext(),
                               null/*tvInputSessionId*/,
                               200/*PRIORITY_HINT_USE_CASE_TYPE_SCAN*/);
            Log.d(TAG, "mTuner created:" + mTuner);
            mTuner.setOnTuneEventListener(mExecutor, this);
        }
        if (mDvrRecorder == null) {
            mDvrRecorder = mTuner.openDvrRecorder(mDvrMQSize_MB * 1024 * 1024, mExecutor, this);
            mDvrRecorder.configure(getDvrSettings());
        }
        mDvrRecorder.setFileDescriptor(fd);
        mDvrFilter = openDvrFilter(vpid);
        mDvrRecorder.attachFilter(mDvrFilter);

        mDvrRecorder.start();
        mDvrRecorder.flush();
    }

    public void onFrameRendered(MediaCodec codec, long presentationTimeUs, long nanoTime) {
        if (mDecoderFreeBufPercentage.get() != presentationTimeUs && mPassthroughMode) {
            mDecoderFreeBufPercentage.set(presentationTimeUs);
            if (mEnableFlowCtl && presentationTimeUs < MIN_DECODER_BUFFER_FREE_THRESHOLD / 2)
                Log.d(TAG, "onFrameRendered decoderFreeBufPercentage = " + presentationTimeUs + " nanoTime= " + nanoTime);
        }
    }

    private void readDataToPlay() {
        Log.d(TAG, "readDataToPlay");
        String mDvrReadTsPktNumStr = Utils.getPropString("getprop " + DBGProp.DVR_PROP_READ_TSPKT_NUM);
        if (mDvrReadTsPktNumStr != null && mDvrReadTsPktNumStr.length() > 0)
            mDvrReadTsPktNum = Integer.parseInt(mDvrReadTsPktNumStr);
        Log.d(TAG, "mDvrReadTsPktNum: " + mDvrReadTsPktNum);
        final long mDvrOnceReadSize = mDvrReadTsPktNum * 188;
        String mDurationStr = Utils.getPropString("getprop " + DBGProp.DVR_PROP_READ_DATA_DURATION);
        if (mDurationStr != null && mDurationStr.length() > 0)
            mDvrReadDataDuration = Integer.parseInt(mDurationStr);
        Log.d(TAG, "mDvrReadDataDuration: " + mDvrReadDataDuration + "ms");
        mDvrReadThreadExit.set(false);
        FlowControl flowControl = new FlowControl(mDvrReadDataDuration, mDecoderFreeBufPercentage.get());

        new Thread(new Runnable() {
            @Override
            public void run() {
                int totalReadMBs = 0;
                int tempMB = 0;
                 mDvrReadStart.compareAndSet(false, true);
                 while (mDvrReadStart.get() && mDvrPlayback != null) {
                    if (mIsCasPlayback && mPlayerStart.get() && !mCasLicenseReceived.get()) {
                        try {
                            Thread.sleep(5);
                        } catch (Exception e) {
                            e.printStackTrace();
                        }
                        continue;
                    }
                    long mReadLen = mDvrPlayback.read(mDvrOnceReadSize);
                    try {
                        flowControl.updateDuration(mDecoderFreeBufPercentage.get(), mDvrPlaybackStatus.get());
                        Thread.sleep(flowControl.getDuration());
                        if (mEnableLocalPlay) {
                            if (mNeedSeekToBegin.get() == true) {
                                mDvrPlayback.stop();
                                stopAVPlay();
                                playStart(!mEnableLocalPlay);
                                Log.d(TAG, "Seek mTestFileDescriptor to SEEK_SET");
                                Os.lseek(mTestFileDescriptor, 0, OsConstants.SEEK_SET);
                                mDvrPlayback.start();
                                mDvrPlayback.flush();
                                mNeedSeekToBegin.compareAndSet(true, false);
                                Thread.sleep(200);
                                Log.d(TAG, "DvrPlayback start read data");
                            }
                        }
                     } catch (Exception e) {
                        e.printStackTrace();
                     }
                    //android_media_tv_Tuner_read_dvr->ret = read(dvrSp->mFd, data, firstToWrite)->(dvrSp->mDvrMQ->commitWrite(ret))
                    if (mReadLen == 0) {
                        Log.w(TAG, "mDvrPlayback read len is 0!");
                        break;
                    } else {
                        totalReadMBs += mReadLen;
                        if (totalReadMBs/1024/1024 % 10 == 0 && totalReadMBs/1024/1024 != tempMB) {
                            tempMB = totalReadMBs/1024/1024;
                            Log.d(TAG, "DvrPlayback read data total " + tempMB + "MB");
                        }
                    }
                 }
                 Log.d(TAG, "Exit DvrPlayback read data thread.");
                 mDvrReadStart.compareAndSet(true, false);
                 mDvrReadThreadExit.compareAndSet(false, true);
            }
        }).start();
    }

    public void stopAudio() {
        if (mAudioTrack != null) {
            Log.d(TAG, "mAudioTrack stop");
            mAudioTrack.stop();
            mAudioTrack.release();
            mAudioTrack = null;
            mAudioformat = null;
        }
    }

    public void stopVideo() {
        if (mMediaCodecPlayer != null) {
            Log.d(TAG, "mMediaCodecPlayer stop");
            mMediaCodecPlayer.stopPlayer();
            mMediaCodecPlayer = null;
        }
    }

    public void stopAVPlay() {
        if (mPlayerStart.get() == true) {
            stopVideo();
            stopAudio();
            mPlayerStart.compareAndSet(true, false);
        }
    }

    public void stopFilters(boolean casPlayback) {
        Log.d(TAG, "stopFilters");
        if (mPatSectionFilter != null)
            mPatSectionFilter.stop();
        if (mPmtSectionFilter != null)
            mPmtSectionFilter.stop();
        if (casPlayback) {
            stopEcmSectionFilter(VIDEO_CHANNEL_INDEX, -1);
            stopEcmSectionFilter(AUDIO_CHANNEL_INDEX, -1);
        }
        if (mDvrFilter != null && mDvrRecorder != null) {
            mDvrRecorder.detachFilter(mDvrFilter);
        }
        if (mVideoFilter != null)
            mVideoFilter.stop();
        if (mAudioFilter != null && mPassthroughMode);
            mAudioFilter.stop();
        if (mPcrFilter != null)
            mPcrFilter.stop();
    }

    public void startTuner() {
        if (mTuner!= null && !mTuneStart.get()) {
            Log.d(TAG, "Frequency:" + mFrequency + "MHz");
            int freqMhz = 0;
            try {
                freqMhz = mFrequency;
            } catch (Exception e) {
            }
            FrontendSettings feSettings = null;
            switch (mScanMode) {
                case "Dvbt":
                case "Dvbt2": {
                    feSettings = DvbtFrontendSettings
                    .builder()
                    .setFrequency(freqMhz  * 1000000)
                    .setStandard("Dvbt2".equals(mScanMode) ?
                                    DvbtFrontendSettings.STANDARD_T2 : DvbtFrontendSettings.STANDARD_T)
                    .setPlpMode(DvbtFrontendSettings.PLP_MODE_AUTO)
                    .build();
                }
                break;
                case "Dvbc": {
                    int symbol = 6900;
                    try {
                        symbol = mSymbol;
                    } catch (Exception e) {
                    }
                    feSettings = DvbcFrontendSettings
                    .builder()
                    .setFrequency(freqMhz  * 1000000)
                    .setAnnex(DvbcFrontendSettings.ANNEX_A)
                    .setSymbolRate(symbol*1000)
                    .build();
                }
                break;
                case "Dvbs":
                case "Dvbs2": {
                    int symbol = 27500;
                    try {
                        symbol = mSymbol;
                    } catch (Exception e) {
                    }
                    DvbsCodeRate codeRate = DvbsCodeRate.builder()
                        .setInnerFec(FrontendSettings.FEC_AUTO)
                        .setLinear(false)
                        .setShortFrameEnabled(true)
                        .setBitsPer1000Symbol(0)
                        .build();
                    feSettings = DvbsFrontendSettings
                    .builder()
                    .setFrequency(freqMhz  * 1000000)
                    .setSymbolRate(symbol*1000)
                    .setStandard("Dvbs2".equals(mScanMode) ?
                                    DvbsFrontendSettings.STANDARD_S2 : DvbsFrontendSettings.STANDARD_S)
                    .setCodeRate(codeRate)
                    .build();
                }
                break;
                case "Analog": {
                    //not support this type to play
                }
                break;
                case "Atsc": {
                    //not support this type to play
                }
                break;
                case "Isdbt": {
                    feSettings = IsdbtFrontendSettings
                    .builder()
                    .setFrequency(freqMhz  * 1000000)
                    .setModulation(IsdbtFrontendSettings.MODULATION_AUTO)
                    .setBandwidth(IsdbtFrontendSettings.BANDWIDTH_AUTO)
                    .build();
                }
                break;
            }
            if (feSettings != null) {
                mTuner.tune(feSettings);
                mTuneStart.set(true);
            }
        }
    }

    public void stopTuner() {
        if (mDvrPlayback != null) {
            Log.d(TAG, "mDvrPlayback stop and close ...");
            mDvrPlayback.stop();
            mDvrPlayback.close();
            mDvrPlayback = null;
        }
        if (mDvrRecorder != null) {
            Log.d(TAG, "mDvrRecorder stop and close ...");
            mDvrRecorder.stop();
            mDvrRecorder.close();
            mDvrRecorder = null;
        }
        if (mTuner != null) {
            stopFilters(mIsCasPlayback);
            if (mTuneStart.get() == true) {
                mTuner.cancelTuning();
                mTuneStart.set(false);
            }
            Log.d(TAG, "mTuner close ...");
            mTuner.close();
            mTuner = null;
        }
        mDescrambler = null;
        mPatSectionFilter = null;
        mPmtSectionFilter = null;
        mVideoFilter = null;
        mAudioFilter = null;
        mPcrFilter = null;
        mDvrFilter = null;
    }

    public void closeMediaFile() {
        if (mTestFd != null) {
            try {
                Log.d(TAG, "mTestFd close ...");
                mTestFd.close();
                mTestFd = null;
                mTestLocalFile = null;
                mTestFileDescriptor = null;
            } catch (IOException e) {
                Log.e(TAG, "IOException! " + e);
            }
        }
    }

    public void ReleaseCas() {
        for (int mCasIdx = 0; mCasIdx < MAX_DSC_CHANNEL_NUM; mCasIdx ++) {
            if (mEsCasInfo[mCasIdx] != null) {
                //mEsCasInfo[mCasIdx].mGetCryptoMode = false;
                for (int mEcmFilterIdx = 0; mEcmFilterIdx < MAX_CAS_ECM_TID_NUM; mEcmFilterIdx ++) {
                    if (mEsCasInfo[mCasIdx].mEcmSectionFilter[mEcmFilterIdx] != null)
                        mEsCasInfo[mCasIdx].mEcmSectionFilter[mEcmFilterIdx] = null;
                }
                if (mEsCasInfo[mCasIdx].mCasKeyToken != null)
                    mEsCasInfo[mCasIdx].mCasKeyToken = null;
                if (mEsCasInfo[mCasIdx].mCasSession != null) {
                    mEsCasInfo[mCasIdx].mCasSession.close();
                    mEsCasInfo[mCasIdx].mCasSession = null;
                }
                mEsCasInfo[mCasIdx].mCasSessionId = -1;
                mEsCasInfo[mCasIdx].mSessionOpened.compareAndSet(true, false);
                mEsCasInfo[mCasIdx] = null;
            }
        }

        if (mMediaCas != null) {
            Log.d(TAG, "mMediaCas close ...");
            mMediaCas.close();
            mMediaCas = null;
            mListener = null;
        }

        mCurCasIdx.getAndSet(-1);
        mEcmPidNum = 0;
        mCasSessionNum.getAndSet(0);
        mCasProvisioned.compareAndSet(true, false);
        mCasLicenseReceived.compareAndSet(true, false);
        mHasSetPrivateData.compareAndSet(true, false);
        mIsCasPlayback = false;
    }

    public void startDvrPlayback() throws Exception {
        Log.d(TAG, "startDvrPlayback. TS source:" + mLocalTsFileForPlayback);

        mTestLocalFile = new File(mLocalTsFileForPlayback);
        mTestFd = ParcelFileDescriptor.open(mTestLocalFile, ParcelFileDescriptor.MODE_READ_ONLY);
        if (mTestFd == null) {
            Log.e(TAG, "mTestFd is null!");
            return;
        }
        mTestFileDescriptor = mTestFd.getFileDescriptor();
        Log.i(TAG, "mProgramId:" + mProgramId);
        Log.d(TAG, "mTestFd: " + mTestFd.getFd());

        if (mTuner == null) {
            mTuner = new Tuner(mActivity.getApplicationContext(),
                               null/*tvInputSessionId*/,
                               200/*PRIORITY_HINT_USE_CASE_TYPE_SCAN*/);
            if (mTuner == null)
                return;
            Log.d(TAG, "mTuner created:" + mTuner);
            mTuner.setOnTuneEventListener(mExecutor, this);
        }
        if (mDvrPlayback == null) {
            mDvrPlayback = mTuner.openDvrPlayback(mDvrMQSize_MB * 1024 * 1024, mExecutor, this);
            if (mDvrPlayback == null)
                return;
            mDvrPlayback.configure(getDvrSettings());
        }
        mDvrPlayback.setFileDescriptor(mTestFd);
        startSectionFilter(PAT_PID, PAT_TID);
        //Wait DemuxQueueNotifyBits::DATA_READY->readPlaybackFMQ->mDvrMQ->read->AM_DMX_WriteTs
        mDvrPlayback.start();
        mDvrPlayback.flush();
        readDataToPlay();
    }

    public void stopDvrPlayback() {
        Log.d(TAG, "stopDvrPlayback");
        int retry = 0;
        mDvrReadStart.set(false);
        while (mEnableLocalPlay && mDvrReadThreadExit.get() != true && retry < 200) {
            try {
                Thread.sleep(5);
            } catch (Exception e) {
                e.printStackTrace();
            }
            retry ++;
        }
        Log.d(TAG, "retry:" + retry);
        stopAVPlay();
        mNeedSeekToBegin.compareAndSet(true, false);
        stopTuner();
        mPatInfo = null;
        mPmtInfo = null;
        builder = null;
        closeMediaFile();
        ReleaseCas();
    }

    public void onTuneEvent(int tuneEvent) {
        Log.d(TAG, "onTuneEvent event: " + tuneEvent);
        mUiHandler.sendMessage(mUiHandler.obtainMessage(SetupActivity.UI_MSG_STATUS, "Got lock event: " + tuneEvent));

        if (tuneEvent == 0) {
            Log.d(TAG, "tuner status is lock");
        } else if (tuneEvent == 1) {
            Log.d(TAG, "tuner got no signal");
        } else if (tuneEvent == 2) {
            Log.d(TAG, "tuner lost lock");
        }
    }

    @Override
    public void onPlaybackStatusChanged(int status) {
        if (mDvrPlaybackStatus.get() != status) {
            Log.d(TAG, "onPlaybackStatusChanged status:" + status);
            mDvrPlaybackStatus.set(status);
        }
    }

    @Override
    public void onRecordStatusChanged(int status) {
        Log.d(TAG, "onRecordStatusChanged status:" + status);
    }

    public class LinearInputBlock1 {
        public MediaCodec.LinearBlock block;
        public ByteBuffer buffer;
        public int offset;
    }

    private MediaExtractor mMediaExtractor = null;
    private MediaCodec.LinearBlock mBlocks = null;
    private boolean mSending = false;
    private static final int APP_BUFFER_SIZE = 2 * 1024 * 1024; //2 MB
    private LinearInputBlock1 mLinearInputBlock = null;

    private boolean initSource() {
        boolean result = true;
        Log.d(TAG, "initSource");
        mMediaExtractor = new MediaExtractor();
        try {
            mMediaExtractor.setDataSource(mLocalTsFileForPlayback, null);
        } catch (Exception e) {
            result = false;
            Log.d(TAG, "initLocalMediaExtractor Exception = " + e.getMessage());
        }

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
        Log.d(TAG, "initLocalMediaCodec selectedTrackIndex = " + selectedTrackIndex + ", selectedTrackMediaFormat = " + selectedTrackMediaFormat);
        if (selectedTrackIndex == -1) {
            Log.d(TAG, "initLocalMediaCodec couldn't get a video track");
            result = false;
        }
        return result;
    }

    private void sendData() {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    Thread.sleep(3000);
                } catch (Exception e) {
                    e.printStackTrace();
                }
                mSending = true;
                while (mSending) {
                    /*if (mBlocks == null) {
                        mBlocks = MediaCodec.LinearBlock.obtain(Math.toIntExact(APP_BUFFER_SIZE), new String[] {"OMX.google.h264.decoder"});
                        //mBlocks = MediaCodec.LinearBlock.obtain(Math.toIntExact(APP_BUFFER_SIZE), new String[] {"OMX.amlogic.avc.decoder.awesome2"});
                        //OMX.amlogic.avc.decoder.awesome2  OMX.google.h264.decoder
                        assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                                mBlocks.isMappable());
                    } else {
                        try {
                            mBlocks.recycle();
                        } catch (Exception e) {

                        }
                        mBlocks = MediaCodec.LinearBlock.obtain(Math.toIntExact(APP_BUFFER_SIZE), new String[] {"OMX.google.h264.decoder"});
                        //mBlocks = MediaCodec.LinearBlock.obtain(Math.toIntExact(APP_BUFFER_SIZE), new String[] {"OMX.amlogic.avc.decoder.awesome2"});
                        assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                                mBlocks.isMappable());
                    }*/
                    /*if (mBlocks != null) {
                        mBlocks.recycle();
                        mBlocks = null;
                    }*/
                    if (mLinearInputBlock == null) {
                        mLinearInputBlock = new LinearInputBlock1();
                    }
                    if (mLinearInputBlock.block != null) {
                        mLinearInputBlock.block.recycle();
                    }
                    mLinearInputBlock.block = MediaCodec.LinearBlock.obtain(Math.toIntExact(APP_BUFFER_SIZE), new String[] {"OMX.google.h264.decoder"});
                    assertTrue("Blocks obtained through LinearBlock.obtain must be mappable",
                            mLinearInputBlock.block.isMappable());
                    //mBlocks = block;
                    long timestampUs = mMediaExtractor.getSampleTime();
                    int written = mMediaExtractor.readSampleData(mLinearInputBlock.block.map(), 0);
                    mMediaExtractor.advance();
                    boolean signaledEos = mMediaExtractor.getSampleTrackIndex() == -1;
                    Log.d(TAG, "sendData timestampUs = " + timestampUs + ", written = " + written + ", signaledEos = " + signaledEos);

                    if (signaledEos) {
                        playStop();
                        break;
                    } else {
                        mMediaCodecPlayer.WriteTunerInputVideoData(mLinearInputBlock, timestampUs, 0, written);
                    }
                    try {
                        Thread.sleep(20);
                    } catch (Exception e) {
                        e.printStackTrace();
                    }
                }
            }
        }).start();
    }

    private void clearResource() {
        Log.d(TAG, "clearResource, release mMediaExtractor");
        mSending = false;
        if (mMediaExtractor != null)
            mMediaExtractor.release();
    }

    private static void assertTrue(String message, boolean condition) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    @Override
    public void onLocked() {
        Log.d(TAG, "onLocked, try to build psi");
        mUiHandler.sendMessage(mUiHandler.obtainMessage(SetupActivity.UI_MSG_STATUS, "scan locked"));
        if (!"Analog".equals(mScanMode)) {
            Message msg = mTaskHandler.obtainMessage(TaskMsg.TASK_MSG_PULL_SECTION);
            msg.arg1 = 0;
            msg.arg2 = 0;
            mTaskHandler.sendMessage(msg);
        }
    }
    @Override
    public void onScanStopped() {
       Log.d(TAG, "onScanStopped");
    }
    @Override
    public void onProgress(int percent) {
        Log.d(TAG, "onProgress percent:" + percent);
    }
    @Override
    public void onFrequenciesReported(int[] frequency) {
        Log.d(TAG, "onFrequenciesReported");
        if (mTuner != null) {
            if (!mTuner.getFrontendStatus(new int[]{FrontendStatus.FRONTEND_STATUS_TYPE_RF_LOCK}).isRfLocked()) {
                Log.d(TAG, "unlock, should stop");
                mUiHandler.sendMessage(mUiHandler.obtainMessage(SetupActivity.UI_MSG_STATUS, "scan unlock"));
                searchStop();
            } else {
                Log.d(TAG, "locked, waiting to build psi.");
            }
        }
    }
    @Override
    public void onSymbolRatesReported(int[] rate) {
        Log.d(TAG, "onSymbolRatesReported");
    }
    @Override
    public void onPlpIdsReported(int[] plpIds) {
        Log.d(TAG, "onPlpIdsReported");
    }
    @Override
    public void onGroupIdsReported(int[] groupIds) {
        Log.d(TAG, "onGroupIdsReported");
    }
    @Override
    public void onInputStreamIdsReported(int[] inputStreamIds) {
        Log.d(TAG, "onInputStreamIdsReported");
    }
    @Override
    public void onDvbsStandardReported(int dvbsStandard) {
        Log.d(TAG, "onDvbsStandardReported");
    }
    @Override
    public void onDvbtStandardReported(int dvbtStandard) {
        Log.d(TAG, "onDvbtStandardReported");
    }
    @Override
    public void onAnalogSifStandardReported(int sif) {
        Log.d(TAG, "onAnalogSifStandardReported");
    }
    @Override
    public void onAtsc3PlpInfosReported(Atsc3PlpInfo[] atsc3PlpInfos) {
        Log.d(TAG, "onAtsc3PlpInfosReported");
    }
    @Override
    public void onHierarchyReported(int hierarchy) {
        Log.d(TAG, "onHierarchyReported");
    }
    @Override
    public void onSignalTypeReported(int signalType) {
        Log.d(TAG, "onSignalTypeReported");
    }
    public class TunerExecutor implements Executor {
        public void execute(Runnable r) {
            //Log.d(TAG, "Execute run() of r in ui thread");
            if (!mTaskHandler.post(r)) {
                Log.d(TAG, "Execute mTaskHandler is shutting down");
            }
        }
    }

    public class TaskMsg      {
        public final static int TASK_MSG_START_SEARCH = 1;
        public final static int TASK_MSG_STOP_SEARCH = 2;
        public final static int TASK_MSG_START_PLAY = 3;
        public final static int TASK_MSG_STOP_PLAY = 4;
        public final static int TASK_MSG_PULL_SECTION = 5;
        public final static int TASK_MSG_STOP_SECTION = 6;
        public final static int TASK_MSG_SHOW_CHNLIST = 7;
        public final static int TASK_MSG_LOCAL_PLAY_START = 8;
        public final static int TASK_MSG_LOCAL_PLAY_STOP = 9;
    }

    public class DBGProp     {
        public static final String DVR_PROP_MQ_SIZE = "vendor.tf.dvrmq.size";
        public static final String DVR_PROP_READ_TSPKT_NUM = "vendor.tf.dvr.tspkt_num";
        public static final String DVR_PROP_READ_DATA_DURATION = "vendor.tf.dvr.read.duration";
        public static final String DVR_PROP_LOW_THRESHOLD = "vendor.tf.dvr.low_threshold";
        public static final String DVR_PROP_HIGH_THRESHOLD = "vendor.tf.dvr.high_threshold";
        public static final String WVCAS_PROP_LICSERVER = "vendor.media.wvcas.licserver";
        public static final String WVCAS_PROP_PROXY_VENDOR = "vendor.wvcas.proxy.vendor";
        public static final String WVCAS_PROP_CONTENT_TYPE = "vendor.wvcas.content.type";
        public static final String WVCAS_PROP_CUSTOMER_DATA = "vendor.wvcas.customer.data";
        public static final String TF_PROP_ENABLE_PASSTHROUGH = "vendor.tf.enable.passthrough";
        public static final String TF_PROP_ENABLE_FLOW_CONTROL = "vendor.tf.enable.flow_control";
        public static final String TF_PROP_DUMP_ES_DATA = "vendor.tf.dump.es";
        public static final String TF_DEBUG_ENABLE_LOCAL_PLAY = "vendor.tf.enable.localplay";
        public static final String TF_PROP_ENABLE_DVR         = "vendor.tf.enable.dvr";
        public static final String CAS_PROP_EXTEND_ECMTID = "vendor.cas.extend.ecmtid";
    }
    public class ProgramInfo {
        public int freq;
        public int programId;
        public int pmtId;
        public String name;
        public int videoPid;
        public int audioPid;
        public boolean ready;
    }

    public class DscChannelInfo {
        public DscChannelInfo(int esPid, boolean isVideo) {
            mEsPid = esPid;
            mIsVideo = isVideo;
        }
        public int mEsPid = 0x1fff;
        private int mEcmPid = 0x1fff;
        public int mEcmTableId;
        public Filter[] mEcmSectionFilter = new Filter[MAX_CAS_ECM_TID_NUM];
        private int mCryptoMode = CryptoMode.kInvalid;
        private int mSessionUsage = MediaCas.SESSION_USAGE_LIVE;
        private int mScramblingMode = MediaCas.SCRAMBLING_MODE_DVB_CSA2;
        public boolean mGetCryptoMode = false;
        public boolean mIsVideo;
        private byte[] mCasKeyToken = null;
        private MediaCas.Session mCasSession = null;
        private AtomicBoolean mSessionOpened = new AtomicBoolean(false);
        private int mCasSessionId = -1;
        public void setEcmPid(int pid) {
            mEcmPid = pid;
        }
        public int getEcmPid() {
            return mEcmPid;
        }
    }
    protected class SectionHeaderInfo {
        public int mTableId = -1;
        public int mSectionSyntaxIndicator;
        public int mZero;
        public int mReserved_1;
        public int mSectionLength;
        public int mTransportStreamId;
        public int mReserved_2;
        public int mVersionNumber;
        public int mCurrentNextIndicator;
        public int mSectionNumber;
        public int mLastSectionNumber;
    }

    public class PatProgramInfo{
        public PatProgramInfo(int programNo, int pmtPid) {
            mProgramNumber = programNo;
            mPmtPid = pmtPid;
        }
        public int mProgramNumber;
        public int mPmtPid;
    }

    protected class PatInfo extends SectionHeaderInfo {
        ArrayList<PatProgramInfo> mPrograms = new ArrayList<PatProgramInfo>();
        public int mReserved_3;
        public int mNetworkPid;
        public int mCRC_32;
    }

    public class PmtStreamInfo     {
        public PmtStreamInfo(int streamType, int esPid, int ecmPid, boolean isVideo) {
            mStreamType = streamType;
            mEsPid = esPid;
            mEcmPid = ecmPid;
            mIsVideo = isVideo;
        }
        public int mStreamType;
        public int mEsPid  = 0x1ffff;
        public int mEcmPid = 0x1fff;
        public boolean mIsVideo;
    }

    protected class PmtInfo extends SectionHeaderInfo{
        public int mReserved_3;
        public int mPcrPid;
        public int mReserved_4;
        public int mProgramInfoLength;
        ArrayList<PmtStreamInfo> mPmtStreams = new ArrayList<PmtStreamInfo>();
        public int mReserved_5;
        public int mReserved_6;
        public int mCRC_32;
        public int mCaSystemId = -1;
        public int mCaPid = 0x1fff;
        public int mPrivateDataLen;
        private byte[] mPrivateData = null;
        private int mDscMode = 0;//NV_MCAS_SCRAMBLING_MODE_RESERVED
    }

    protected class FlowControl {
        public FlowControl(int duration,     long percentage) {
            mDuration = duration;
            mPreDuration = duration;
            mPercentage = percentage;
            mPrePercentage = percentage;
        }
        private static final int DVRMQ_SPACE_EMPTY = 0;
        private static final int DVRMQ_SPACE_ALMOST_EMPTY = 2;
        private static final int DVRMQ_SPACE_ALMOST_FULL = 4;
        private static final int DVRMQ_SPACE_FULL = 8;

        private int mDuration;
        private int mPreDuration;
        private long mPercentage;
        private long mPrePercentage;
        private int mStatus;

        public void updateDuration(long percentage, int status) {
            mPercentage = percentage;
            mStatus = status;

            if (mEnableFlowCtl) {
                if (mPrePercentage > mPercentage &&
                    mPercentage <= MIN_DECODER_BUFFER_FREE_THRESHOLD &&
                    mDuration < MAX_DVR_READ_DURATION_MS / 4) {
                    mDuration = mDuration * 2;
                    //Log.d(TAG, "mPercentage:" + mPercentage + " mPrePercentage:" + mPercentage);
                    if ((mPercentage <= MIN_DECODER_BUFFER_FREE_THRESHOLD / 2 && mDuration < MAX_DVR_READ_DURATION_MS / 2) ||
                        (mPercentage <= MIN_DECODER_BUFFER_FREE_THRESHOLD / 4 && mDuration < MAX_DVR_READ_DURATION_MS)) {
                        mDuration = mDuration * 2;
                        //Log.d(TAG, "mPercentage:" + mPercentage + " mPrePercentage:" + mPercentage);
                    }
                }
                if (mPrePercentage < mPercentage  &&
                    mPercentage >= MAX_DECODER_BUFFER_FREE_THRESHOLD / 2 &&
                    mPercentage < MAX_DECODER_BUFFER_FREE_THRESHOLD &&
                    (mDuration >= 2 * DEFAULT_DVR_READ_DURATION_MS)) {
                    //Log.d(TAG, "mPercentage:" + mPercentage + " mPrePercentage:" + mPercentage);
                    mDuration = mDuration / 2;
                }
                if ((mPercentage >= MAX_DECODER_BUFFER_FREE_THRESHOLD) &&
                    (mDuration != DEFAULT_DVR_READ_DURATION_MS)) {
                    mDuration = DEFAULT_DVR_READ_DURATION_MS;
                    Log.d(TAG, "Reset duration to " + DEFAULT_DVR_READ_DURATION_MS + " with mPercentage:" + mPercentage);
                }
                if (mStatus == DVRMQ_SPACE_ALMOST_FULL)
                    mDuration = MAX_DVR_READ_DURATION_MS / 2;
                else if (mStatus == DVRMQ_SPACE_FULL)
                    mDuration = MAX_DVR_READ_DURATION_MS;
                if (mPreDuration != mDuration) {
                    mPreDuration = mDuration;
                    Log.d(TAG, "mDuration:" + mDuration + "ms");
                }
                if (mPrePercentage != mPercentage)
                    mPrePercentage = mPercentage;
            }else {
                switch (mStatus) {
                    case DVRMQ_SPACE_EMPTY:
                    case DVRMQ_SPACE_ALMOST_EMPTY:
                        setDuration(DEFAULT_DVR_READ_DURATION_MS);
                        break;
                    case DVRMQ_SPACE_ALMOST_FULL:
                        setDuration(MAX_DVR_READ_DURATION_MS / 2);
                        break;
                    case DVRMQ_SPACE_FULL:
                        setDuration(MAX_DVR_READ_DURATION_MS);
                        break;
                    default:
                        Log.e(TAG, "Unknown status! mStatus: " + mStatus);
                }
            }
        };

        public void setDuration(int duration) {
            if (mDuration != duration) {
                Log.d(TAG, "Set mDuration to " + duration + " ms");
                mDuration = duration;
            }
        }
        public int getDuration() {
            return mDuration;
        }
        public void setPercentage(long percentage) {
            mPercentage = percentage;
        }
        public long getPercentage() {
            return mPercentage;
        }
    }
}
