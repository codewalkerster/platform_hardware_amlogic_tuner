package com.droidlogic.tuner.setup;

import androidx.appcompat.app.AppCompatActivity;
import androidx.viewpager.widget.ViewPager;

import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.TextView;

import com.droidlogic.tuner.R;
import com.droidlogic.tuner.channel.ChannelManager;
import com.droidlogic.tuner.scan.ScanManager;
import com.droidlogic.tuner.utils.Constans;
import com.droidlogic.tuner.utils.ThreadManager;

public class SetupActivity extends AppCompatActivity {
    private final static String TAG = Constans.TAG;
    private boolean isScanning;
    private int totalPages = 0;
    private TextView mTvIndicator;
    private ImageView mLockedImage;
    private TextView mChannelNum;
    private TextView mSymbolText;
    private TextView mSymbols;
    private ProgressBar mStrengthProgressBar;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_setup);
        mTvIndicator = findViewById(R.id.tvIndicate);
        mLockedImage = findViewById(R.id.imageViewLocked);
        mChannelNum = findViewById(R.id.tvChannelNum);
        mSymbolText = findViewById(R.id.tvSymbol);
        mSymbols = findViewById(R.id.tvSymbols);
        mStrengthProgressBar = findViewById(R.id.progressBarStrength);
        ScanManager.getInstance().switchSignalType(ScanManager.SIGNAL_TYPE_DVBT);
        initPagers();
        updateIndicator(1);
    }

    @Override
    protected void onResume() {
        super.onResume();
        mLockedImage.setImageResource(R.drawable.initial);
        mSymbolText.setVisibility(View.INVISIBLE);
        registerScanCallback();
    }

    private void initPagers() {
        SetupViewPagerAdapter pagerAdapter = new SetupViewPagerAdapter(getSupportFragmentManager());
        pagerAdapter.addFragment(FragmentDvbtSetup.newInstance("dvbt"));
        pagerAdapter.addFragment(FragmentDvbcSetup.newInstance("dvbc"));
        pagerAdapter.addFragment(FragmentDvbsSetup.newInstance("dvbs", null));
        pagerAdapter.addFragment(FragmentIsdbSetup.newInstance("isdbt", null));
        pagerAdapter.addFragment(FragmentAtscSetup.newInstance("atsc", null));

        totalPages = pagerAdapter.getCount();
        ViewPager pager = findViewById(R.id.viewPagerMain);
        pager.setAdapter(pagerAdapter);
        pager.addOnPageChangeListener(new ViewPager.OnPageChangeListener() {
            @Override
            public void onPageScrolled(int position,
                                       float positionOffset, int positionOffsetPixels) {
            }

            @Override
            public void onPageSelected(int position) {
                switch (position) {
                    case 0:
                        ScanManager.getInstance().switchSignalType(ScanManager.SIGNAL_TYPE_DVBT);
                        break;
                    case 1:
                        ScanManager.getInstance().switchSignalType(ScanManager.SIGNAL_TYPE_DVBC);
                        break;
                    case 2:
                        ScanManager.getInstance().switchSignalType(ScanManager.SIGNAL_TYPE_DVBS);
                        break;
                    case 3:
                        ScanManager.getInstance().switchSignalType(ScanManager.SIGNAL_TYPE_ISDBT);
                        break;
                    case 4:
                        ScanManager.getInstance().switchSignalType(ScanManager.SIGNAL_TYPE_ATSC);
                        break;
                }
                updateIndicator(position + 1);
            }

            @Override
            public void onPageScrollStateChanged(int state) {

            }
        });
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (isScanning) {
            return false;
        }
        return super.onKeyDown(keyCode, event);
    }

    public synchronized void onScanStopped() {
        isScanning = false;
        final int num = ChannelManager.getInstance().getChannelNumber();
        Log.i(TAG, "Scan finish, found " + num + " channels");
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                mChannelNum.setText("" + num);
            }
        }, 0);
        ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
            @Override
            public void run() {
                finish();
            }
        }, 1500);
    }

    private void registerScanCallback() {
        ScanManager.getInstance().registerScanEvent(new ScanManager.OnScanEvent() {
            @Override
            public void onScanStart() {
                synchronized (this) {
                    isScanning = true;
                }
            }

            @Override
            public void onScanLock(final int strength) {
                Log.d(TAG, "scan locked");
                ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
                    @Override
                    public void run() {
                        mLockedImage.setImageResource(R.drawable.locked);
                        mStrengthProgressBar.setProgress(strength);
                    }
                }, 0);
            }

            @Override
            public void onScanUnLock(final int strength) {
                Log.d(TAG, "scan unlock, stop scan");
                ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
                    @Override
                    public void run() {
                        mLockedImage.setImageResource(R.drawable.unlocked);
                        mStrengthProgressBar.setProgress(strength);
                        mSymbolText.setVisibility(View.INVISIBLE);
                        mSymbols.setText("");
                    }
                }, 0);
            }

            @Override
            public void onSymbolRatesReported(int[] rate) {
                if (rate.length > 0 && rate[0] > 0) {
                    final int symbolRate = rate[0] / 1000;
                    ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
                        @Override
                        public void run() {
                            mSymbolText.setText(R.string.symbol);
                            mSymbolText.setVisibility(View.VISIBLE);
                            mSymbols.setText("" + symbolRate);
                        }
                    }, 0);
                }
            }

            @Override
            public void onPlpIdsReported(final int[] plpIds) {
                ThreadManager.getInstance().runOnMainThreadDelayed(new Runnable() {
                    @Override
                    public void run() {
                        if (plpIds.length > 0) {
                            mSymbolText.setText(R.string.plpId_num);
                            mSymbolText.setVisibility(View.VISIBLE);
                            mSymbols.setText("" + plpIds.length);
                        }
                    }
                }, 0);
            }

            @Override
            public void onScanEnd() {
                Log.d(TAG, "scan end");
                onScanStopped();
            }
        });
    }

    private void updateIndicator(int current) {
        if (mTvIndicator != null) {
            mTvIndicator.setText(current + " / " + totalPages);
        }
    }
}
