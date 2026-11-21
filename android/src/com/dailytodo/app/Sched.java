package com.dailytodo.app;

import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.Icon;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.PowerManager;
import android.provider.Settings;

import java.util.Calendar;
import java.util.List;
import java.util.Locale;

/** 闹钟调度 + 系统通知 + 小米超级岛（灵动岛）通知。 */
public class Sched {
    public static final String CH_ID = "reminders";
    public static final String ACTION_FIRE = "com.dailytodo.app.FIRE";
    public static final String ACTION_DELAY = "com.dailytodo.app.DELAY";
    public static final String EXTRA_ID = "id";
    public static final String EXTRA_DELAY = "delayMin";

    /** 超级岛里引用的图片键名（对应 miui.focus.pics 里的 Bundle 键） */
    private static final String PIC_KEY = "miui.focus.pic_icon";

    public static void ensureChannel(Context c) {
        if (Build.VERSION.SDK_INT < 26) return;
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;
        NotificationChannel ch = nm.getNotificationChannel(CH_ID);
        if (ch == null) {
            ch = new NotificationChannel(CH_ID, "待办提醒", NotificationManager.IMPORTANCE_HIGH);
            ch.setDescription("到点提醒每日待办");
            ch.enableVibration(true);
            nm.createNotificationChannel(ch);
        }
    }

    private static PendingIntent pi(Context c, Task t, int flags) {
        Intent i = new Intent(c, AlarmRcv.class);
        i.setAction(ACTION_FIRE);
        i.putExtra(EXTRA_ID, t.id);
        int code = (t.id == null ? 0 : t.id.hashCode()) & 0x7fffffff;
        // Android 12（API 31）起必须显式声明可变性，否则直接抛 IllegalArgumentException
        int base = flags | PendingIntent.FLAG_IMMUTABLE;
        if ((flags & PendingIntent.FLAG_NO_CREATE) == 0) base |= PendingIntent.FLAG_UPDATE_CURRENT;
        return PendingIntent.getBroadcast(c, code, i, base);
    }

    /** 下一次触发的毫秒时间戳；-1 表示不再触发 */
    public static long nextTrigger(Task t, long nowMs) {
        if (!t.enabled || t.finished()) return -1;
        Calendar now = Calendar.getInstance();
        now.setTimeInMillis(nowMs);
        for (int addDay = 0; addDay <= 400; addDay++) {
            Calendar c = Calendar.getInstance();
            c.setTimeInMillis(nowMs);
            c.add(Calendar.DAY_OF_MONTH, addDay);
            c.set(Calendar.HOUR_OF_DAY, t.hour);
            c.set(Calendar.MINUTE, t.minute);
            c.set(Calendar.SECOND, 0);
            c.set(Calendar.MILLISECOND, 0);
            if (c.getTimeInMillis() <= nowMs + 500) continue;
            String d = String.format(Locale.US, "%04d-%02d-%02d", c.get(Calendar.YEAR),
                    c.get(Calendar.MONTH) + 1, c.get(Calendar.DAY_OF_MONTH));
            if (t.start != null && !t.start.isEmpty() && d.compareTo(t.start) < 0) continue;
            if (t.hasEnd && t.end != null && !t.end.isEmpty() && d.compareTo(t.end) > 0) return -1;
            return c.getTimeInMillis();
        }
        return -1;
    }

    public static void scheduleAll(Context c) {
        AlarmManager am = (AlarmManager) c.getSystemService(Context.ALARM_SERVICE);
        if (am == null) return;
        ensureChannel(c);
        List<Task> list = Store.loadTasks(c);
        long now = System.currentTimeMillis();
        for (Task t : list) {
            long when = nextTrigger(t, now);
            PendingIntent p = pi(c, t, PendingIntent.FLAG_NO_CREATE);
            if (when < 0) {
                if (p != null) am.cancel(p);
                continue;
            }
            PendingIntent target = pi(c, t, 0);
            fire(am, when, target);
        }
    }

