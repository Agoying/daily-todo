package com.dailytodo.app;

import android.content.Context;

/** 开机/更新后重新排闹钟。 */
public class BootRcv extends android.content.BroadcastReceiver {
    @Override
    public void onReceive(Context c, android.content.Intent i) {
        String a = i == null ? "" : i.getAction();
        if (android.content.Intent.ACTION_BOOT_COMPLETED.equals(a)
                || "android.intent.action.QUICKBOOT_POWERON".equals(a)
                || android.content.Intent.ACTION_MY_PACKAGE_REPLACED.equals(a)) {
            if (Prefs.autoStart(c)) Sched.scheduleAll(c);
        }
    }
}
