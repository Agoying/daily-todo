package com.dailytodo.app;

import android.content.Context;
import android.content.SharedPreferences;

/** 设置项。 */
public class Prefs {
    private static SharedPreferences sp(Context c) {
        return c.getSharedPreferences("settings", Context.MODE_PRIVATE);
    }

    public static boolean autoStart(Context c) {
        return sp(c).getBoolean("autoStart", true);
    }

    public static void setAutoStart(Context c, boolean v) {
        sp(c).edit().putBoolean("autoStart", v).apply();
    }

    public static boolean sound(Context c) {
        return sp(c).getBoolean("sound", true);
    }

    public static void setSound(Context c, boolean v) {
        sp(c).edit().putBoolean("sound", v).apply();
    }

    public static boolean paused(Context c) {
        return sp(c).getBoolean("paused", false);
    }

    public static void setPaused(Context c, boolean v) {
        sp(c).edit().putBoolean("paused", v).apply();
    }

    public static boolean syncOn(Context c) {
        return sp(c).getBoolean("syncOn", true);
    }

    public static void setSyncOn(Context c, boolean v) {
        sp(c).edit().putBoolean("syncOn", v).apply();
    }

    public static String view(Context c) {
        return sp(c).getString("view", "timeline");
    }

    public static void setView(Context c, String v) {
        sp(c).edit().putString("view", v).apply();
    }
}
