package io.github.hydriostatic.banjothor;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * The game's activity. SDL does the heavy lifting (window, input, audio); this class adds what
 * the Android version needs on top: unpacking the game's UI assets, Android's document picker in
 * place of the desktop file dialogs, telling the renderer when the screen goes away, and the
 * companion view on the AYN Thor's second screen.
 */
public class ThorActivity extends SDLActivity {
    private static final String TAG = "BanjoThor";
    private static final int REQUEST_PICK_DOCUMENTS = 0x4254;

    private SecondScreen secondScreen;

    // Implemented in src/android/android_bridge.cpp.
    static native void nativeOnDocumentsPicked(String[] paths);
    static native void nativeSetSurfaceReady(boolean ready);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The native side reads these as soon as SDL starts it, so they have to be set first.
        File programDir = new File(getFilesDir(), "program");
        unpackProgramAssets(programDir);
        setEnv("BANJO_PROGRAM_DIR", programDir.getAbsolutePath());
        // Saves, settings and mods go to ~/.config/<program id> like on Linux, so HOME
        // is pointed at the app's private storage.
        setEnv("HOME", getFilesDir().getAbsolutePath());
        // Where the native crash handler writes its report (see CrashLogs).
        setEnv("BANJO_CRASH_REPORT", CrashLogs.reportFile(this).getAbsolutePath());
        CrashLogs.installJavaHandler(this);

        super.onCreate(savedInstanceState);

        CrashLogs.saveReportFromLastSession(this);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        secondScreen = new SecondScreen(this);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new GameSurface(context);
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        secondScreen.show();
    }

    @Override
    protected void onDestroy() {
        if (secondScreen != null) {
            secondScreen.dismiss();
        }
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    // ---------------------------------------------------------------------------------------
    // Program assets

    /**
     * The game loads its UI files, fonts and controller database from disk, so the copies
     * packed in the APK are unpacked once per installed version.
     */
    private void unpackProgramAssets(File programDir) {
        String version = installedVersion();
        File marker = new File(programDir, ".unpacked-version");
        if (version.equals(readText(marker))) {
            return;
        }

        deleteRecursively(programDir);
        try {
            copyAssetTree(getAssets(), "program", programDir);
            writeText(marker, version);
        } catch (IOException e) {
            Log.e(TAG, "Unpacking the program assets failed", e);
        }
    }

    private String installedVersion() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return info.versionName + "/" + info.lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            return "unknown";
        }
    }

    private static void copyAssetTree(AssetManager assets, String assetPath, File destination) throws IOException {
        String[] children = assets.list(assetPath);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("Can't create " + destination);
            }
            for (String child : children) {
                copyAssetTree(assets, assetPath + "/" + child, new File(destination, child));
            }
            return;
        }

        try (InputStream in = assets.open(assetPath); OutputStream out = new FileOutputStream(destination)) {
            copy(in, out);
        }
    }

    // ---------------------------------------------------------------------------------------
    // Document picker (stands in for the desktop file dialogs)

    /**
     * Called from native code when the game wants a file: the ROM, or mods to install.
     * The picked documents are copied into the app's cache and their paths handed back.
     */
    @SuppressWarnings("unused")
    public void openDocumentPicker(boolean multiple) {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
            try {
                startActivityForResult(intent, REQUEST_PICK_DOCUMENTS);
            } catch (Exception e) {
                Log.e(TAG, "No document picker available", e);
                nativeOnDocumentsPicked(new String[0]);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_DOCUMENTS) {
            return;
        }

        final List<Uri> uris = new ArrayList<>();
        if (resultCode == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                    uris.add(data.getClipData().getItemAt(i).getUri());
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
        }

        // ROMs and mods can be big; copy them off the UI thread.
        new Thread(() -> {
            File pickedDir = new File(getCacheDir(), "picked");
            deleteRecursively(pickedDir);
            pickedDir.mkdirs();

            List<String> paths = new ArrayList<>();
            for (Uri uri : uris) {
                File destination = new File(pickedDir, safeName(displayName(uri)));
                try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(destination)) {
                    if (in == null) {
                        continue;
                    }
                    copy(in, out);
                    paths.add(destination.getAbsolutePath());
                } catch (IOException e) {
                    Log.e(TAG, "Copying " + uri + " failed", e);
                }
            }

            nativeOnDocumentsPicked(paths.toArray(new String[0]));
        }, "DocumentImport").start();
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                return cursor.getString(0);
            }
        } catch (Exception ignored) {
        }

        String last = uri.getLastPathSegment();
        return last != null ? last : "document";
    }

    private static String safeName(String name) {
        String cleaned = name.replace('/', '_').replace('\\', '_');
        return cleaned.isEmpty() ? "document" : cleaned;
    }

    // ---------------------------------------------------------------------------------------
    // Surface lifecycle

    /** Tells the renderer when Android takes the window away and when it gives it back. */
    private static class GameSurface extends SDLSurface {
        GameSurface(Context context) {
            super(context);
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            // A new window isn't usable until SDL has seen its size in surfaceChanged().
            setSurfaceReady(false);
            super.surfaceCreated(holder);
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            super.surfaceChanged(holder, format, width, height);
            // SDL skips surfaces it can't use yet (e.g. the wrong orientation while rotating).
            setSurfaceReady(mIsSurfaceReady);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            // Stop presenting before SDL lets go of the native window.
            setSurfaceReady(false);
            super.surfaceDestroyed(holder);
        }

        private static void setSurfaceReady(boolean ready) {
            try {
                nativeSetSurfaceReady(ready);
            } catch (UnsatisfiedLinkError e) {
                Log.w(TAG, "Native library not loaded yet", e);
            }
        }
    }

    // ---------------------------------------------------------------------------------------
    // Small file helpers

    private static void setEnv(String name, String value) {
        try {
            Os.setenv(name, value, true);
        } catch (ErrnoException e) {
            Log.e(TAG, "Can't set " + name, e);
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[1 << 16];
        int count;
        while ((count = in.read(buffer)) > 0) {
            out.write(buffer, 0, count);
        }
    }

    private static String readText(File file) {
        try (InputStream in = new java.io.FileInputStream(file)) {
            byte[] bytes = new byte[(int) file.length()];
            int read = in.read(bytes);
            return new String(bytes, 0, Math.max(read, 0), "UTF-8");
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeText(File file, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes("UTF-8"));
        }
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        file.delete();
    }
}
