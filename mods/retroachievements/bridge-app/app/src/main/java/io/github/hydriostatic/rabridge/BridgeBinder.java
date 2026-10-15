package io.github.hydriostatic.rabridge;

import android.content.Context;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Binder;
import android.os.Parcel;
import android.os.RemoteException;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/**
 * The Binder handed to the game in its launch intent. The mod's native library calls it with a
 * raw Parcel (no AIDL), so the layout here must match perform_bridge() in
 * native/src/http_android.cpp:
 *
 *   request: interface token, url (String), post data (byte[]), content type (String), user agent (String)
 *   reply:   no exception, status (int), body (byte[])
 *
 * Only HTTPS requests to RetroAchievements are relayed, and only for the supported game apps.
 */
final class BridgeBinder extends Binder {
    static final String EXTRA_KEY = "io.github.hydriostatic.rabridge.BINDER";
    static final String DESCRIPTOR = "io.github.hydriostatic.rabridge.IHttpBridge";
    static final int TRANSACTION_HTTP = FIRST_CALL_TRANSACTION;

    // Same values as rcheevos' RC_API_SERVER_RESPONSE_CLIENT_ERROR / _RETRYABLE_CLIENT_ERROR.
    private static final int CLIENT_ERROR = -1;
    private static final int RETRYABLE_CLIENT_ERROR = -2;

    private static final List<String> ALLOWED_HOSTS = Arrays.asList(
            "retroachievements.org", "www.retroachievements.org", "media.retroachievements.org");

    private static final String TAG = "RABridge";
    private static BridgeBinder instance;

    static final AtomicInteger requestCount = new AtomicInteger();
    static final AtomicInteger failureCount = new AtomicInteger();
    static final AtomicLong lastRequestTime = new AtomicLong();

    private final Context context;

    private BridgeBinder(Context context) {
        this.context = context.getApplicationContext();
        attachInterface(null, DESCRIPTOR);
    }

    static synchronized BridgeBinder get(Context context) {
        if (instance == null) {
            instance = new BridgeBinder(context);
        }
        return instance;
    }

    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        if (code != TRANSACTION_HTTP) {
            return super.onTransact(code, data, reply, flags);
        }
        data.enforceInterface(DESCRIPTOR);

        String url = data.readString();
        byte[] postData = data.createByteArray();
        String contentType = data.readString();
        String userAgent = data.readString();

        int status;
        byte[] body = null;

        if (!isSupportedCaller(getCallingUid())) {
            Log.w(TAG, "Refused a request from uid " + getCallingUid());
            status = CLIENT_ERROR;
        }
        else if (!isAllowed(url)) {
            Log.w(TAG, "Refused a request to " + url);
            status = CLIENT_ERROR;
        }
        else {
            Response response = send(url, postData, contentType, userAgent);
            status = response.status;
            body = response.body;
        }

        requestCount.incrementAndGet();
        lastRequestTime.set(System.currentTimeMillis());
        if (status < 200 || status >= 300) {
            failureCount.incrementAndGet();
        }

        reply.writeNoException();
        reply.writeInt(status);
        reply.writeByteArray(body);
        return true;
    }

    private boolean isSupportedCaller(int uid) {
        if (uid == android.os.Process.myUid()) {
            return true;
        }
        PackageManager pm = context.getPackageManager();
        String[] packages = pm.getPackagesForUid(uid);
        if (packages == null) {
            return false;
        }
        for (String name : packages) {
            if (Games.isSupported(name)) {
                return true;
            }
        }
        return false;
    }

    private static boolean isAllowed(String url) {
        if (url == null) {
            return false;
        }
        Uri uri = Uri.parse(url);
        return "https".equals(uri.getScheme()) && uri.getHost() != null && ALLOWED_HOSTS.contains(uri.getHost().toLowerCase());
    }

    private static final class Response {
        int status = RETRYABLE_CLIENT_ERROR;
        byte[] body;
    }

    private static Response send(String url, byte[] postData, String contentType, String userAgent) {
        Response response = new Response();
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setConnectTimeout(15000);
            connection.setReadTimeout(30000);
            if (userAgent != null && !userAgent.isEmpty()) {
                connection.setRequestProperty("User-Agent", userAgent);
            }

            if (postData != null && postData.length > 0) {
                connection.setRequestMethod("POST");
                connection.setDoOutput(true);
                connection.setRequestProperty("Content-Type",
                        contentType == null || contentType.isEmpty() ? "application/x-www-form-urlencoded" : contentType);
                connection.setFixedLengthStreamingMode(postData.length);
                try (OutputStream out = connection.getOutputStream()) {
                    out.write(postData);
                }
            }

            int status = connection.getResponseCode();
            InputStream in;
            try {
                in = connection.getInputStream();
            }
            catch (IOException e) {
                in = connection.getErrorStream();
            }
            response.body = in != null ? readAll(in) : new byte[0];
            response.status = status;
        }
        catch (IOException | RuntimeException e) {
            Log.w(TAG, "Request failed: " + e);
            response.status = RETRYABLE_CLIENT_ERROR;
            response.body = null;
        }
        finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
        return response;
    }

    private static byte[] readAll(InputStream in) throws IOException {
        try (InputStream input = in) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[16384];
            int count;
            while ((count = input.read(buffer)) != -1) {
                out.write(buffer, 0, count);
            }
            return out.toByteArray();
        }
    }
}
