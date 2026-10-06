package io.github.hydriostatic.banjothor;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.WindowManager;

/**
 * Companion view for a second display, such as the AYN Thor's bottom screen. Without it that
 * screen would just sit there black (or show the launcher) while the game runs on top.
 *
 * The window is made non-focusable on purpose: a focused window on the other screen would
 * take the controller input away from the game.
 */
class SecondScreen {
    private static final String TAG = "BanjoThor";

    private final Activity activity;
    private CompanionPresentation presentation;

    SecondScreen(Activity activity) {
        this.activity = activity;
    }

    void show() {
        Display display = findSecondDisplay();
        if (display == null) {
            dismiss();
            return;
        }

        if (presentation != null && presentation.getDisplay().getDisplayId() == display.getDisplayId()) {
            return;
        }

        dismiss();
        try {
            presentation = new CompanionPresentation(activity, display);
            presentation.show();
        } catch (WindowManager.InvalidDisplayException e) {
            Log.w(TAG, "Second screen went away before it could be used", e);
            presentation = null;
        }
    }

    void dismiss() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
    }

    private Display findSecondDisplay() {
        DisplayManager manager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (manager == null) {
            return null;
        }

        Display current = activity.getWindowManager().getDefaultDisplay();
        for (Display display : manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() != current.getDisplayId()) {
                return display;
            }
        }

        return null;
    }

    private static class CompanionPresentation extends Presentation {
        CompanionPresentation(Context context, Display display) {
            super(context, display);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                    | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            setContentView(R.layout.second_screen);
        }
    }
}
