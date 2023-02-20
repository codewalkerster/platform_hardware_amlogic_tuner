package com.droidlogic.tuner.utils;

import android.media.tv.tuner.frontend.FrontendInfo;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.util.List;

public class TunerHelper {
    public static class Tuner {
        static Method sGetFrontendIds;
        static Method sGetFrontendInfoById;
        static {
            try {
                Class<?> Tuner = Class.forName("android.media.tv.tuner.Tuner");
                sGetFrontendIds = Tuner.getDeclaredMethod("getFrontendIds");
                sGetFrontendInfoById = Tuner.getDeclaredMethod("getFrontendInfoById", int.class);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }

        public static List<Integer> getFrontendIds(android.media.tv.tuner.Tuner tuner) {
            try {
                return (List<Integer>) sGetFrontendIds.invoke(tuner);
            } catch (Exception e) {
                e.printStackTrace();
            }
            return null;
        }

        public static FrontendInfo getFrontendInfoById(android.media.tv.tuner.Tuner tuner, int id) {
            try {
                return (FrontendInfo) sGetFrontendInfoById.invoke(tuner, id);
            } catch (Exception e) {
                e.printStackTrace();
            }
            return null;
        }
    }

    public static class TunerVersionChecker {
        public static int TUNER_VERSION_UNKNOWN;
        public static int TUNER_VERSION_1_0;
        public static int TUNER_VERSION_1_1;


        static Method sIsHigherOrEqualVersionTo;
        static Method sGetTunerVersion;
        static {
            try {
                Class<?> TunerVersionChecker =
                        Class.forName("android.media.tv.tuner.TunerVersionChecker");
                sIsHigherOrEqualVersionTo = TunerVersionChecker
                        .getDeclaredMethod("isHigherOrEqualVersionTo", int.class);
                sGetTunerVersion = TunerVersionChecker
                        .getDeclaredMethod("getTunerVersion");
                TUNER_VERSION_UNKNOWN = TunerVersionChecker
                        .getDeclaredField("TUNER_VERSION_UNKNOWN").getInt(null);
                TUNER_VERSION_1_0 = TunerVersionChecker
                        .getDeclaredField("TUNER_VERSION_1_0").getInt(null);
                TUNER_VERSION_1_1 = TunerVersionChecker
                        .getDeclaredField("TUNER_VERSION_1_1").getInt(null);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }

        public static boolean isHigherOrEqualVersionTo(int version) {
            try {
                Boolean result = (Boolean) sIsHigherOrEqualVersionTo.invoke(null, version);
                return result != null? result: false;
            } catch (Exception e) {
                e.printStackTrace();
            }
            return false;
        }

        public static int getTunerVersion() {
            try {
                Integer result = (Integer) sGetTunerVersion.invoke(null);
                return result != null? result: TUNER_VERSION_UNKNOWN;
            } catch (Exception e) {
                e.printStackTrace();
            }
            return TUNER_VERSION_UNKNOWN;
        }
    }

    public static class AudioTrack {
        static int sENCAPSULATION_MODE_HANDLE;
        static Constructor<?> sTunerConfigurationConstructor;
        static Method sSetTunerConfiguration;
        static {
            try {
                Class<?> AudioTrack = Class.forName("android.media.AudioTrack");
                sENCAPSULATION_MODE_HANDLE = AudioTrack
                        .getDeclaredField("ENCAPSULATION_MODE_HANDLE").getInt(null);
                Class<?> TunerConfiguration =
                        Class.forName("android.media.AudioTrack$TunerConfiguration");
                sTunerConfigurationConstructor =
                        TunerConfiguration.getConstructor(int.class, int.class);
                Class<?> AudioTrackBuilder = Class.forName("android.media.AudioTrack$Builder");
                sSetTunerConfiguration = AudioTrackBuilder
                        .getDeclaredMethod("setTunerConfiguration", TunerConfiguration);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
        public static void setTunerConfiguration(android.media.AudioTrack.Builder builder,
                                                 int audioFilterId, int avSyncHwId) {
            try {
                builder.setEncapsulationMode(sENCAPSULATION_MODE_HANDLE);
                Object tunerConfiguration = sTunerConfigurationConstructor
                        .newInstance(new Object[]{audioFilterId, avSyncHwId});
                sSetTunerConfiguration.invoke(builder, tunerConfiguration);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public static class MediaEvent {
        static Method sRelease;
        static {
            try {
                Class<?> MediaEvent = Class.forName("android.media.tv.tuner.filter.MediaEvent");
                sRelease = MediaEvent.getDeclaredMethod("release");
            } catch (Exception e) {
                e.printStackTrace();
            }
        }

        public static void release(android.media.tv.tuner.filter.MediaEvent mediaEvent) {
            try {
                sRelease.invoke(mediaEvent);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }
}

