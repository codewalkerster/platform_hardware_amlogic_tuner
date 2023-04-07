package com.droidlogic.tunerframeworksetup;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemSelectedListener;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.Spinner;
import android.widget.TextView;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;

import com.droidlogic.app.SystemControlManager;
import com.droidlogic.tunerframeworksetup.SetupInstance.TaskMsg;
import com.droidlogic.tunerframeworksetup.SetupInstance.DBGProp;
import java.lang.System;

public class SetupActivity extends Activity {

    private final static String TAG = SetupActivity.class.getSimpleName();

    public final static int MAX_INSTANCES = 2;
    public final static int UI_MSG_STATUS = 1;

    private Spinner mSpinnerStreamMode = null;
    private Spinner mSpinnerScanMode = null;
    private LinearLayout mLayoutFrequency = null;
    private LinearLayout mLayoutScanmode = null;
    private LinearLayout mLayoutSymbol = null;
    private EditText mFrequency = null;
    private EditText mVideoPid = null;//Unused
    private EditText mAudioPid = null;//Unused
    private EditText mSymbol = null;
    private Button mSearchStart = null;
    private Button mSearchStop = null;
    private Button mPlayStart = null;
    private Button mPlayStop = null;
    private TextView mStatus = null;

    private SurfaceView[] mPlayerViews = new SurfaceView[MAX_INSTANCES];
    private LinearLayout[] mLayoutLocalFiles = new LinearLayout[MAX_INSTANCES];
    private LinearLayout[] mLayoutChannelIds = new LinearLayout[MAX_INSTANCES];
    private EditText[] mChannelIds = new EditText[MAX_INSTANCES];
    private EditText[] mTsFiles = new EditText[MAX_INSTANCES];
    private Button[] mLocalPlayStarts = new Button[MAX_INSTANCES];
    private Button[] mLocalPlayStops = new Button[MAX_INSTANCES];
    private Surface[] mSurfaces = new Surface[MAX_INSTANCES];
    private RelativeLayout[] mSurfaceLayouts = new RelativeLayout[MAX_INSTANCES];

    private SingleClickListener mSingleClickListener = null;
    private MultiClickListener[] mMultiClickListeners = new MultiClickListener[MAX_INSTANCES];

    private Handler mUiHandler = null;

    private SetupInstance[] mInstances = new SetupInstance[MAX_INSTANCES];

    private boolean mEnableLocalPlay = true;

    private String mStreamMode = null;
    private String mScanMode = "Dvbt";

    private static final String TF_DSCTYPE_PROP = "vendor.media.tunerhal.dsc_type";
    private static final String TSN_SOURCE_NODE = "/sys/class/stb/tsn_source";
    private static final String TSN_SOURCE_LOCAL = "local";
    private static final String TSN_SOURCE_DEMOD = "demod";
    private static final int CA_DSC_COMMON_TYPE = 0;
    private static final int CA_DSC_TSD_TYPE = 1;

