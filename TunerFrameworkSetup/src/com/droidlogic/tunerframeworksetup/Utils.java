package com.droidlogic.tunerframeworksetup;

import android.content.Context;
import android.content.res.Resources;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class Utils {
    private static final String TAG = "Utils";
    private static Method sysPropGet;
    private static Method sysPropSet;


    public Utils() {
    }

    static {
        try {
            Class<?> S = Class.forName("android.os.SystemProperties");
            Method M[] = S.getMethods();
            for (Method m : M) {
                String n = m.getName();
                if (n.equals("get")) {
                    sysPropGet = m;
                } else if (n.equals("set")) {
                    sysPropSet = m;
                }
            }
        } catch (ClassNotFoundException e) {
            e.printStackTrace();
        }
    }

    public static String getSysProp(String name, String default_value) {
        try {
            return (String) sysPropGet.invoke(null, name, default_value);
        } catch (IllegalArgumentException e) {
            e.printStackTrace();
        } catch (IllegalAccessException e) {
            e.printStackTrace();
        } catch (InvocationTargetException e) {
            e.printStackTrace();
        }
        return default_value;
    }

    public static void setSysProp(String name, String value) {
        try {
            sysPropSet.invoke(null, name, value);
        } catch (IllegalArgumentException e) {
            e.printStackTrace();
        } catch (IllegalAccessException e) {
            e.printStackTrace();
        } catch (InvocationTargetException e) {
            e.printStackTrace();
        }
    }

    public static Process execCmd(String cmd) {
        int ch;
        Process p = null;
        Log.d(TAG, "exec command: " + cmd);
        try {
            p = Runtime.getRuntime().exec(cmd);
            if (p == null) {
                Log.d(TAG, "execCmd fail, p is null!!!");
                return null;
            }
            Log.d(TAG, "Now before waitFor!");
            p.waitFor();
            Log.d(TAG, "Now after waitFor!");
            BufferedReader input = new BufferedReader(new InputStreamReader(p.getInputStream()));
            String line = "";
            StringBuffer shellRet = new StringBuffer();
            while ((line = input.readLine()) != null) {
                shellRet.append(line);
            }
            input.close();
            Log.d(TAG, "shellRet is:" + shellRet.toString());
        } catch (InterruptedException e) {
            Log.d(TAG, "InterruptedException: " + e.toString());

        } catch (IOException e) {
            Log.d(TAG, "IOException: " + e.toString());
        }
        return p;
    }

    public static void copyRawToFile(Context context, int id, String dstPath) {
        BufferedInputStream bufferIn = null;
        BufferedOutputStream bufferOut = null;
        byte[] buffer = new byte[1024];
        try {
            File dstFile = new File(dstPath);
            if (!dstFile.exists()) {
                dstFile.createNewFile();
            }
            InputStream inStream = context.getResources().openRawResource(id);
            OutputStream outStream = new FileOutputStream(dstPath);
            bufferIn = new BufferedInputStream(inStream);
            bufferOut = new BufferedOutputStream(outStream);
            int len = bufferIn.read(buffer);
            while (len != -1) {
                bufferOut.write(buffer, 0, len);
                Arrays.fill(buffer, (byte) '\0');
                len = bufferIn.read(buffer);
            }
        } catch (Resources.NotFoundException e) {
            Log.d(TAG, "Now Resources.NotFoundException !!!");
        } catch (IOException e) {
            Log.d(TAG, "Now IOException!");
        } catch (Exception e) {
        } finally {
            try {
                if (bufferIn != null) {
                    bufferIn.close();
                }
                if (bufferOut != null) {
                    bufferOut.close();
                }
            } catch (IOException e) {
                Log.d(TAG, "Now IOException 2!!!");
            }
        }
    }

    public static void writeLineToFile(String str, String filePath) {
        Log.d(TAG, "writeLineToFile, str:" + str + " filePath:" + filePath);
        File tmpFile = new File(filePath);

        try {
            if (!tmpFile.exists()) {
                tmpFile.createNewFile();
            }
            FileWriter fw = new FileWriter(filePath, true);
            PrintWriter pw = new PrintWriter(fw);
            pw.println(str);
            pw.close();
            fw.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public static String readLineFromFile(String filePath) {
        String line = null;
        FileInputStream inputStream = null;
        BufferedReader bufferedReader = null;
        try {
            inputStream = new FileInputStream(filePath);
            bufferedReader = new BufferedReader(new InputStreamReader(inputStream));
            line = bufferedReader.readLine();
            bufferedReader.close();
            inputStream.close();
        } catch (IOException e) {
        };
        return line;
    }

    public static float calBitrate(int bytes, int time) {
        if (time == 0 || bytes == 0)
            return 0;
        return bytes * 8 / time;
    }

    public static String parseBitrateFromFile(String source) {
        String ret = null;
        Pattern pattern;
        pattern = Pattern.compile("fps_(.*[MK])bps_");
        Matcher matcher = pattern.matcher(source);

        if (matcher.find()) {
            int count = matcher.groupCount();
            if (count >= 1) {
                ret = matcher.group(1);
            }
        }
        return ret;
    }

    public static String parseFramerateFromFile(String source) {
        String ret = null;
        Pattern pattern;
        pattern = Pattern.compile("_(.*)fps_");
        Matcher matcher = pattern.matcher(source);
        if (matcher.find()) {
            int count = matcher.groupCount();
            if (count >= 1) {
                ret = matcher.group(1);
            }
        }
        return ret;
    }

    public static String parseVideoFormatFromFile(String source) {
        String ret = null;
        Pattern pattern;
        pattern = Pattern.compile("^4K(.*)_");
        Matcher matcher = pattern.matcher(source);
        if (matcher.find()) {
            int count = matcher.groupCount();
            if (count >= 1) {
                ret = matcher.group(1);
            }
        }
        return ret;
    }

    public static String getPrivateDir(Context context) {
        return "/data/data/" + context.getPackageName() + "/";
    }

    public static String setPropString(String prop) {
        String propValue = null;
        String propString = null;
        try {
            Process mProcess = Runtime.getRuntime().exec(prop);
            InputStreamReader mISR = new InputStreamReader(mProcess.getInputStream());
            BufferedReader mBR = new BufferedReader(mISR);
            Log.d(TAG, prop);
            while ((propValue = mBR.readLine()) != null) {
                propString = propValue;
            }
            if (mBR != null) {
                mBR.close();
            }
        } catch (IOException e) {
            Log.e(TAG, "Can not read value from prop " + prop);
            //e.printStackTrace();
            Log.e(TAG, "IOException message: " + e);
        } catch (Exception e) {
            Log.e(TAG, "Can not read value from prop " + prop);
            Log.e(TAG, "Exception message: " + e);
        }
        if (propString != null && propString.length() > 0) {
            Log.d(TAG, prop + " is " + propString);
            return propString;
        } else {
            Log.d(TAG, prop + " is empty");
            return null;
        }
    }

    public static String getPropString(String prop) {
        String propValue = null;
        String propString = null;
        try {
            Process mProcess = Runtime.getRuntime().exec(prop);
            InputStreamReader mISR = new InputStreamReader(mProcess.getInputStream());
            BufferedReader mBR = new BufferedReader(mISR);
            Log.d(TAG, prop);
            while ((propValue = mBR.readLine()) != null) {
                propString = propValue;
            }
            if (mBR != null) {
                mBR.close();
            }
        } catch (IOException e) {
            Log.e(TAG, "Can not read value from prop " + prop);
            //e.printStackTrace();
            Log.e(TAG, "IOException message: " + e);
        } catch (Exception e) {
            Log.e(TAG, "Can not read value from prop " + prop);
            Log.e(TAG, "Exception message: " + e);
        }
        if (propString != null && propString.length() > 0) {
            Log.d(TAG, prop + " is " + propString);
            return propString;
        } else {
            Log.d(TAG, prop + " is empty");
            return null;
        }
    }
}
