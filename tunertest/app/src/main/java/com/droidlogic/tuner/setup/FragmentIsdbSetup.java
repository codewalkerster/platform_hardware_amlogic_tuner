package com.droidlogic.tuner.setup;

import android.os.Bundle;

import androidx.fragment.app.Fragment;

import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SimpleAdapter;
import android.widget.Spinner;

import com.droidlogic.tuner.R;
import com.droidlogic.tuner.scan.IsdbtScanManager;
import com.droidlogic.tuner.scan.ScanManager;
import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.ThreadManager;

/**
 * A simple {@link Fragment} subclass.
 * Use the {@link FragmentIsdbSetup#newInstance} factory method to
 * create an instance of this fragment.
 */
public class FragmentIsdbSetup extends Fragment {
    // TODO: Rename parameter arguments, choose names that match
    // the fragment initialization parameters, e.g. ARG_ITEM_NUMBER
    private static final String ARG_PARAM1 = "param1";
    private static final String ARG_PARAM2 = "param2";
    private static final String TAG = Constans.TAG;
    private EditText mEditFrequency;
    private Spinner mSpinnerModulation;
    private Spinner mSpinnerBandwidth;
    private Button mButtonScan;

    // TODO: Rename and change types of parameters
    private String mParam1;
    private String mParam2;

    public FragmentIsdbSetup() {
        // Required empty public constructor
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param param1 Parameter 1.
     * @param param2 Parameter 2.
     * @return A new instance of fragment FragmentIsdbSetup.
     */
    // TODO: Rename and change types and number of parameters
    public static FragmentIsdbSetup newInstance(String param1, String param2) {
        FragmentIsdbSetup fragment = new FragmentIsdbSetup();
        Bundle args = new Bundle();
        args.putString(ARG_PARAM1, param1);
        args.putString(ARG_PARAM2, param2);
        fragment.setArguments(args);
        return fragment;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (getArguments() != null) {
            mParam1 = getArguments().getString(ARG_PARAM1);
            mParam2 = getArguments().getString(ARG_PARAM2);
        }
    }

    @Override
    public void onDestroy() {
        mEditFrequency = null;
        mSpinnerModulation = null;
        mSpinnerBandwidth = null;
        mButtonScan = null;
        super.onDestroy();
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        View view = inflater.inflate(R.layout.fragment_isdb_setup, container, false);
        if (view != null) {
            mEditFrequency = view.findViewById(R.id.frequency_value);
            mSpinnerModulation = view.findViewById(R.id.modulation_spinner);
            mSpinnerBandwidth = view.findViewById(R.id.bandwidth_spinner);
            mButtonScan = view.findViewById(R.id.button_start);
            IsdbtScanManager isdbtScanManager = (IsdbtScanManager) ScanManager.getInstance()
                    .getSession(ScanManager.SIGNAL_TYPE_ISDBT);
            SimpleAdapter modulationAdapter = new SimpleAdapter(getActivity(),
                    isdbtScanManager.getModulationSettings(),
                    R.layout.simple_list_item,
                    new String[]{"name"}, new int[]{R.id.name});
            mSpinnerModulation.setAdapter(modulationAdapter);
            SimpleAdapter bandwidthAdapter = new SimpleAdapter(getActivity(),
                    isdbtScanManager.getBandwidthSettings(),
                    R.layout.simple_list_item,
                    new String[]{"name"}, new int[]{R.id.name});
            mSpinnerBandwidth.setAdapter(bandwidthAdapter);
            mButtonScan.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    startScan();
                }
            });
        }
        return view;
    }

    private void startScan() {
        int freq = 0;
        final Bundle bundle = new Bundle();
        if (mEditFrequency != null) {
            try {
                String freqStr = mEditFrequency.getText().toString();
                freq = Integer.parseInt(freqStr);
            } catch (Exception e) {
                freq = 490;
            }
            bundle.putInt(ScanManager.KEY_FREQUENCY, freq);
            mEditFrequency.setEnabled(false);
        }
        if (mSpinnerModulation != null) {
            bundle.putInt(IsdbtScanManager.KEY_MODULATION,
                    mSpinnerModulation.getSelectedItemPosition());
            mSpinnerModulation.setEnabled(false);
        }
        if (mSpinnerBandwidth != null) {
            bundle.putInt(IsdbtScanManager.KEY_BANDWIDTH,
                    mSpinnerBandwidth.getSelectedItemPosition());
            mSpinnerBandwidth.setEnabled(false);
        }
        mButtonScan.setEnabled(false);
        final int finalFreq = freq;
        Log.d(TAG, "start isdbt scan, param:" + bundle.toString());
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                ScanManager.getInstance().startScan(
                        requireActivity().getApplicationContext(), finalFreq, bundle);
            }
        }, 0);
    }
}
