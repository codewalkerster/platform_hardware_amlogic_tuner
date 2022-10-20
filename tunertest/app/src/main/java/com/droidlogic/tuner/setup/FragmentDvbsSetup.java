package com.droidlogic.tuner.setup;

import android.os.Bundle;

import androidx.fragment.app.Fragment;

import android.text.TextUtils;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SimpleAdapter;
import android.widget.Spinner;
import android.widget.Toast;

import com.droidlogic.tuner.R;
import com.droidlogic.tuner.scan.DvbsScanManager;
import com.droidlogic.tuner.scan.ScanManager;

/**
 * A simple {@link Fragment} subclass.
 * Use the {@link FragmentDvbsSetup#newInstance} factory method to
 * create an instance of this fragment.
 */
public class FragmentDvbsSetup extends Fragment {
    // TODO: Rename parameter arguments, choose names that match
    // the fragment initialization parameters, e.g. ARG_ITEM_NUMBER
    private static final String ARG_PARAM1 = "param1";
    private static final String ARG_PARAM2 = "param2";

    // TODO: Rename and change types of parameters
    private String mParam1;
    private String mParam2;
    private Spinner mSpinnerLnbType;
    private CheckBox mCheckUseUb;
    private LinearLayout mLayoutUbSettings;
    private Spinner mSpinnerUbsLot;
    private EditText mEditUbFreq;
    private EditText mEditSateFreq;
    private Spinner mSpinnerPolarity;
    private EditText mEditSateSymbol;
    private CheckBox mCheckDvbS2;
    private Button mButtonScan;

