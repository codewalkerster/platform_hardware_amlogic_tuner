package com.droidlogic.tuner.setup;

import android.os.Bundle;

import androidx.fragment.app.Fragment;

import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.SimpleAdapter;
import android.widget.Spinner;

import com.droidlogic.tuner.R;
import com.droidlogic.tuner.scan.DvbtScanManager;
import com.droidlogic.tuner.scan.ScanManager;
import com.droidlogic.tuner.utils.Constants;
import com.droidlogic.tuner.utils.ThreadManager;


/**
 * A simple {@link Fragment} subclass.
 * Use the {@link FragmentDvbtSetup#newInstance} factory method to
 * create an instance of this fragment.
 */
public class FragmentDvbtSetup extends Fragment {
    private static final String ARG_PARAM1 = "param1";
    private static final String TAG = Constants.TAG;

    private EditText mEditFrequency = null;
    private CheckBox mSwitchStandard = null;
    private Spinner mSpinnerBandwidth = null;
    private Spinner mSpinnerTransmissionMode = null;
    private Button mButtonScan = null;

    private String mTag;

    public FragmentDvbtSetup() {
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param tag Parameter 1.
     * @return A new instance of fragment FragmentDvbtSetup.
     */
    // TODO: Rename and change types and number of parameters
    static FragmentDvbtSetup newInstance(String tag) {
        FragmentDvbtSetup fragment = new FragmentDvbtSetup();
        Bundle args = new Bundle();
        args.putString(ARG_PARAM1, tag);
        fragment.setArguments(args);
        return fragment;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.i(TAG, "onCreate");
        if (getArguments() != null) {
            mTag = getArguments().getString(ARG_PARAM1);
        }
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        Log.i(TAG, "onCreateView");
        View view = inflater.inflate(R.layout.fragment_dvbt_setup, container, false);
        mEditFrequency = view.findViewById(R.id.frequency_value);
        mSpinnerBandwidth = view.findViewById(R.id.bw_spinner);
        DvbtScanManager dvbtScanManager = (DvbtScanManager) ScanManager.getInstance()
                    .getSession(ScanManager.SIGNAL_TYPE_DVBT);
        SimpleAdapter bwAdapter = new SimpleAdapter(getActivity(),
                dvbtScanManager.getBandWidthSettings(),
                R.layout.simple_list_item,
                new String[] {"name"}, new int[] {R.id.name});
        mSpinnerBandwidth.setAdapter(bwAdapter);
        mSpinnerTransmissionMode = view.findViewById(R.id.ts_spinner);
        SimpleAdapter tsAdapter = new SimpleAdapter(getActivity(),
                dvbtScanManager.getTransmissionModeSettings(),
                R.layout.simple_list_item,
                new String[] {"name"}, new int[] {R.id.name});
        mSpinnerTransmissionMode.setAdapter(tsAdapter);
        mButtonScan = view.findViewById(R.id.button_start);
        mButtonScan.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startScan();
            }
        });
        mSwitchStandard = view.findViewById(R.id.dvbt2Switch);
        return view;
    }

    @Override
    public void onDestroyView() {
        mEditFrequency = null;
        mSpinnerBandwidth = null;
        mSpinnerTransmissionMode = null;
        mButtonScan = null;
        mSwitchStandard = null;
        super.onDestroyView();
    }

    private void startScan() {
        int freqMhz = 0;
        final Bundle bundle = new Bundle();

        if (mEditFrequency != null) {
            String freqStr = mEditFrequency.getText().toString();
            try {
                freqMhz = Integer.parseInt(freqStr);
            } catch (Exception e) {
                freqMhz = 490;
            }
            mEditFrequency.setEnabled(false);
            mSpinnerBandwidth.setEnabled(false);
            mSpinnerTransmissionMode.setEnabled(false);
            mButtonScan.setEnabled(false);
            mButtonScan.setText("Scanning");
            bundle.putInt(ScanManager.KEY_FREQUENCY, freqMhz);
            bundle.putInt(DvbtScanManager.KEY_BAND_WIDTH,
                    mSpinnerBandwidth.getSelectedItemPosition());
            bundle.putInt(DvbtScanManager.KEY_TRANSMISSION_MODE,
                    mSpinnerTransmissionMode.getSelectedItemPosition());
            bundle.putBoolean(DvbtScanManager.KEY_IS_T2_MODE, mSwitchStandard.isChecked());
        }
        final int finalFreqMhz = freqMhz;
        Log.d(TAG, "start dvbt scan, param:" + bundle.toString());
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                ScanManager.getInstance().startScan(
                        requireActivity().getApplicationContext(), finalFreqMhz, bundle);
            }
        }, 0);
    }
}
