package io.github.hydriostatic.rabridge;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.format.DateUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Lists the installed Banjo apps and launches them with the bridge's Binder in the intent.
 * The RetroAchievements mod finds the Binder there and sends its requests through it.
 */
public final class MainActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;

    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            updateStatus();
            handler.postDelayed(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { Manifest.permission.POST_NOTIFICATIONS }, 0);
        }

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(24);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("RA Bridge");
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 28);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextColor(Color.rgb(0xF5, 0xCD, 0x60));
        root.addView(title);

        TextView about = new TextView(this);
        about.setText("Start Banjo from here to earn RetroAchievements. The game can't go online by itself, "
                + "so the RetroAchievements mod sends its requests through this app.\n\n"
                + "Close the game first if it's already open, then tap Play.");
        about.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        about.setPadding(0, dp(8), 0, dp(16));
        root.addView(about);

        PackageManager pm = getPackageManager();
        boolean any = false;
        Button first = null;
        for (String[] game : Games.KNOWN) {
            if (pm.getLaunchIntentForPackage(game[0]) == null) {
                continue;
            }
            any = true;
            Button play = new Button(this);
            play.setText("Play " + game[1]);
            play.setAllCaps(false);
            play.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
            final String packageName = game[0];
            play.setOnClickListener(v -> launch(packageName));
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            params.bottomMargin = dp(8);
            root.addView(play, params);
            if (first == null) {
                first = play;
            }
        }
        if (first != null) {
            first.requestFocus();
        }
        if (!any) {
            TextView none = new TextView(this);
            none.setText("No supported Banjo app is installed.");
            none.setTextColor(Color.rgb(0xFF, 0x8A, 0x80));
            root.addView(none);
        }

        Button stop = new Button(this);
        stop.setText("Stop the bridge");
        stop.setAllCaps(false);
        stop.setOnClickListener(v -> startService(new Intent(this, BridgeService.class).setAction(BridgeService.ACTION_STOP)));
        root.addView(stop);

        status = new TextView(this);
        status.setPadding(0, dp(16), 0, 0);
        status.setGravity(Gravity.START);
        root.addView(status);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
    }

    @Override
    protected void onResume() {
        super.onResume();
        handler.post(refresh);
    }

    @Override
    protected void onPause() {
        handler.removeCallbacks(refresh);
        super.onPause();
    }

    private void launch(String packageName) {
        Intent intent = getPackageManager().getLaunchIntentForPackage(packageName);
        if (intent == null) {
            Toast.makeText(this, "That app isn't installed.", Toast.LENGTH_SHORT).show();
            return;
        }

        startForegroundService(new Intent(this, BridgeService.class));

        Bundle extras = new Bundle();
        extras.putBinder(BridgeBinder.EXTRA_KEY, BridgeBinder.get(this));
        intent.putExtras(extras);
        // Not FLAG_ACTIVITY_CLEAR_TASK: the game can't restart inside a running process. If it's
        // already open this just brings it back, and the mod says the game must be restarted.
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
    }

    private void updateStatus() {
        StringBuilder text = new StringBuilder();
        text.append(BridgeService.running ? "Bridge: on" : "Bridge: off");
        int requests = BridgeBinder.requestCount.get();
        text.append("\nRequests relayed: ").append(requests);
        int failures = BridgeBinder.failureCount.get();
        if (failures > 0) {
            text.append(" (").append(failures).append(" failed)");
        }
        long last = BridgeBinder.lastRequestTime.get();
        if (last != 0) {
            text.append("\nLast request: ").append(DateUtils.getRelativeTimeSpanString(last));
        }
        status.setText(text);
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics());
    }
}
