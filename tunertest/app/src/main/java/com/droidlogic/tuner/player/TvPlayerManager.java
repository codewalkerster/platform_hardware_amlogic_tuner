package com.droidlogic.tuner.player;

import android.content.Context;
import android.util.Log;

import com.droidlogic.tuner.utils.Constants;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class TvPlayerManager {
    private static final String TAG = Constants.TAG;
    private static final int MAX_PLAYERS = 2;
    private static TvPlayerManager mInstance;
    private Map<Integer, Boolean> mPlayerPools;
    private List<TvPlayer> mPlayers;

    private TvPlayerManager() {
        mPlayerPools = new HashMap<>();
        mPlayers = new ArrayList<>();
        for (int i = 0; i < MAX_PLAYERS; i ++) {
            mPlayerPools.put(i, true);
        }
        Log.d(TAG, mPlayerPools.toString());
    }

    public static TvPlayerManager getInstance() {
        if (mInstance == null) {
            mInstance = new TvPlayerManager();
        }
        return mInstance;
    }

    private int acquirePlayerId() {
        int freeId = -1;
        for (int i : mPlayerPools.keySet()) {
            if (mPlayerPools.get(i)) {
                freeId = i;
                break;
            }
        }
        Log.d(TAG, "Acquired player id: " + freeId);
        return freeId;
    }

    public TvPlayer createPlayer() {
        int id = acquirePlayerId();
        if (id == -1) {
            Log.e(TAG, "No player resources.");
            return null;
        }
        TvPlayer p =  new TvPlayer(id);
        mPlayers.add(p);
        mPlayerPools.put(id, false);
        return p;
    }

    public void releasePlayer(Context context, int id) {
        TvPlayer player = null;
        for (TvPlayer p : mPlayers) {
            if (p.getPlayerId() == id) {
                player = p;
                break;
            }
        }
        if (player != null) {
            player.release(context);
            mPlayerPools.put(player.getPlayerId(), true);
        }
    }

    public void releasePlayer(Context context, TvPlayer player) {
        if (player == null) return;

        try {
            mPlayers.remove(player);
            mPlayerPools.put(player.getPlayerId(), true);
        } catch (Exception ignored) {
        } finally {
            player.release(context);
        }
    }
}
