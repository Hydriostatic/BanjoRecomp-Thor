package io.github.hydriostatic.banjothor;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Saves crash information where the player can get to it without a computer.
 *
 * When the game crashes, the native side (src/android/crash_report.cpp) or the Java handler
 * below writes a short report. On the next launch it's combined with the app's recent log
 * and saved as a text file in Download/BanjoThor, ready to be shared.
 */
final class CrashLogs {
    private static final String TAG = "BanjoThor";
    private static final String FOLDER = "BanjoThor";

    private CrashLogs() {
    }

    static File reportFile(Activity activity) {
        return new File(activity.getFilesDir(), "crash-report.txt");
    }

    /** Catches Java exceptions that would close the app, so they're reported too. */
    static void installJavaHandler(Activity activity) {
        final File report = reportFile(activity);
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try (FileOutputStream out = new FileOutputStream(report)) {
                StringWriter trace = new StringWriter();
                error.printStackTrace(new PrintWriter(trace));
                out.write(("Java exception on thread " + thread.getName() + "\n\n" + trace).getBytes(StandardCharsets.UTF_8));
            } catch (Exception ignored) {
            }

            if (previous != null) {
                previous.uncaughtException(thread, error);
            }
        });
    }

    /** If the last session crashed, saves its report and log to Download/BanjoThor. */
    static void saveReportFromLastSession(Activity activity) {
        File report = reportFile(activity);
        if (!report.exists()) {
            return;
        }

        StringBuilder text = new StringBuilder();
        text.append("Banjo Thor crash report\n");
        text.append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(", Android ").append(Build.VERSION.RELEASE).append('\n');
        try {
            text.append("App version: ")
                    .append(activity.getPackageManager().getPackageInfo(activity.getPackageName(), 0).versionName)
                    .append('\n');
        } catch (Exception ignored) {
        }
        text.append('\n').append(readFile(report)).append("\n\n---- Log ----\n").append(readLog());

        String name = "crash-" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date()) + ".txt";
        if (writeToDownloads(activity, name, text.toString())) {
            report.delete();
            Toast.makeText(activity, "The game crashed last time. A crash log was saved to Download/" + FOLDER + "/" + name,
                    Toast.LENGTH_LONG).show();
        }
    }

    /** The log lines from this app, which still include the previous session's. */
    private static String readLog() {
        StringBuilder log = new StringBuilder();
        try {
            Process process = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "4000")
                    .redirectErrorStream(true)
                    .start();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    log.append(line).append('\n');
                }
            }
            process.waitFor();
        } catch (Exception e) {
            log.append("Couldn't read the log: ").append(e);
        }
        return log.toString();
    }

    private static boolean writeToDownloads(Activity activity, String name, String text) {
        try {
            ContentResolver resolver = activity.getContentResolver();
            ContentValues values = new ContentValues();
            values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
            values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
            values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/" + FOLDER);
            Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null) {
                return false;
            }
            try (OutputStream out = resolver.openOutputStream(uri)) {
                if (out == null) {
                    return false;
                }
                out.write(text.getBytes(StandardCharsets.UTF_8));
            }
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Saving the crash log failed", e);
            return false;
        }
    }

    private static String readFile(File file) {
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] bytes = new byte[(int) file.length()];
            int read = in.read(bytes);
            return new String(bytes, 0, Math.max(read, 0), StandardCharsets.UTF_8);
        } catch (Exception e) {
            return "Couldn't read the report: " + e;
        }
    }
}
