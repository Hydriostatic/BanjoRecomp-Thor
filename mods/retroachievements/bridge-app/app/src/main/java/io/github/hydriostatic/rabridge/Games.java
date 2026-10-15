package io.github.hydriostatic.rabridge;

/** The Banjo apps RA Bridge knows how to launch. Keep in sync with <queries> in the manifest. */
final class Games {
    static final String[][] KNOWN = {
        { "io.github.hydriostatic.banjothor", "Banjo: Recompiled (Thor)" },
        { "com.aure.banjorecomp", "Banjo: Recompiled (AurelioB)" },
    };

    static boolean isSupported(String packageName) {
        for (String[] game : KNOWN) {
            if (game[0].equals(packageName)) {
                return true;
            }
        }
        return false;
    }

    private Games() {}
}
