package com.dailytodo.app;

import java.util.Locale;

/** 一条待办。字段顺序与 Windows 版的 TSV 一一对应。 */
public class Task {
    public String id = "";
    public String title = "";
    public int kind = 0;              // 0=常驻 1=临时
    public int hour = 9;
    public int minute = 0;
    public String start = "";         // yyyy-MM-dd
    public String end = "";           // yyyy-MM-dd
    public boolean hasEnd = false;
    public int total = 3;
    public int done = 0;
    public boolean enabled = true;
    public String lastFired = "";     // yyyy-MM-dd HH:mm
    public long createdAt = 0;

    public boolean isTemp() {
        return kind == 1;
    }

    public boolean finished() {
        if (!enabled) return false;
        return isTemp() && done >= total;
    }

    public String hhmm() {
        return String.format(Locale.US, "%02d:%02d", hour, minute);
    }
}
