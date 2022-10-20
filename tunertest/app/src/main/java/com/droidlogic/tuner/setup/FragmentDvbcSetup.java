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
import com.droidlogic.tuner.scan.DvbcScanManager;
import com.droidlogic.tuner.scan.ScanManager;
import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.ThreadManager;


/**
 * A simple {@link Fragment} subclass.
 * Use the {@link FragmentDvbcSetup#newInstance} factory method to
 * create an instance of this fragment.
 */
public class FragmentDvbcSetup extends Fragment {
    private static final String ARG_PARAM1 = "param1";
    private static final String TAG = Constans.TAG;

    private EditText mEditFrequency = null;
    private EditText mEditSymbol = null;
    private Spinner mSpinnerQamMode = null;
    private Button mButtonScan = null;

    private String mTag;

    public FragmentDvbcSetup() {
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param tag Parameter 1.
     * @return A new instance of fragment FragmentDvbtSetup.
     */
    // TODO: Rename and change types and number of parameters
    static FragmentDvbcSetup newInstance(String tag) {
        FragmentDvbcSetup fragment = new FragmentDvbcSetup();
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
        View view = inflater.inflate(R.layout.fragment_dvbc_setup, container, false);
        mEditFrequency = view.findViewById(R.id.frequency_value);
        mEditSymbol = view.findViewById(R.id.symbol_value);
        mSpinnerQamMode = view.findViewById(R.id.qam_spinner);
        DvbcScanManager dvbcScanManager = (DvbcScanManager) ScanManager.getInstance()
                .getSession(ScanManager.SIGNAL_TYPE_DVBC);
        SimpleAdapter tsAdapter = new SimpleAdapter(getActivity(),
                dvbcScanManager.getQamModeSettings(),
                R.layout.simple_list_item,
                new String[] {"name"}, new int[] {R.id.name});
        mSpinnerQamMode.setAdapter(tsAdapter);
        mButtonScan = view.findViewById(R.id.button_start);
        mButtonScan.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startScan();
            }
        });
        return view;
    }

    @Override
    public void onDestroyView() {
        mEditFrequency = null;
        mEditSymbol = null;
        mSpinnerQamMode = null;
        mButtonScan = null;
        super.onDestroyView();
    }

    private void startScan() {
        int freqMhz = 0;
        int symbolRate = 6900;
        int qamMode = 0;
        final Bundle bundle = new Bundle();

        if (mEditFrequency != null) {
            String freqStr = mEditFrequency.getText().toString();
            try {
                freqMhz = Integer.parseInt(freqStr);
            } catch (Exception e) {
                freqMhz = 490;
            }
            mEditFrequency.setEnabled(false);
            if (mEditSymbol != null) {
                String symbolStr = mEditSymbol.getText().toString();
                try {
                    symbolRate = Integer.parseInt(symbolStr);
                } catch (Exception e) {
                    symbolRate = 6900;
                }
                mEditSymbol.setEnabled(false);
            }
            qamMode = mSpinnerQamMode.getSelectedItemPosition();
            mSpinnerQamMode.setEnabled(false);
            mButtonScan.setEnabled(false);
            mButtonScan.setText("Scanning");
        }
        final int finalFreqMhz = freqMhz;
        bundle.putInt(ScanManager.KEY_FREQUENCY, freqMhz);
        bundle.putInt(DvbcScanManager.KEY_SYMBOL_RATE, symbolRate);
        bundle.putInt(DvbcScanManager.KEY_QAM_MODE, qamMode);
        Log.d(TAG, "start dvbc scan, param:" + bundle.toString());
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                ScanManager.getInstance().startScan(
                        requireActivity().getApplicationContext(), finalFreqMhz, bundle);
            }
        }, 0);
    }
}
