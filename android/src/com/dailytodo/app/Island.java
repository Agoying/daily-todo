package com.dailytodo.app;

import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * 自绘「灵动岛」悬浮窗。
 *
 * 为什么不直接用小米超级岛：焦点通知（miui.focus.param）需要小米在服务端给应用开通
 * 白名单（官方 Q&A：发邮件到 mipush-permission@xiaomi.com 申请），自签名安装的包
 * 即使 hasFocusPermission 返回 true，系统也只把它当普通通知，岛不出现。
 * 所以这里用 SYSTEM_ALERT_WINDOW 自绘一个行为一致的岛：
 *   小岛：有待办要完成 → 点一下展开
 *   大岛：待办名称（超长省略号）+ 说明 → 再点一下进入应用的「确认待办」页
 * 同时通知里仍带 miui.focus.param，将来真拿到白名单也不用改代码。
 */
public final class Island {

    private static final int BG = 0xF2161616;
    private static final int ACCENT = 0xFF4CC2FF;
    private static final int TEXT = 0xFFF2F2F2;
    private static final int TEXT2 = 0xFFA8A8A8;

    private static WindowManager wm;
    private static FrameLayout root;
    private static LinearLayout pill;
    private static LinearLayout card;
    private static TextView cardTitle;
    private static TextView cardSub;
    private static String taskId;
    private static boolean expanded;
    private static final Handler H = new Handler(Looper.getMainLooper());
    private static final Runnable AUTO_HIDE = new Runnable() {
        public void run() { hide(); }
    };

    private Island() {
    }

    /** 是否已授予「显示在其他应用上层」权限 */
    public static boolean canDraw(Context c) {
        try {
            if (Build.VERSION.SDK_INT >= 23) return Settings.canDrawOverlays(c);
            return true;
        } catch (Throwable e) {
            return false;
        }
    }