    private SystemControlManager mSystemControlManager = null;
    private String mTsnSource = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        setContentView(R.layout.activity_main);
        mSystemControlManager = SystemControlManager.getInstance();
        mTsnSource = getTsnSource();
        initView();
        initListener();
        initHandler();
        setPlaybackParas();
        for (int instanceId = 0; instanceId < MAX_INSTANCES; instanceId ++) {
            mInstances[instanceId] = new SetupInstance(instanceId, this, mPlayerViews[instanceId]);
        }
    }

    @Override
    protected void onResume() {
        Log.d(TAG, "onResume");
        setPlaybackParas();
        setDscMode(CA_DSC_COMMON_TYPE);
        if (mStreamMode != null) {
            String requireTsnSource = mStreamMode.equals("local") ? TSN_SOURCE_LOCAL : TSN_SOURCE_DEMOD;
            setTsnSource(requireTsnSource);
        }
        super.onResume();
    }

    @Override
    protected void onPause() {
        Log.d(TAG, "onPause");
            super.onPause();
        for (int instanceId = 0; instanceId < MAX_INSTANCES; instanceId ++) {
            if (mInstances[instanceId] != null) {
                mInstances[instanceId].pause();
            }
        }
        Log.d(TAG, "onPause end");
    }

    @Override
    protected void onStop() {
        Log.d(TAG, "onStop");
        setDscMode(CA_DSC_TSD_TYPE);
        setTsnSource(mTsnSource);
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        Log.d(TAG, "onDestroy");
        mSingleClickListener = null;

        for (int instanceId = 0; instanceId < MAX_INSTANCES; instanceId ++) {
            mMultiClickListeners[instanceId] = null;
        }

        for (int instanceId = 0; instanceId < MAX_INSTANCES; instanceId++) {
            if (mInstances[instanceId] != null) {
                mInstances[instanceId].destroy();
                mInstances[instanceId] = null;
            }
        }
        releaseHandler();
        super.onDestroy();
        Log.d(TAG, "onDestroy end");
    }

    private void initView() {
        Log.d(TAG, "Init view components");

        mSpinnerStreamMode = (Spinner)findViewById(R.id.spinner_stream_mode);
        mLayoutFrequency = (LinearLayout)findViewById(R.id.layout_frequency);
        mLayoutScanmode = (LinearLayout)findViewById(R.id.layout_scanmode);
        mLayoutSymbol = (LinearLayout)findViewById(R.id.layout_symbol);
        mFrequency = (EditText)findViewById(R.id.frequency);
        mVideoPid = (EditText)findViewById(R.id.video_pid);//Unused
        mAudioPid = (EditText)findViewById(R.id.audio_pid);//Unused
        mSymbol = (EditText)findViewById(R.id.edit_symbol);
        mSpinnerScanMode = (Spinner)findViewById(R.id.spinner_scan_mode);
        mSearchStart = (Button)findViewById(R.id.search_start);
        mSearchStop = (Button)findViewById(R.id.search_stop);
        mPlayStart = (Button)findViewById(R.id.play_start);
        mPlayStop = (Button)findViewById(R.id.play_stop);
        mStatus = (TextView)findViewById(R.id.status);

        if (MAX_INSTANCES >= 1) {
            mPlayerViews[0] = (SurfaceView)findViewById(R.id.play_view);
            mPlayerViews[0].setVisibility(View.INVISIBLE);
            mLayoutLocalFiles[0] = (LinearLayout)findViewById(R.id.layout_local_file);
            mTsFiles[0] = (EditText)findViewById(R.id.ts_file);
            mLayoutChannelIds[0] = (LinearLayout)findViewById(R.id.layout_channel_id);
            mChannelIds[0] = (EditText)findViewById(R.id.channel_id);
            mLocalPlayStarts[0] = (Button)findViewById(R.id.local_play_start);
            mLocalPlayStops[0] = (Button)findViewById(R.id.local_play_stop);
            mSurfaceLayouts[0] = (RelativeLayout)findViewById(R.id.rl_main);
        }
        if (MAX_INSTANCES >= 2) {
            mPlayerViews[1] = (SurfaceView)findViewById(R.id.play_view_flt);
            mPlayerViews[1].setVisibility(View.INVISIBLE);
            mLayoutLocalFiles[1] = (LinearLayout)findViewById(R.id.layout_local_file2);
            mTsFiles[1] = (EditText)findViewById(R.id.ts_file2);
            mLayoutChannelIds[1] = (LinearLayout)findViewById(R.id.layout_channel_id2);
            mChannelIds[1] = (EditText)findViewById(R.id.channel_id2);
            mLocalPlayStarts[1] = (Button)findViewById(R.id.local_play_start2);
            mLocalPlayStops[1] = (Button)findViewById(R.id.local_play_stop2);
            mSurfaceLayouts[1] = (RelativeLayout)findViewById(R.id.rl_flt);
        }

        mFrequency.setText(getParameter("frequency"));
        mVideoPid.setText(getParameter("video"));//Unused
        mAudioPid.setText(getParameter("audio"));//Unused
        if (MAX_INSTANCES >= 1) {
            mChannelIds[0].setText(getParameter("channel"));
            mTsFiles[0].setText(getParameter("ts"));
        }
        if (MAX_INSTANCES >= 2) {
            mChannelIds[1].setText(getParameter("channel2"));
            mTsFiles[1].setText(getParameter("ts2"));
        }
    }

    private void updateParameters() {
        Log.d(TAG, "Update parameters for edit texts");
        putParameter("frequency", mFrequency.getText().toString());
        putParameter("video", mVideoPid.getText().toString());
        putParameter("audio", mAudioPid.getText().toString());
        putParameter("mode", "" + mSpinnerScanMode.getSelectedItemPosition());
        if (MAX_INSTANCES >= 1) {
            putParameter("ts", mTsFiles[0].getText().toString());
            putParameter("channel", mChannelIds[0].getText().toString());
        }
        if (MAX_INSTANCES >= 2) {
            putParameter("ts2", mTsFiles[1].getText().toString());
            putParameter("channel2", mChannelIds[1].getText().toString());
        }
    }

    private void setPlaybackParas() {
        String mEnablePassthrough = Utils.getPropString("getprop " + DBGProp.TF_PROP_ENABLE_PASSTHROUGH);
        if (mEnablePassthrough != null && mEnablePassthrough.length() > 0)
            SetupInstance.mPassthroughMode = Integer.parseInt(mEnablePassthrough) == 0 ? false : true;
        Log.d(TAG, "mPassthroughMode: " + SetupInstance.mPassthroughMode);

        String mEnableFlowCtlStr = Utils.getPropString("getprop " + DBGProp.TF_PROP_ENABLE_FLOW_CONTROL);
        if (mEnableFlowCtlStr != null && mEnableFlowCtlStr.length() > 0)
            SetupInstance.mEnableFlowCtl = Integer.parseInt(mEnableFlowCtlStr) == 0 ? false : true;
        Log.d(TAG, "mEnableFlowCtl: " + SetupInstance.mEnableFlowCtl);

        String mEnableDumpEs = Utils.getPropString("getprop " + DBGProp.TF_PROP_DUMP_ES_DATA);
        if (mEnableDumpEs != null && mEnableDumpEs.length() > 0)
            SetupInstance.mDumpVideoEs = Integer.parseInt(mEnableDumpEs) == 0 ? false : true;
        Log.d(TAG, "mDumpVideoEs: " + SetupInstance.mDumpVideoEs);

        String mDvrMQSizeStr = Utils.getPropString("getprop " + DBGProp.DVR_PROP_MQ_SIZE);
        if (mDvrMQSizeStr != null && mDvrMQSizeStr.length() > 0)
            SetupInstance.mDvrMQSize_MB = Integer.parseInt(mDvrMQSizeStr);
        Log.d(TAG, "mDvrMQSize_MB: " + SetupInstance.mDvrMQSize_MB + "MB");

        String mDvrLowThresholdStr = Utils.getPropString("getprop " + DBGProp.DVR_PROP_LOW_THRESHOLD);
        if (mDvrLowThresholdStr != null && mDvrLowThresholdStr.length() > 0)
            SetupInstance.mDvrLowThreshold = Integer.parseInt(mDvrLowThresholdStr);
        else
            SetupInstance.mDvrLowThreshold = SetupInstance.mDvrMQSize_MB * 1024 * 1024 * 2 / 10;
        Log.d(TAG, "mDvrLowThreshold: " + SetupInstance.mDvrLowThreshold);

        String mDvrHighThresholdStr = Utils.getPropString("getprop " + DBGProp.DVR_PROP_HIGH_THRESHOLD);
        if (mDvrHighThresholdStr != null && mDvrHighThresholdStr.length() > 0)
            SetupInstance.mDvrHighThreshold = Integer.parseInt(mDvrHighThresholdStr);
        else
            SetupInstance.mDvrHighThreshold = SetupInstance.mDvrMQSize_MB * 1024 * 1024 * 8 / 10;
        Log.d(TAG, "mDvrHighThreshold: " + SetupInstance.mDvrHighThreshold);

        String enableDvr = Utils.getPropString("getprop " + DBGProp.TF_PROP_ENABLE_DVR);
        if (enableDvr != null && enableDvr.length() > 0)
            SetupInstance.mEnableDvr = Integer.parseInt(enableDvr) == 0 ? false : true;
        Log.d(TAG, "mEnableDvr: " + SetupInstance.mEnableDvr);

        String enableExtendEcmTid = Utils.getPropString("getprop " + DBGProp.CAS_PROP_EXTEND_ECMTID);
        if (enableExtendEcmTid != null && enableExtendEcmTid.length() > 0)
            SetupInstance.mEnableExtendEcmTid = Integer.parseInt(enableExtendEcmTid) == 0 ? false : true;
        Log.d(TAG, "mEnableExtendEcmTid: " + SetupInstance.mEnableExtendEcmTid);
    }

    //Use for SC2 support TSD
    public void setDscMode(int dscMode) {
        if (mSystemControlManager == null)
            mSystemControlManager = SystemControlManager.getInstance();
        if (mSystemControlManager != null) {
            mSystemControlManager.setProperty(TF_DSCTYPE_PROP, Integer.toString(dscMode));
        } else {
            Log.e(TAG, "Can not get mSystemControlManager!");
            return;
        }
    }

    public int getDscMode() {
        int dscMode = -1;
        if (mSystemControlManager == null)
            mSystemControlManager = SystemControlManager.getInstance();
        if (mSystemControlManager != null) {
            dscMode = mSystemControlManager.getPropertyInt(TF_DSCTYPE_PROP, 0);
            Log.d(TAG, "Get dscMode is " + dscMode);
        } else {
            Log.e(TAG, "Can not get mSystemControlManager!");
            return -1;
        }
        return dscMode;
    }

    public void setTsnSource(String requireTsnSource) {
        String tsnSource = null;
        if (mSystemControlManager == null)
            mSystemControlManager = SystemControlManager.getInstance();
        if (mSystemControlManager != null) {
            tsnSource = mSystemControlManager.readSysFs(TSN_SOURCE_NODE);
            Log.d(TAG, "Current tsnSource is " + tsnSource);
        } else {
            Log.e(TAG, "Can not get mSystemControlManager!");
            return;
        }
        if (!tsnSource.contains(requireTsnSource)) {
            Log.d(TAG, "Set tsnSource to " + requireTsnSource);
            mSystemControlManager.writeSysFs(TSN_SOURCE_NODE, requireTsnSource);
        }
    }

    public String getTsnSource() {
        String tsnSource = null;
        if (mSystemControlManager == null)
            mSystemControlManager = SystemControlManager.getInstance();
        if (mSystemControlManager != null) {
            tsnSource = mSystemControlManager.readSysFs(TSN_SOURCE_NODE);
            Log.d(TAG, "Get tsnSource is " + tsnSource);
        } else {
            Log.e(TAG, "Can not get mSystemControlManager!");
            return null;
        }
        if (tsnSource.contains(TSN_SOURCE_LOCAL)) {
            tsnSource = TSN_SOURCE_LOCAL;
        } else if (tsnSource.contains(TSN_SOURCE_DEMOD)) {
            tsnSource = TSN_SOURCE_DEMOD;
        }
        return tsnSource;
    }

    public Handler getUiHandler() {
        return mUiHandler;
    }

    public String getFrequency() {
        return mFrequency.getText().toString();
    }

    //Unused
    public String getVideoPid() {
        return mVideoPid.getText().toString();
    }

    //Unused
    public String getAudioPid() {
        return mAudioPid.getText().toString();
    }

    private void initListener() {
        Log.d(TAG, "initListener");
        mSingleClickListener = new SingleClickListener();
        mSearchStart.setOnClickListener(mSingleClickListener);
        mSearchStop.setOnClickListener(mSingleClickListener);
        mPlayStart.setOnClickListener(mSingleClickListener);
        mPlayStop.setOnClickListener(mSingleClickListener);

        for (int instanceId = 0; instanceId < MAX_INSTANCES; instanceId ++) {
            mMultiClickListeners[instanceId] = new MultiClickListener(instanceId);
            mLocalPlayStarts[instanceId].setOnClickListener(mMultiClickListeners[instanceId]);
            mLocalPlayStops[instanceId].setOnClickListener(mMultiClickListeners[instanceId]);
        }

        mSpinnerStreamMode.setOnItemSelectedListener(new OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> adapterView, View view, int i, long l) {
                boolean localMode = (i == 0);
                Log.d(TAG, "onItemSelected localMode:" + localMode);
                enableLocalMode(localMode);
                int tunerVisible = View.GONE;
                int localVisible = View.VISIBLE;
                if (!localMode) {
                    tunerVisible = View.VISIBLE;
                    localVisible = View.GONE;
                }
                mLayoutFrequency.setVisibility(tunerVisible);
                mLayoutScanmode.setVisibility(tunerVisible);
                mSearchStart.setVisibility(tunerVisible);
                mPlayStop.setVisibility(tunerVisible);
                mLayoutChannelIds[0].setVisibility(View.VISIBLE);

                mLayoutSymbol.setVisibility(View.GONE);
                if (!localMode) {
                    if ("Dvbc".equals(mScanMode)) {
                        mLayoutSymbol.setVisibility(View.VISIBLE);
                        mSymbol.setHint(6900);
                    } else if ("Dvbs".equals(mScanMode)) {
                        mLayoutSymbol.setVisibility(View.VISIBLE);
                        mSymbol.setHint(27500);
                    }
                }
                mStatus.setVisibility(tunerVisible);
                for (int instanceId = 0; instanceId < MAX_INSTANCES; instanceId ++) {
                    mLayoutLocalFiles[instanceId].setVisibility(localVisible);
                    mLocalPlayStarts[instanceId].setVisibility(localVisible);
                    mLocalPlayStops[instanceId].setVisibility(localVisible);
                    if (instanceId > 0)
                        mLayoutChannelIds[instanceId].setVisibility(localVisible);
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> adapterView) {
            }
        });

        mSpinnerScanMode.setOnItemSelectedListener(new OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> adapterView, View view, int i, long l) {
                mScanMode = getResources().getStringArray(R.array.scan_modes)[i];
                //Log.d(TAG, "onItemSelected mScanMode:" + mScanMode);
                if ("Dvbc".equals(mScanMode)) {
                    mLayoutSymbol.setVisibility(View.VISIBLE);
                    mSymbol.setHint("6900");
                } else if ("Dvbs".equals(mScanMode)) {
                    mLayoutSymbol.setVisibility(View.VISIBLE);
                    mSymbol.setHint("27500");
                } else {
                    mLayoutSymbol.setVisibility(View.GONE);
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> adapterView) {
            }
        });
        int selection = 0;
        try {
            selection = Integer.parseInt(getParameter("mode"));
        } catch (Exception e) {
        }
        mSpinnerScanMode.setSelection(selection);
    }

    private void enableLocalMode(boolean enable) {
        Log.d(TAG, "enableLocalMode:" + enable);
        if (enable) {
            mStreamMode = "local";
            mEnableLocalPlay = true;
        } else {
            mStreamMode = "tuner";
            mEnableLocalPlay = false;
        }
        String requireTsnSource = mStreamMode.equals("local") ? TSN_SOURCE_LOCAL : TSN_SOURCE_DEMOD;
        setTsnSource(requireTsnSource);
    }

    private class SingleClickListener extends MultiClickListener {
        public SingleClickListener() {
            super(0);
        }
    }

    private class MultiClickListener implements View.OnClickListener {
        private int mInstance = 0;

        public MultiClickListener(int instance) {
            mInstance = instance;
        }

        @Override
        public void onClick(View view) {
            boolean needUpdate = true;
            switch (view.getId()) {
                case R.id.search_start:
                    Log.d(TAG, "Click search start " + mInstance);
                    mPlayerViews[mInstance].setVisibility(View.VISIBLE);
                    mUiHandler.sendMessage(mUiHandler.obtainMessage(UI_MSG_STATUS, "search_start"));
                    mInstances[mInstance].setFrequency(Integer.parseInt(mFrequency.getText().toString()));
                    if ("Dvbc".equals(mScanMode) || "Dvbs".equals(mScanMode))
                        mInstances[mInstance].setSymbol(Integer.parseInt(mSymbol.getText().toString()));
                    mInstances[mInstance].setEnableLocalPlay(mEnableLocalPlay);
                    mInstances[mInstance].setProgramId(Integer.parseInt(mChannelIds[mInstance].getText().toString()));
                    mInstances[mInstance].getTaskHandler().sendEmptyMessage(TaskMsg.TASK_MSG_START_SEARCH);
                    break;
                case R.id.search_stop:
                    Log.d(TAG, "Click search stop");
                    mUiHandler.sendMessage(mUiHandler.obtainMessage(UI_MSG_STATUS, "search_stop"));
                    mInstances[mInstance].getTaskHandler().sendEmptyMessage(TaskMsg.TASK_MSG_STOP_SEARCH);
                    break;
                case R.id.play_start:
                    Log.d(TAG, "Click play start");
                    mUiHandler.sendMessage(mUiHandler.obtainMessage(UI_MSG_STATUS, "play_start"));
                    mInstances[mInstance].setFrequency(Integer.parseInt(mFrequency.getText().toString()));
                    mInstances[mInstance].setSymbol(Integer.parseInt(mSymbol.getText().toString()));
                    mInstances[mInstance].setProgramId(Integer.parseInt(mChannelIds[mInstance].getText().toString()));
                    mInstances[mInstance].getTaskHandler().sendEmptyMessage(TaskMsg.TASK_MSG_START_PLAY);
                    break;
                case R.id.play_stop:
                    Log.d(TAG, "Click play stop");
                    mPlayerViews[mInstance].setVisibility(View.GONE);
                    mUiHandler.sendMessage(mUiHandler.obtainMessage(UI_MSG_STATUS, "play_stop"));
                    mInstances[mInstance].getTaskHandler().sendEmptyMessage(TaskMsg.TASK_MSG_STOP_PLAY);
                    break;
                case R.id.local_play_start:
                case R.id.local_play_start2:
                    Log.d(TAG, "Click local play start");
                    mUiHandler.sendMessage(mUiHandler.obtainMessage(UI_MSG_STATUS, "local_play_start"));
                    mInstances[mInstance].setLocalTsFileForPlayback(mTsFiles[mInstance].getText().toString());
                    mInstances[mInstance].setProgramId(Integer.parseInt(mChannelIds[mInstance].getText().toString()));
                    mInstances[mInstance].setEnableLocalPlay(mEnableLocalPlay);
                    mInstances[mInstance].getTaskHandler().sendEmptyMessage(TaskMsg.TASK_MSG_LOCAL_PLAY_START);
                    if (mInstance == 1)
                        mSurfaceLayouts[mInstance].setVisibility(View.VISIBLE);
                    mPlayerViews[mInstance].setVisibility(View.VISIBLE);
                    //if (mInstance == 1) {
                        //mPlayerViews[mInstance].setZOrderOnTop(true);
                        //mPlayerViews[mInstance].setZOrderMediaOverlay(true);
                    //}

                    break;
                case R.id.local_play_stop:
                case R.id.local_play_stop2:
                    Log.d(TAG, "Click local play stop");
                    if (mInstance == 1)
                        mSurfaceLayouts[mInstance].setVisibility(View.INVISIBLE);
                    mPlayerViews[mInstance].setVisibility(View.GONE);
                    mUiHandler.sendMessage(mUiHandler.obtainMessage(UI_MSG_STATUS, "local_play_stop"));
                    mInstances[mInstance].getTaskHandler().sendEmptyMessage(TaskMsg.TASK_MSG_LOCAL_PLAY_STOP);
                    break;
                default:
                    needUpdate = false;
                    break;
            }

            if (needUpdate) {
                updateParameters();
            }
        }
    }

    private class TaskHandler extends Handler {
        public TaskHandler(Looper looper, Callback callback) {
            super(looper, callback);
        }
    }

    private class UiHandlerCallback implements Handler.Callback {

        @Override
        public boolean handleMessage(Message message) {
            boolean result = true;
            Log.d(TAG, "UiHandlerCallback handleMessage:" + message.what);
            switch (message.what) {
                case UI_MSG_STATUS:
                    mStatus.setText((String)message.obj);
                    break;
                default:
                    result = false;
                    break;
            }
            return result;
        }
    }

    private void initHandler() {
        Log.d(TAG, "Create UiHandler and UiHandlerCallback");
        mUiHandler = new TaskHandler(getMainLooper(), new UiHandlerCallback());
    }

    private void releaseHandler() {
        Log.d(TAG, "mUiHandler removeCallbacksAndMessages");
        if (mUiHandler != null) {
            mUiHandler.removeCallbacksAndMessages(null);
            mUiHandler = null;
        }
    }

    private String getParameter(String key) {
        String result = null;
        result = getSharedPreferences("parameter", 0).getString(key, "");
        return result;
    }

    private void putParameter(String key, String value) {
        getSharedPreferences("parameter", 0).edit().putString(key, value).apply();
    }

}
