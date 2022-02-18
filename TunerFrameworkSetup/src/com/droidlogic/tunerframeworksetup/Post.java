package com.droidlogic.tunerframeworksetup;

import android.util.Log;
import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.HashMap;
import java.util.Map;
import java.util.Iterator;

public class Post {
    private static final String TAG = Post.class.getSimpleName();
    private String mUrl;
    private byte[] mData;
    private final boolean mExpectOutput;
    private final Map<String, String> mProperties = new HashMap();

    public class Response {
        byte[] body;
        int code;
    };

    Post(String url, byte[] data) {
        this.mUrl = url;
        this.mData = data;
        if (data == null) {
            this.mExpectOutput = false;
        } else {
            this.mExpectOutput = true;
        }
    }

    public void setProperty(String a, String b) {
        this.mProperties.put(a, b);
    }

    public Response send() {
        Response r = new Response();
        try {
            r.body = sendPost(this.mUrl, this.mData);
            r.code = 200;
        } catch (IOException e) {
            Log.d(TAG, e.getMessage());
            r.body = null;
            r.code = 400;
        }
        return r;
    }

    private byte[] sendPost(String url, byte[] data) throws IOException {
        URL obj = new URL(url);
        HttpURLConnection con = (HttpURLConnection) obj.openConnection();

        //add request header
        con.setRequestMethod("POST");
        if (SetupInstance.mContentType != null)
            con.setRequestProperty("content-type", SetupInstance.mContentType +"; charset=utf-8");
        //con.setRequestProperty("Accept-Language", "en-US,en;q=0.5");
        //con.setRequestProperty("User-Agent", "Widevine CDM v1.0");
        Iterator iterator = this.mProperties.keySet().iterator();
        while (iterator.hasNext()) {
            Object key = iterator.next();
            Log.d(TAG, "key is :" + key);
            Log.d(TAG, "get(key) is :" + this.mProperties.get(key));
            con.setRequestProperty(key.toString(), this.mProperties.get(key));
        }
        //con.setRequestProperty("Content-Length", "0");
        con.setRequestProperty("Connection", "close");
        con.setReadTimeout(10000 /* milliseconds */);
        con.setConnectTimeout(15000 /* milliseconds */);

        //String urlParameters = "sn=C02G8416DRJM&cn=&locale=&caller=&num=12345";

        // Send post request
        //con.setDoInput(true);
        if (this.mExpectOutput) {
            con.setDoOutput(true);
            DataOutputStream wr = new DataOutputStream(con.getOutputStream());
            //wr.writeBytes(urlParameters);
            wr.write(data, 0, data.length);
            wr.flush();
            wr.close();
        }

        /*
           int responseCode = con.getResponseCode();
           System.out.println("\nSending 'POST' request to URL : " + url);
           System.out.println("Post data : " + data);
           System.out.println("Response Code : " + responseCode);

           BufferedReader in = new BufferedReader(
           new InputStreamReader(con.getInputStream()));
           String inputLine;
           StringBuffer response = new StringBuffer();

           while ((inputLine = in.readLine()) != null) {
           response.append(inputLine);
           }
           in.close();

        //print result
        System.out.println(response.toString());

        return null;
        */

        InputStream is = con.getInputStream();
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        byte[] buffer = new byte[1024];
        int readBytes = -1;
        while ((readBytes = is.read(buffer)) > 1) {
            baos.write(buffer,0,readBytes);
        }

        byte[] responseArray = baos.toByteArray();
        return responseArray;

    }
}