    /** 跳系统设置让用户开启悬浮窗权限 */
    public static void requestPermission(Context c) {
        try {
            Intent i = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    android.net.Uri.parse("package:" + c.getPackageName()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            c.startActivity(i);
        } catch (Throwable e) {
            try {
                Intent i = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                c.startActivity(i);
            } catch (Throwable ignore) {
            }
        }
    }

    private static int dp(Context c, float v) {
        return (int) (v * c.getResources().getDisplayMetrics().density + 0.5f);
    }

    private static int statusBarHeight(Context c) {
        int id = c.getResources().getIdentifier("status_bar_height", "dimen", "android");
        if (id > 0) return c.getResources().getDimensionPixelSize(id);
        return dp(c, 28);
    }

    private static GradientDrawable round(int color, float r, Context c) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(c, r));
        g.setStroke(dp(c, 1), 0x33FFFFFF);
        return g;
    }

    /** 显示小岛（已有岛时只更新任务与文案，不重复添加窗口） */
    public static void show(Context ctx, String id, String title, String sub) {
        if (ctx == null || !canDraw(ctx)) return;
        final Context c = ctx.getApplicationContext();
        taskId = id;
        if (root != null) {
            update(c, title, sub);
            collapse();
            return;
        }
        try {
            wm = (WindowManager) c.getSystemService(Context.WINDOW_SERVICE);
            if (wm == null) return;

            root = new FrameLayout(c);

            // 小岛
            pill = new LinearLayout(c);
            pill.setOrientation(LinearLayout.HORIZONTAL);
            pill.setGravity(Gravity.CENTER_VERTICAL);
            pill.setPadding(dp(c, 14), dp(c, 8), dp(c, 16), dp(c, 8));
            pill.setBackground(round(BG, 22, c));
            pill.setElevation(dp(c, 8));
            TextView ic = new TextView(c);
            ic.setText("✓");
            ic.setTextColor(ACCENT);
            ic.setTextSize(15);
            ic.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            pill.addView(ic);
            TextView tv = new TextView(c);
            tv.setText("有待办要完成");
            tv.setTextColor(TEXT);
            tv.setTextSize(13);
            LinearLayout.LayoutParams tvLp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            tvLp.leftMargin = dp(c, 8);
            pill.addView(tv, tvLp);

            // 大岛
            card = new LinearLayout(c);
            card.setOrientation(LinearLayout.VERTICAL);
            card.setPadding(dp(c, 16), dp(c, 12), dp(c, 16), dp(c, 13));
            card.setBackground(round(BG, 20, c));
            card.setElevation(dp(c, 10));
            LinearLayout rowT = new LinearLayout(c);
            rowT.setOrientation(LinearLayout.HORIZONTAL);
            rowT.setGravity(Gravity.CENTER_VERTICAL);
            TextView ic2 = new TextView(c);
            ic2.setText("✓");
            ic2.setTextColor(ACCENT);
            ic2.setTextSize(16);
            ic2.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            rowT.addView(ic2);
            cardTitle = new TextView(c);
            cardTitle.setTextColor(TEXT);
            cardTitle.setTextSize(16);
            cardTitle.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            cardTitle.setSingleLine(true);
            cardTitle.setEllipsize(android.text.TextUtils.TruncateAt.END);
            LinearLayout.LayoutParams tLp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            tLp.leftMargin = dp(c, 8);
            rowT.addView(cardTitle, tLp);
            card.addView(rowT);
            cardSub = new TextView(c);
            cardSub.setTextColor(TEXT2);
            cardSub.setTextSize(12);
            LinearLayout.LayoutParams sLp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            sLp.topMargin = dp(c, 4);
            sLp.leftMargin = dp(c, 2);
            card.addView(cardSub, sLp);

            card.setVisibility(View.GONE);
            root.addView(pill);
            root.addView(card);

            root.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    if (!expanded) expand(c);
                    else openApp(c);
                }
            });

            WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    Build.VERSION.SDK_INT >= 26
                            ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                            : WindowManager.LayoutParams.TYPE_PHONE,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                            | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                    android.graphics.PixelFormat.TRANSLUCENT);
            lp.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
            lp.y = statusBarHeight(c) + dp(c, 6);
            wm.addView(root, lp);

            update(c, title, sub);
            root.setAlpha(0f);
            root.setScaleX(0.9f);
            root.setScaleY(0.9f);
            root.animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(180).start();
            H.removeCallbacks(AUTO_HIDE);
            H.postDelayed(AUTO_HIDE, 25000);
        } catch (Throwable e) {
            root = null;
            pill = null;
            card = null;
        }
    }

    private static void update(Context c, String title, String sub) {
        if (cardTitle == null) return;
        String t = title == null ? "" : title.trim();
        if (t.length() > 12) t = t.substring(0, 12) + "…";
        cardTitle.setText(t);
        cardSub.setText(sub == null ? "" : sub);
        cardTitle.setMaxWidth(dp(c, 240));
        cardSub.setMaxWidth(dp(c, 240));
    }

    private static void expand(final Context c) {
        if (root == null) return;
        expanded = true;
        H.removeCallbacks(AUTO_HIDE);
        H.postDelayed(AUTO_HIDE, 60000);
        pill.setVisibility(View.GONE);
        card.setVisibility(View.VISIBLE);
        card.setAlpha(0f);
        card.setScaleX(0.92f);
        card.animate().alpha(1f).scaleX(1f).setDuration(150).start();
    }

    private static void collapse() {
        if (root == null) return;
        expanded = false;
        card.setVisibility(View.GONE);
        pill.setVisibility(View.VISIBLE);
        H.removeCallbacks(AUTO_HIDE);
        H.postDelayed(AUTO_HIDE, 25000);
    }

    private static void openApp(Context c) {
        try {
            Intent i = new Intent(c, MainActivity.class);
            i.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP
                    | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            if (taskId != null) i.putExtra("confirm", taskId);
            c.startActivity(i);
        } catch (Throwable ignore) {
        }
        hide();
    }

    /** 移除岛（确认完成 / 延后 / 超时 都调用） */
    public static void hide() {
        H.removeCallbacks(AUTO_HIDE);
        if (root != null && wm != null) {
            try {
                wm.removeView(root);
            } catch (Throwable ignore) {
            }
        }
        root = null;
        pill = null;
        card = null;
        expanded = false;
        taskId = null;
    }
}
