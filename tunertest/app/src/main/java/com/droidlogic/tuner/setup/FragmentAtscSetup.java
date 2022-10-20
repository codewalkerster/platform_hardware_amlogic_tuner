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
import com.droidlogic.tuner.scan.AtscScanManager;
import com.droidlogic.tuner.scan.ScanManager;
import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.ThreadManager;

/**
 * A simple {@link Fragment} subclass.
 * Use the {@link FragmentAtscSetup#newInstance} factory method to
 * create an instance of this fragment.
 */
public class FragmentAtscSetup extends Fragment {
    private static final String ARG_PARAM1 = "param1";
    private static final String ARG_PARAM2 = "param2";
    private static final String TAG = Constans.TAG;

    private String mParam1;
    private String mParam2;
    private EditText mEditFrequency;
    private Spinner mSpinnerModulation;
    private Button mButtonScan;

    public FragmentAtscSetup() {
        // Required empty public constructor
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param param1 Parameter 1.
     * @param param2 Parameter 2.
     * @return A new instance of fragment FragmentAtscSetup.
     */
    // TODO: Rename and change types and number of parameters
    static FragmentAtscSetup newInstance(String param1, String param2) {
        FragmentAtscSetup fragment = new FragmentAtscSetup();
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
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        View view = inflater.inflate(R.layout.fragment_atsc_setup, container, false);
        if (view != null) {
            mEditFrequency = view.findViewById(R.id.frequency_value);
            mSpinnerModulation = view.findViewById(R.id.modulation_spinner);
            mButtonScan = view.findViewById(R.id.button_start);
            AtscScanManager atscScanManager = (AtscScanManager) ScanManager.getInstance()
                    .getSession(ScanManager.SIGNAL_TYPE_ATSC);
            SimpleAdapter modulationAdapter = new SimpleAdapter(getActivity(),
                    atscScanManager.getModulationSettings(),
                    R.layout.simple_list_item,
                    new String[] {"name"}, new int[] {R.id.name});
            mSpinnerModulation.setAdapter(modulationAdapter);
            mButtonScan.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    startScan();
                }
            });
        }
        return view;
    }

    @Override
    public void onDestroyView() {
        mEditFrequency = null;
        mSpinnerModulation = null;
        mButtonScan = null;
        super.onDestroyView();
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
            bundle.putInt(AtscScanManager.KEY_MODULATION,
                    mSpinnerModulation.getSelectedItemPosition());
            mSpinnerModulation.setEnabled(false);
        }
        mButtonScan.setEnabled(false);
        final int finalFreq = freq;
        Log.d(TAG, "start atsc scan, param:" + bundle.toString());
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                ScanManager.getInstance().startScan(
                        requireActivity().getApplicationContext(), finalFreq, bundle);
            }
        }, 0);
    }
}