    public FragmentDvbsSetup() {
        // Required empty public constructor
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param param1 Parameter 1.
     * @param param2 Parameter 2.
     * @return A new instance of fragment FragmentDvbsSetup.
     */
    // TODO: Rename and change types and number of parameters
    public static FragmentDvbsSetup newInstance(String param1, String param2) {
        FragmentDvbsSetup fragment = new FragmentDvbsSetup();
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
        mSpinnerLnbType = null;
        mCheckUseUb = null;
        mLayoutUbSettings = null;
        mSpinnerUbsLot = null;
        mEditUbFreq = null;
        mEditSateFreq = null;
        mSpinnerPolarity = null;
        mEditSateSymbol = null;
        mCheckDvbS2 = null;
        mButtonScan = null;
        super.onDestroy();
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        DvbsScanManager dvbsScanManager = (DvbsScanManager) ScanManager.getInstance()
                .getSession(ScanManager.SIGNAL_TYPE_DVBS);
        View view = inflater.inflate(R.layout.fragment_dvbs_setup, container, false);
        mSpinnerLnbType = view.findViewById(R.id.spinner_lnb_type);
        SimpleAdapter lnbTypeAdapter = new SimpleAdapter(getActivity(),
                dvbsScanManager.getLnbTypeList(),
                R.layout.simple_list_item,
                new String[] {"name"}, new int[] {R.id.name});
        mSpinnerLnbType.setAdapter(lnbTypeAdapter);

        mCheckUseUb = view.findViewById(R.id.checkBox_satcr);
        mLayoutUbSettings = view.findViewById(R.id.layout_ub_settings);
        mCheckUseUb.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (isChecked) {
                    mLayoutUbSettings.setVisibility(View.VISIBLE);
                } else {
                    mLayoutUbSettings.setVisibility(View.GONE);
                }
            }
        });
        mSpinnerUbsLot = view.findViewById(R.id.spinner_ub_slot);
        SimpleAdapter ubSlotAdapter = new SimpleAdapter(getActivity(),
                dvbsScanManager.getUbSlotList(),
                R.layout.simple_list_item,
                new String[] {"name"}, new int[] {R.id.name});
        mSpinnerUbsLot.setAdapter(ubSlotAdapter);

        mEditUbFreq = view.findViewById(R.id.edit_ub_freq);
        mEditSateFreq = view.findViewById(R.id.edit_sate_freq);

        mSpinnerPolarity = view.findViewById(R.id.spinner_polarity);
        SimpleAdapter polarityAdapter = new SimpleAdapter(getActivity(),
                dvbsScanManager.getPolarityList(),
                R.layout.simple_list_item,
                new String[] {"name"}, new int[] {R.id.name});
        mSpinnerPolarity.setAdapter(polarityAdapter);

        mEditSateSymbol = view.findViewById(R.id.edit_sate_symbol);
        mCheckDvbS2 = view.findViewById(R.id.checkBox_standard);
        mButtonScan = view.findViewById(R.id.button_start);
        mButtonScan.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startScan();
            }
        });
        return view;
    }

    private void startScan() {
        Bundle bundle = new Bundle();

        try {
            int sateFreq = Integer.parseInt(mEditSateFreq.getText().toString());
            bundle.putInt(DvbsScanManager.KEY_SATELLITE_FREQUENCY, sateFreq);

            int polarity = mSpinnerPolarity.getSelectedItemPosition();
            bundle.putInt(DvbsScanManager.KEY_SATELLITE_POLARITY, polarity);

            String symbolsStr = mEditSateSymbol.getText().toString();
            if (TextUtils.isEmpty(symbolsStr)) {
                symbolsStr = mEditSateSymbol.getHint().toString();
            }
            int symbols = Integer.parseInt(symbolsStr);
            bundle.putInt(DvbsScanManager.KEY_SATELLITE_SYMBOL, symbols);

            int lnbType = mSpinnerLnbType.getSelectedItemPosition();
            bundle.putInt(DvbsScanManager.KEY_LNB_TYPE, lnbType);
            bundle.putBoolean(DvbsScanManager.KEY_LNB_UNICABLE, mCheckUseUb.isChecked());
            if (lnbType == DvbsScanManager.LNB_TYPE_SINGLE) {
                bundle.putInt(DvbsScanManager.KEY_LNB_LOF, DvbsScanManager.DEFAULT_SINGLE_LNB);
            } else if (lnbType == DvbsScanManager.LNB_TYPE_UNIVERSE) {
                bundle.putInt(DvbsScanManager.KEY_LNB_LOF, DvbsScanManager.DEFAULT_DOUBLE_LNB_LOW);
                bundle.putInt(DvbsScanManager.KEY_LNB_HOF, DvbsScanManager.DEFAULT_DOUBLE_LNB_HIGH);
            }
            if (mCheckUseUb.isChecked()) {
                int slot = mSpinnerUbsLot.getSelectedItemPosition();
                int freq = Integer.parseInt(mEditUbFreq.getText().toString());
                bundle.putInt(DvbsScanManager.KEY_UNICABLE_BAND, slot);
                bundle.putInt(DvbsScanManager.KEY_UNICABLE_FREQUENCY, freq);
            }
            bundle.putBoolean(DvbsScanManager.KEY_SATELLITE_STANDARD, mCheckDvbS2.isChecked());
        } catch (Exception ignored){
        }

        DvbsScanManager dvbsScanManager = (DvbsScanManager) ScanManager.getInstance()
                .getSession(ScanManager.SIGNAL_TYPE_DVBS);
        if (dvbsScanManager.isSettingsValid(bundle)) {
            ScanManager.getInstance().startScan(requireActivity().getApplicationContext(),
                    bundle.getInt(DvbsScanManager.KEY_SATELLITE_FREQUENCY), bundle);
            mEditSateFreq.setEnabled(false);
            mSpinnerPolarity.setEnabled(false);
            mEditSateSymbol.setEnabled(false);
            mCheckUseUb.setEnabled(false);
            mSpinnerLnbType.setEnabled(false);
            mSpinnerUbsLot.setEnabled(false);
            mEditUbFreq.setEnabled(false);
            mCheckDvbS2.setEnabled(false);
            mButtonScan.setEnabled(false);
        } else {
            Toast.makeText(requireActivity().getApplicationContext(),
                    "Settings invalid", Toast.LENGTH_SHORT).show();
        }
    }
}