    private static void fire(AlarmManager am, long when, PendingIntent p) {
        try {
            if (Build.VERSION.SDK_INT >= 31 && !am.canScheduleExactAlarms()) {
                am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, when, p);
            } else if (Build.VERSION.SDK_INT >= 23) {
                am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, when, p);
            } else {
                am.setExact(AlarmManager.RTC_WAKEUP, when, p);
            }
        } catch (SecurityException e) {
            am.set(AlarmManager.RTC_WAKEUP, when, p);
        }
    }

    /** 延后提醒：minutes 分钟后重新发一次同一条待办的提醒（不影响每日固定调度） */
    public static void scheduleDelay(Context c, String id, int minutes) {
        AlarmManager am = (AlarmManager) c.getSystemService(Context.ALARM_SERVICE);
        if (am == null || id == null) return;
        Intent i = new Intent(c, AlarmRcv.class);
        i.setAction(ACTION_DELAY);
        i.putExtra(EXTRA_ID, id);
        i.putExtra(EXTRA_DELAY, minutes);
        int code = ((id.hashCode() & 0x7fffffff) ^ 0x0F0F0F);
        PendingIntent p = PendingIntent.getBroadcast(c, code, i,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        fire(am, System.currentTimeMillis() + minutes * 60000L, p);
    }

    public static int notifId(String id) {
        return id == null ? 0x51A7 : (id.hashCode() & 0x3FFFFFFF);
    }

    public static void cancel(Context c, String id) {
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) {
            try {
                nm.cancel(notifId(id));
            } catch (Throwable ignore) {
            }
        }
    }

    // ------------------------------------------------------------ 超级岛
    /** 系统是否声明支持小米超级岛（OS1 及以上都返回 1/2/3） */
    public static int focusProtocol(Context c) {
        try {
            return Settings.System.getInt(c.getContentResolver(), "notification_focus_protocol", 0);
        } catch (Throwable e) {
            return 0;
        }
    }

    public static boolean islandSupported(Context c) {
        return focusProtocol(c) >= 1;
    }

    /** 应用自身 uid 查询「焦点通知」权限；无权限时岛通知会退化为普通通知 */
    public static boolean hasFocusPermission(Context c) {
        try {
            Bundle extras = new Bundle();
            extras.putString("package", c.getPackageName());
            Bundle r = c.getContentResolver().call(
                    Uri.parse("content://miui.statusbar.notification.public"), "canShowFocus", null, extras);
            return r != null && r.getBoolean("canShowFocus", false);
        } catch (Throwable e) {
            return false;
        }
    }

    /** 跳到本应用的通知设置页（用户可在那里开「焦点通知 / 允许通知」） */
    public static void openNotificationSettings(Context c) {
        try {
            Intent i = new Intent();
            if (Build.VERSION.SDK_INT >= 26) {
                i.setAction(Settings.ACTION_APP_NOTIFICATION_SETTINGS);
                i.putExtra(Settings.EXTRA_APP_PACKAGE, c.getPackageName());
            } else {
                i.setAction("android.settings.APP_NOTIFICATION_SETTINGS");
                i.putExtra("app_package", c.getPackageName());
                i.putExtra("app_uid", c.getApplicationInfo().uid);
            }
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            c.startActivity(i);
        } catch (Throwable ignore) {
        }
    }

    /** 按「字符数」截断，超出部分用省略号 */
    public static String trunc(String s, int max) {
        if (s == null) return "";
        String t = s.trim();
        if (t.length() <= max) return t;
        return t.substring(0, max) + "…";
    }

    private static String esc(String s) {
        if (s == null) return "";
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            switch (ch) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append(" "); break;
                case '\r': b.append(" "); break;
                case '\t': b.append(" "); break;
                default:
                    if (ch < 0x20) b.append(' ');
                    else b.append(ch);
            }
        }
        return b.toString();
    }

    /** 岛上的第二行说明 */
    public static String subOf(Task t, boolean delayed) {
        if (delayed) return "已延后提醒 · " + t.hhmm();
        if (t.isTemp()) return "临时代办 · " + t.hhmm() + " · 已完成 " + t.done + "/" + t.total;
        return "常驻待办 · 每天 " + t.hhmm();
    }

    /** 小岛 / 大岛用的 islandParams（小米超级岛 param_v2 协议） */
    public static String islandJson(Task t, boolean delayed) {
        String title = trunc(t.title, 14);
        String sub = subOf(t, delayed);
        String ticker = "有待办要完成";
        String pic = "\"picInfo\":{\"type\":1,\"pic\":\"" + PIC_KEY + "\"}";
        StringBuilder s = new StringBuilder();
        s.append("{\"param_v2\":{");
        s.append("\"protocol\":3,");
        s.append("\"business\":\"dailytodo\",");
        s.append("\"enableFloat\":true,");
        s.append("\"updatable\":true,");
        s.append("\"islandFirstFloat\":true,");
        s.append("\"filterWhenNoPermission\":false,");
        s.append("\"timeout\":5,");
        s.append("\"islandTimeout\":12,");
        s.append("\"ticker\":\"").append(esc(ticker)).append("\",");
        s.append("\"tickerPic\":\"").append(PIC_KEY).append("\",");
        s.append("\"aodTitle\":\"").append(esc(ticker)).append("\",");
        s.append("\"aodPic\":\"").append(PIC_KEY).append("\",");
        s.append("\"param_island\":{");
        s.append("\"islandProperty\":1,");
        s.append("\"bigIslandArea\":{\"imageTextInfoLeft\":{");
        s.append("\"type\":1,").append(pic).append(",");
        s.append("\"textInfo\":{\"title\":\"").append(esc(title))
                .append("\",\"content\":\"").append(esc(sub)).append("\",\"useHighLight\":false}");
        s.append("}},");
        s.append("\"smallIslandArea\":{\"type\":1,").append(pic)
                .append(",\"textInfo\":{\"title\":\"").append(esc(ticker)).append("\",\"useHighLight\":false}},");
        s.append("\"shareData\":{\"pic\":\"").append(PIC_KEY)
                .append("\",\"title\":\"").append(esc(title))
                .append("\",\"content\":\"").append(esc(sub))
                .append("\",\"shareContent\":\"").append(esc(title + " " + sub)).append("\"}");
        s.append("},");
        s.append("\"hintInfo\":{\"type\":1,\"title\":\"").append(esc(ticker)).append("\"},");
        s.append("\"baseInfo\":{\"title\":\"").append(esc(title))
                .append("\",\"content\":\"").append(esc(sub)).append("\",\"type\":2}");
        s.append("}}");
        return s.toString();
    }

    /** 到点提醒：通知 + 超级岛参数；点击进入「确认待办」页 */
    public static void notifyTask(Context c, Task t, boolean delayed) {
        ensureChannel(c);
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null || t == null) return;
        String body = subOf(t, delayed);

        Intent open = new Intent(c, MainActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP
                | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        open.putExtra("confirm", t.id);
        PendingIntent content = PendingIntent.getActivity(c, notifId(t.id), open,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) b = new Notification.Builder(c, CH_ID);
        else b = new Notification.Builder(c);
        b.setContentTitle(t.title)
                .setContentText(body)
                .setStyle(new Notification.BigTextStyle().bigText(body))
                .setSmallIcon(android.R.drawable.ic_popup_reminder)
                .setAutoCancel(true)
                .setOngoing(true)
                .setWhen(System.currentTimeMillis())
                .setShowWhen(true)
                .setContentIntent(content);
        if (Build.VERSION.SDK_INT < 26) {
            b.setPriority(Notification.PRIORITY_HIGH);
            b.setDefaults(Notification.DEFAULT_ALL);
        }
        Notification n = b.build();
        try {
            Bundle pics = new Bundle();
            pics.putParcelable(PIC_KEY, Icon.createWithResource(c, android.R.drawable.ic_popup_reminder));
            n.extras.putBundle("miui.focus.pics", pics);
            n.extras.putString("miui.focus.param", islandJson(t, delayed));
        } catch (Throwable ignore) {
            // 参数失败不影响普通通知
        }
        try {
            nm.notify(notifId(t.id), n);
        } catch (SecurityException ignore) {
            // 未授予通知权限时静默失败
        }
    }

    /** 设置页的「测试灵动岛」：优先拿一条真实待办，这样点进去能看到真正的确认页 */
    public static void notifyIslandTest(Context c) {
        Task t = null;
        try {
            for (Task x : Store.loadTasks(c)) {
                if (x != null && x.enabled) { t = x; break; }
            }
        } catch (Throwable ignore) {
        }
        if (t == null) {
            t = new Task();
            t.id = "island-test";
            t.title = "测试待办名称特别长用来验证省略号是否生效";
            t.kind = 0;
            t.hour = 9;
            t.minute = 30;
            t.total = 3;
            t.done = 1;
            t.enabled = true;
        }
        notifyTask(c, t, false);
        Island.show(c, t.id, t.title, subOf(t, false));
    }

    public static void show(Context c, String title, String body) {
        ensureChannel(c);
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;
        Intent open = new Intent(c, MainActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent content = PendingIntent.getActivity(c, 1, open,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) b = new Notification.Builder(c, CH_ID);
        else b = new Notification.Builder(c);
        b.setContentTitle(title)
                .setContentText(body)
                .setStyle(new Notification.BigTextStyle().bigText(body))
                .setSmallIcon(android.R.drawable.ic_popup_reminder)
                .setAutoCancel(true)
                .setWhen(System.currentTimeMillis())
                .setShowWhen(true)
                .setContentIntent(content);
        if (Build.VERSION.SDK_INT < 26) {
            b.setPriority(Notification.PRIORITY_HIGH);
            b.setDefaults(Notification.DEFAULT_ALL);
        }
        try {
            nm.notify((int) (System.currentTimeMillis() & 0xffff), b.build());
        } catch (SecurityException ignore) {
            // 未授予通知权限时静默失败
        }
    }

    /** 部分机型需要忽略电池优化，提醒才稳定 */
    public static void requestIgnoreBattery(Context c) {
        try {
            PowerManager pm = (PowerManager) c.getSystemService(Context.POWER_SERVICE);
            if (pm == null || pm.isIgnoringBatteryOptimizations(c.getPackageName())) return;
            Intent i = new Intent(android.provider.Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            i.setData(Uri.parse("package:" + c.getPackageName()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            c.startActivity(i);
        } catch (Exception ignore) {
        }
    }
}
