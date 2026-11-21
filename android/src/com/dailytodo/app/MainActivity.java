package com.dailytodo.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.DatePickerDialog;
import android.app.TimePickerDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.DatePicker;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.TimePicker;
import android.widget.Toast;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class MainActivity extends Activity {

    // 配色（与 Windows 版深色主题一致）
    static final int C_BG = 0xFF202020;
    static final int C_BG2 = 0xFF272727;
    static final int C_CARD = 0xFF2C2C2C;
    static final int C_CTRL = 0xFF333333;
    static final int C_TEXT = 0xFFF3F3F3;
    static final int C_TEXT2 = 0xFFA0A0A0;
    static final int C_TEXT3 = 0xFF8A8A8A;
    static final int C_ACCENT = 0xFF4CC2FF;
    static final int C_PERM = 0xFF4CC2FF;
    static final int C_TEMP = 0xFFF0C24A;
    static final int C_DANGER = 0xFFFF7A7A;

    private int page = 0;          // 0 待办 1 编辑 2 账号 3 设置
    private int editing = -1;      // -1 新增
    private String view = "timeline";

    private FrameLayout content;
    private View pageList, pageEdit, pageAccount, pageSettings, pageConfirm;
    private LinearLayout confirmWrap;
    private String confirmId;          // 「确认待办」页正在看的待办 id
    private LinearLayout navRow;
    private final List<View> navItems = new ArrayList<View>();

    // 编辑态
    private EditText etName;
    private int fHour = 9, fMinute = 0, fKind = 0, fTotal = 3;
    private String fStart = "", fEnd = "";
    private boolean fHasEnd = true;
    private TextView tvTime, tvStart, tvEnd, tvTotal, tvEditTitle;

    // 账号态
    private EditText etEmail, etPass;
    private String email = "", hash = "";
    private boolean loggedIn = false;

    private final SimpleDateFormat HM = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US);

    // ---------------------------------------------------------------- 基础
    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private GradientDrawable round(int color, int radiusDp) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(radiusDp));
        return g;
    }

    private GradientDrawable roundStroke(int color, int radiusDp, int strokeColor, int wDp) {
        GradientDrawable g = round(color, radiusDp);
        g.setStroke(dp(wDp), strokeColor);
        return g;
    }

    private TextView text(String s, int sizeSp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sizeSp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private Button button(String s, int bg, int fg) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextSize(14);
        b.setTextColor(fg);
        b.setBackground(round(bg, 8));
        b.setPadding(dp(14), dp(8), dp(14), dp(8));
        b.setMinHeight(dp(44));
        return b;
    }

    private LinearLayout row() {
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.HORIZONTAL);
        l.setGravity(Gravity.CENTER_VERTICAL);
        return l;
    }

    private LinearLayout col() {
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        return l;
    }

    private View gap(int h) {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(1, dp(h)));
        return v;
    }

    private EditText field(String hint, boolean password) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setTextSize(15);
        e.setTextColor(C_TEXT);
        e.setHintTextColor(C_TEXT3);
        e.setBackground(roundStroke(C_CTRL, 8, 0xFF3A3A3A, 1));
        e.setPadding(dp(12), dp(10), dp(12), dp(10));
        e.setSingleLine(true);
        if (password) e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        else e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        return e;
    }

    // ---------------------------------------------------------------- 生命周期
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().setStatusBarColor(C_BG);
        if (Build.VERSION.SDK_INT >= 21) getWindow().setNavigationBarColor(C_BG);

        loadAccount();
        // 不再自动写入示例待办：空列表就显示「还没有待办」，由用户自己添加
        // 通知渠道 / 同步 / 闹钟都不应该阻断界面启动：任何一步出问题都只影响提醒，不影响打开
        try {
            Sched.ensureChannel(this);
        } catch (Throwable ignore) {
        }
        try {
            Sync.start(getApplicationContext());
        } catch (Throwable ignore) {
        }

        LinearLayout root = col();
        root.setBackgroundColor(C_BG);
        root.setPadding(0, 0, 0, 0);

        // 顶部导航
        navRow = row();
        navRow.setBackgroundColor(C_BG2);
        navRow.setPadding(dp(8), dp(8), dp(8), dp(8));
        String[] titles = new String[]{"待办", "账号", "设置"};
        final int[] pages = new int[]{0, 2, 3};
        for (int i = 0; i < titles.length; i++) {
            final int target = pages[i];
            TextView tv = text(titles[i], 14, C_TEXT2, false);
            tv.setGravity(Gravity.CENTER);
            tv.setPadding(dp(10), dp(10), dp(10), dp(10));
            tv.setBackground(round(0x00000000, 8));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            tv.setLayoutParams(lp);
            tv.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) { go(target); }
            });
            navItems.add(tv);
            navRow.addView(tv);
        }
        root.addView(navRow, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        content = new FrameLayout(this);
        root.addView(content, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);

        buildListPage();
        buildEditPage();
        buildAccountPage();
        buildSettingsPage();
        buildConfirmPage();

        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission("android.permission.POST_NOTIFICATIONS") != 0) {
                requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"}, 11);
            }
        }
        go(0);
        handleIntent(getIntent());
        try {
            Sched.scheduleAll(this);
        } catch (Throwable ignore) {
            // 个别机型（尤其未授予“闹钟和提醒”权限时）可能抛异常，不应影响界面
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (page == 0) refreshList();
        if (page == 2) refreshAccountPage();
        if (page == 4) refreshConfirm();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIntent(intent);
    }

    /** 从通知 / 超级岛点进来时带 confirm=<id>，直接打开「确认待办」页 */
    private void handleIntent(Intent intent) {
        if (intent == null) return;
        String id = intent.getStringExtra("confirm");
        if (id == null || id.isEmpty()) return;
        intent.removeExtra("confirm");
        openConfirm(id);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
    }

    private long lastBackAt = 0;

    /**
     * 系统返回（屏幕左/右边缘滑动、三键导航的返回）＝应用内「返回上一级」：
     * 修改页 / 确认页 → 待办列表；账号页 / 设置页 → 先回待办并提示再滑一次退出；
     * 已在待办列表时，2 秒内连滑两次才真正退出。
     */
    @Override
    public void onBackPressed() {
        if (page == 1 || page == 4) {
            go(0);
            return;
        }
        if (page == 2 || page == 3) {
            go(0);
            Toast.makeText(this, "已回到待办列表，再滑一次退出", Toast.LENGTH_SHORT).show();
            return;
        }
        long now = System.currentTimeMillis();
        if (now - lastBackAt < 2000) {
            super.onBackPressed();
            return;
        }
        lastBackAt = now;
        Toast.makeText(this, "再滑一次返回退出每日待办", Toast.LENGTH_SHORT).show();
    }

    private void go(int p) {
        page = p;
        pageList.setVisibility(p == 0 ? View.VISIBLE : View.GONE);
        pageEdit.setVisibility(p == 1 ? View.VISIBLE : View.GONE);
        pageAccount.setVisibility(p == 2 ? View.VISIBLE : View.GONE);
        pageSettings.setVisibility(p == 3 ? View.VISIBLE : View.GONE);
        pageConfirm.setVisibility(p == 4 ? View.VISIBLE : View.GONE);
        for (int i = 0; i < navItems.size(); i++) {
            int[] pages = new int[]{0, 2, 3};
            boolean on = pages[i] == p;
            navItems.get(i).setBackground(round(on ? C_CARD : 0x00000000, 8));
            ((TextView) navItems.get(i)).setTextColor(on ? C_ACCENT : C_TEXT2);
        }
        if (p == 0) refreshList();
        if (p == 2) refreshAccountPage();
        if (p == 3) refreshSettings();
        if (p == 4) refreshConfirm();
    }

    // ---------------------------------------------------------------- 确认待办
    private void buildConfirmPage() {
        ScrollView sc = new ScrollView(this);
        LinearLayout wrap = col();
        wrap.setPadding(dp(16), dp(16), dp(16), dp(24));
        sc.addView(wrap);
        confirmWrap = wrap;
        pageConfirm = sc;
        content.addView(sc, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        pageConfirm.setVisibility(View.GONE);
    }

    private void openConfirm(String id) {
        confirmId = id;
        go(4);
    }

    private Task findTask(String id) {
        if (id == null) return null;
        for (Task t : tasks()) if (id.equals(t.id)) return t;
        return null;
    }

    /** 确认待办页：待办详情 + 完成本次 + 延后 5/10/15 分钟 */
    private void refreshConfirm() {
        if (confirmWrap == null) return;
        confirmWrap.removeAllViews();
        final Task t = findTask(confirmId);
        if (t == null) {
            confirmWrap.addView(text("这条待办已经不存在了", 16, C_TEXT, true));
            confirmWrap.addView(gap(12));
            Button back = button("返回列表", C_CTRL, C_TEXT);
            back.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) { go(0); }
            });
            confirmWrap.addView(back);
            return;
        }

        confirmWrap.addView(text("确认待办", 26, C_TEXT, true));
        confirmWrap.addView(text("到点提醒 · 确认完成或延后提醒", 13, C_TEXT2, false));
        confirmWrap.addView(gap(14));

        LinearLayout box = col();
        box.setBackground(round(C_CARD, 12));
        box.setPadding(dp(14), dp(14), dp(14), dp(14));

        LinearLayout titleRow = row();
        View bar = new View(this);
        bar.setBackground(round(t.isTemp() ? C_TEMP : C_PERM, 2));
        titleRow.addView(bar, new LinearLayout.LayoutParams(dp(4), dp(22)));
        TextView tvTitle = text(t.title, 20, C_TEXT, true);
        tvTitle.setPadding(dp(10), 0, 0, 0);
        titleRow.addView(tvTitle, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        box.addView(titleRow);
        box.addView(gap(8));

        String meta = t.isTemp()
                ? ("临时代办 · " + t.hhmm() + " · 已完成 " + t.done + " / " + t.total + " 次")
                : ("常驻待办 · 每天 " + t.hhmm() + (t.hasEnd && t.end != null && !t.end.isEmpty()
                        ? (" · 到 " + t.end) : " · 长期有效"));
        box.addView(text(meta, 13, C_TEXT2, false));

        String code = Sync.taskCodeShort(t);
        if (code != null && !code.isEmpty()) box.addView(text("识别码 #" + code, 12, C_TEXT3, false));
        String fired = t.lastFired == null || t.lastFired.isEmpty() ? "" : ("上次提醒 " + t.lastFired);
        if (!fired.isEmpty()) box.addView(text(fired, 12, C_TEXT3, false));
        confirmWrap.addView(box);
        confirmWrap.addView(gap(16));

        Button done = button(t.isTemp() ? "完成本次（计入 1 次）" : "完成本次", C_ACCENT, 0xFF10202A);
        done.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { confirmDone(t); }
        });
        confirmWrap.addView(done);
        confirmWrap.addView(gap(14));

        confirmWrap.addView(text("稍后再提醒我", 14, C_TEXT, false));
        confirmWrap.addView(gap(8));
        LinearLayout delayRow = row();
        int[] mins = new int[]{5, 10, 15};
        for (int i = 0; i < mins.length; i++) {
            final int m = mins[i];
            Button b = button("延后 " + m + " 分钟", C_CTRL, C_TEXT);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            if (i > 0) lp.leftMargin = dp(8);
            b.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) { confirmDelay(t, m); }
            });
            delayRow.addView(b, lp);
        }
        confirmWrap.addView(delayRow);
        confirmWrap.addView(gap(12));

        Button back = button("返回列表", 0x00000000, C_TEXT2);
        back.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { go(0); }
        });
        confirmWrap.addView(back);
    }

    private void confirmDone(Task t) {
        List<Task> list = tasks();
        for (Task x : list) {
            if (x.id != null && x.id.equals(t.id)) {
                x.lastFired = HM.format(new Date());
                if (x.isTemp() && x.done < x.total) x.done++;
                break;
            }
        }
        Store.saveTasks(this, list);
        touch(t);
        Sched.cancel(this, t.id);
        Island.hide();
        Toast.makeText(this, t.isTemp() ? "已完成 1 次，进度已更新" : "已确认完成", Toast.LENGTH_SHORT).show();
        Sched.scheduleAll(this);
        confirmId = null;
        go(0);
    }

    private void confirmDelay(Task t, int minutes) {
        Sched.scheduleDelay(this, t.id, minutes);
        Sched.cancel(this, t.id);
        Island.hide();
        Toast.makeText(this, "将在 " + minutes + " 分钟后再次提醒", Toast.LENGTH_SHORT).show();
        confirmId = null;
        go(0);
    }

    // ---------------------------------------------------------------- 数据
    private List<Task> tasks() {
        return Store.loadTasks(this);
    }

    private long updatedOf(Task t) {
        Map<String, long[]> meta = Store.loadMeta(this);
        long[] m = meta.get(t.id);
        return (m != null && m[0] > t.createdAt) ? m[0] : t.createdAt;
    }

    private void touch(Task t) {
        Map<String, long[]> meta = Store.loadMeta(this);
        meta.put(t.id, new long[]{System.currentTimeMillis() / 1000L, 0});
        Store.saveMeta(this, meta);
    }

    private void bury(String id) {
        Map<String, long[]> meta = Store.loadMeta(this);
        meta.put(id, new long[]{System.currentTimeMillis() / 1000L, 1});
        Store.saveMeta(this, meta);
    }

    // ---------------------------------------------------------------- 列表页
    private void buildListPage() {
        ScrollView sc = new ScrollView(this);
        LinearLayout wrap = col();
        wrap.setPadding(dp(16), dp(16), dp(16), dp(24));
        sc.addView(wrap);
        pageList = sc;
        content.addView(sc, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private void refreshList() {
        ScrollView sc = (ScrollView) pageList;
        LinearLayout wrap = (LinearLayout) sc.getChildAt(0);
        wrap.removeAllViews();

        List<Task> list = tasks();
        Task next = null;
        long best = Long.MAX_VALUE;
        long now = System.currentTimeMillis();
        for (Task t : list) {
            long w = Sched.nextTrigger(t, now);
            if (w > 0 && w < best) { best = w; next = t; }
        }

        TextView h1 = text("今日待办", 26, C_TEXT, true);
        wrap.addView(h1);
        String sub = "共 " + list.size() + " 项";
        if (next != null) {
            long mins = Math.max(1, (best - now) / 60000);
            sub += " · 下一个 " + next.hhmm() + "（" + prettyLeft(mins) + "后）";
        }
        wrap.addView(text(sub, 13, C_TEXT2, false));
        wrap.addView(gap(12));

        // 视图切换
        LinearLayout seg = row();
        seg.setBackground(round(C_CTRL, 8));
        seg.setPadding(dp(4), dp(4), dp(4), dp(4));
        final TextView b1 = segTab("时间线");
        final TextView b2 = segTab("分栏");
        seg.addView(b1, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        seg.addView(b2, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        b1.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { view = "timeline"; Prefs.setView(MainActivity.this, view); refreshList(); }
        });
        b2.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { view = "split"; Prefs.setView(MainActivity.this, view); refreshList(); }
        });
        b1.setBackground(round("timeline".equals(view) ? C_CARD : 0x00000000, 6));
        b1.setTextColor("timeline".equals(view) ? C_ACCENT : C_TEXT2);
        b2.setBackground(round("split".equals(view) ? C_CARD : 0x00000000, 6));
        b2.setTextColor("split".equals(view) ? C_ACCENT : C_TEXT2);
        wrap.addView(seg);
        wrap.addView(gap(12));

        if ("timeline".equals(view)) {
            final List<Task> ordered = new ArrayList<Task>(list);
            Collections.sort(ordered, new Comparator<Task>() {
                public int compare(Task a, Task b) {
                    long wa = Sched.nextTrigger(a, System.currentTimeMillis());
                    long wb = Sched.nextTrigger(b, System.currentTimeMillis());
                    if (wa < 0) wa = Long.MAX_VALUE;
                    if (wb < 0) wb = Long.MAX_VALUE;
                    return wa < wb ? -1 : (wa > wb ? 1 : 0);
                }
            });
            if (ordered.isEmpty()) wrap.addView(emptyHint());
            for (Task t : ordered) wrap.addView(card(t, false));
        } else {
            LinearLayout cols = row();
            cols.setGravity(Gravity.TOP);
            LinearLayout left = col();
            LinearLayout right = col();
            left.addView(text("常驻待办", 13, C_TEXT2, true));
            left.addView(gap(8));
            right.addView(text("临时代办", 13, C_TEXT2, true));
            right.addView(gap(8));
            for (Task t : list) {
                if (t.isTemp()) right.addView(card(t, true));
                else left.addView(card(t, true));
            }
            LinearLayout.LayoutParams lpL = new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            lpL.rightMargin = dp(6);
            LinearLayout.LayoutParams lpR = new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            lpR.leftMargin = dp(6);
            cols.addView(left, lpL);
            cols.addView(right, lpR);
            wrap.addView(cols);
        }

        wrap.addView(gap(16));
        Button add = button("＋ 添加待办", C_ACCENT, 0xFF10202A);
        add.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { openEdit(-1); }
        });
        wrap.addView(add);
    }

    private TextView segTab(String s) {
        TextView t = text(s, 14, C_TEXT2, false);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(8), dp(10), dp(8), dp(10));
        return t;
    }

    private View emptyHint() {
        LinearLayout l = col();
        l.setGravity(Gravity.CENTER);
        l.setPadding(0, dp(60), 0, dp(60));
        TextView t1 = text("还没有待办", 16, C_TEXT, true);
        t1.setGravity(Gravity.CENTER);
        TextView t2 = text("点下面的「添加待办」创建第一条", 13, C_TEXT3, false);
        t2.setGravity(Gravity.CENTER);
        l.addView(t1);
        l.addView(gap(6));
        l.addView(t2);
        return l;
    }

    private String prettyLeft(long mins) {
        if (mins < 60) return mins + " 分钟";
        long h = mins / 60, m = mins % 60;
        if (h < 24) return m > 0 ? (h + " 小时 " + m + " 分") : (h + " 小时");
        long d = h / 24;
        return d + " 天 " + (h % 24) + " 小时";
    }

    private View card(final Task t, final boolean narrow) {
        // 竖排卡片：第一行=色条+信息+剩余时间，第二行=右对齐的 修改/删除
        LinearLayout box = col();
        box.setBackground(round(C_CARD, 10));
        box.setPadding(dp(10), dp(10), dp(10), dp(8));
        LinearLayout.LayoutParams boxLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        boxLp.bottomMargin = dp(10);
        box.setLayoutParams(boxLp);

        LinearLayout top = row();

        View bar = new View(this);
        bar.setBackground(round(t.isTemp() ? C_TEMP : C_PERM, 2));
        LinearLayout.LayoutParams barLp = new LinearLayout.LayoutParams(dp(4), dp(42));
        barLp.rightMargin = dp(10);
        top.addView(bar, barLp);

        LinearLayout info = col();
        LinearLayout.LayoutParams infoLp = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        top.addView(info, infoLp);
        info.addView(text(t.title, 17, t.finished() ? C_TEXT3 : C_TEXT, true));
        String meta = t.isTemp()
                ? ("临时 · " + t.hhmm() + " · 已执行 " + t.done + "/" + t.total + " 次")
                : ("常驻 · 每天 " + t.hhmm() + (t.hasEnd ? (" · 至 " + t.end) : " · 长期有效"));
        TextView metaTv = text(meta, 12, C_TEXT2, false);
        metaTv.setPadding(0, dp(3), 0, 0);
        info.addView(metaTv);
        // 识别码不在列表露出，统一放到修改页的「更多选项」里（可复制）

        long w = Sched.nextTrigger(t, System.currentTimeMillis());
        String chip = t.finished() ? "已完成"
                : (w > 0 ? (prettyLeft(Math.max(1, (w - System.currentTimeMillis()) / 60000)) + "后") : "已暂停");
        TextView chipTv = text(chip, 11, C_ACCENT, false);
        chipTv.setBackground(round(0x224CC2FF, 10));
        chipTv.setPadding(dp(8), dp(3), dp(8), dp(4));
        LinearLayout.LayoutParams chipLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        chipLp.leftMargin = dp(6);
        top.addView(chipTv, chipLp);
        box.addView(top);

        LinearLayout btns = row();
        btns.setGravity(Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams btnsLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        btnsLp.topMargin = dp(8);

        TextView edit = linkBtn(narrow ? "改" : "修改", C_TEXT);
        edit.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { openEdit(indexOf(t.id)); }
        });
        TextView del = linkBtn(narrow ? "删" : "删除", C_DANGER);
        del.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { confirmDelete(t); }
        });
        btns.addView(edit);
        View sp = new View(this);
        btns.addView(sp, new LinearLayout.LayoutParams(dp(8), 1));
        btns.addView(del);
        box.addView(btns, btnsLp);
        return box;
    }

    /** 卡片里的小文字按钮（比 button() 矮，避免挤掉信息区） */
    private TextView linkBtn(String s, int fg) {
        TextView tv = text(s, 13, fg, false);
        tv.setGravity(Gravity.CENTER);
        tv.setPadding(dp(14), dp(6), dp(14), dp(6));
        tv.setBackground(round(C_CTRL, 8));
        return tv;
    }

    private int indexOf(String id) {
        List<Task> l = tasks();
        for (int i = 0; i < l.size(); i++) if (l.get(i).id.equals(id)) return i;
        return -1;
    }

    private void confirmDelete(final Task t) {
        new AlertDialog.Builder(this)
                .setTitle("删除待办")
                .setMessage("确定删除「" + t.title + "」吗？局域网内的其它设备也会同步删除。")
                .setNegativeButton("取消", null)
                .setPositiveButton("删除", new android.content.DialogInterface.OnClickListener() {
                    public void onClick(android.content.DialogInterface d, int w) {
                        List<Task> l = tasks();
                        for (int i = 0; i < l.size(); i++) {
                            if (l.get(i).id.equals(t.id)) { l.remove(i); break; }
                        }
                        bury(t.id);
                        Store.saveTasks(MainActivity.this, l);
                        Sched.scheduleAll(MainActivity.this);
                        refreshList();
                    }
                }).show();
    }

    // ---------------------------------------------------------------- 编辑页
    private void buildEditPage() {
        ScrollView sc = new ScrollView(this);
        LinearLayout wrap = col();
        wrap.setPadding(dp(16), dp(16), dp(16), dp(24));
        sc.addView(wrap);

        TextView back = text("‹ 返回", 14, C_TEXT2, false);
        back.setPadding(0, dp(4), 0, dp(12));
        back.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { go(0); }
        });
        wrap.addView(back);

        tvEditTitle = text("添加待办", 24, C_TEXT, true);
        wrap.addView(tvEditTitle);
        wrap.addView(gap(14));

        // ---- 导入待办：粘贴识别码一键导入（识别码来自另一台设备的「更多选项 → 复制」）
        impRow = row();
        impRow.setGravity(Gravity.CENTER_VERTICAL);
        etCode = field("粘贴识别码，例如 87bcd3b6", false);
        impRow.addView(etCode, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView impBtn = linkBtn("导入", C_ACCENT);
        LinearLayout.LayoutParams impLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        impLp.leftMargin = dp(8);
        impRow.addView(impBtn, impLp);
        wrap.addView(impRow);
        TextView impHint = text("粘贴另一台设备「更多选项 → 复制」的识别码，一键把那条例待办导进来",
                11, C_TEXT3, false);
        impHint.setPadding(0, dp(6), 0, 0);
        wrap.addView(impHint);
        impHintRow = impHint;
        impBtn.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { doImport(); }
        });
        wrap.addView(gap(14));

        wrap.addView(label("待办名称"));
        etName = field("例如：喝水、起来活动一下", false);
        wrap.addView(etName);
        wrap.addView(gap(12));

        wrap.addView(label("提醒时间（每天）"));
        tvTime = text("09:00", 16, C_TEXT, true);
        tvTime.setBackground(roundStroke(C_CTRL, 8, 0xFF3A3A3A, 1));
        tvTime.setPadding(dp(14), dp(12), dp(14), dp(12));
        tvTime.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { pickTime(); }
        });
        wrap.addView(tvTime);
        wrap.addView(gap(12));

        wrap.addView(label("待办类型"));
        final RadioGroup rg = new RadioGroup(this);
        rg.setOrientation(RadioGroup.HORIZONTAL);
        RadioButton r1 = new RadioButton(this);
        r1.setText("常驻待办");
        r1.setTextColor(C_TEXT);
        r1.setId(1001);
        RadioButton r2 = new RadioButton(this);
        r2.setText("临时代办");
        r2.setTextColor(C_TEXT);
        r2.setId(1002);
        rg.addView(r1);
        rg.addView(r2);
        rg.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            public void onCheckedChanged(RadioGroup g, int id) {
                fKind = (id == 1002) ? 1 : 0;
                refreshEditVisibility();
            }
        });
        wrap.addView(rg);
        rgKind = rg;
        wrap.addView(gap(8));

        LinearLayout dateBox = col();
        dateBox.addView(label("生效区间"));
        LinearLayout dr = row();
        tvStart = dateBtn("");
        tvEnd = dateBtn("");
        dr.addView(tvStart, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        dr.addView(tvEnd, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        dateBox.addView(dr);
        final CheckBox cb = new CheckBox(this);
        cb.setText("无结束日期（长期有效）");
        cb.setTextColor(C_TEXT2);
        cb.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(android.widget.CompoundButton b, boolean v) {
                fHasEnd = !v;
                tvEnd.setText(fHasEnd ? fEnd : "无结束日期");
                tvEnd.setAlpha(fHasEnd ? 1f : 0.45f);
            }
        });
        dateBox.addView(cb);
        wrap.addView(dateBox);

        LinearLayout cntBox = row();
        cntBox.addView(label("执行次数"));
        final TextView minus = button("−", C_CTRL, C_TEXT);
        final TextView plus = button("+", C_CTRL, C_TEXT);
        tvTotal = text("3 次", 15, C_TEXT, true);
        tvTotal.setPadding(dp(12), 0, dp(12), 0);
        minus.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { if (fTotal > 1) fTotal--; tvTotal.setText(fTotal + " 次"); }
        });
        plus.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { if (fTotal < 99) fTotal++; tvTotal.setText(fTotal + " 次"); }
        });
        cntBox.addView(minus);
        cntBox.addView(tvTotal);
        cntBox.addView(plus);
        wrap.addView(cntBox);

        // ---- 更多选项（默认收起）：识别码 + 复制按钮
        moreHeader = text("更多选项  ▾", 14, C_TEXT2, false);
        moreHeader.setPadding(0, dp(16), 0, dp(8));
        wrap.addView(moreHeader);

        moreBox = col();
        moreBox.setVisibility(View.GONE);
        LinearLayout codeRow = row();
        codeRow.setGravity(Gravity.CENTER_VERTICAL);
        codeRow.addView(text("识别码", 14, C_TEXT2, false), new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        tvCode = text("—", 13, C_TEXT, false);
        codeRow.addView(tvCode);
        TextView copy = linkBtn("复制", C_ACCENT);
        LinearLayout.LayoutParams copyLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        copyLp.leftMargin = dp(10);
        codeRow.addView(copy, copyLp);
        copy.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { copyTaskCode(); }
        });
        moreBox.addView(codeRow);
        TextView codeHint = text("名称、时间、类型、区间/次数相同的待办识别码相同，同步时据此去重",
                11, C_TEXT3, false);
        codeHint.setPadding(0, dp(4), 0, 0);
        moreBox.addView(codeHint);
        wrap.addView(moreBox);

        moreHeader.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                boolean show = moreBox.getVisibility() != View.VISIBLE;
                moreBox.setVisibility(show ? View.VISIBLE : View.GONE);
                moreHeader.setText(show ? "更多选项  ▴" : "更多选项  ▾");
                if (show) refreshCodeText();
            }
        });

        wrap.addView(gap(18));
        LinearLayout btns = row();
        Button cancel = button("取消", C_CTRL, C_TEXT);
        Button save = button("保存", C_ACCENT, 0xFF10202A);
        cancel.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { go(0); }
        });
        save.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { commitEdit(); }
        });
        LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        bl.rightMargin = dp(8);
        btns.addView(cancel, bl);
        btns.addView(save, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        wrap.addView(btns);

        pageEdit = sc;
        content.addView(sc, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        pageEdit.setVisibility(View.GONE);
        this.editDateBox = dateBox;
        this.editCountBox = cntBox;
        this.editNoEndCb = cb;
    }

    private LinearLayout editDateBox, editCountBox;
    private CheckBox editNoEndCb;
    private TextView moreHeader, tvCode;
    private LinearLayout moreBox;
    private EditText etCode;

    /** 导入待办：先查本机是否已有同码（提示不重复导入），再去局域网里找，找到就落库 */
    private void doImport() {
        String raw = etCode == null ? "" : etCode.getText().toString().trim().toLowerCase(Locale.US);
        raw = raw.replace("#", "").replace(" ", "").replace("\n", "").replace("\r", "");
        if (raw.length() < 8) {
            Toast.makeText(this, "识别码至少 8 位，请重新粘贴", Toast.LENGTH_SHORT).show();
            return;
        }
        final String code = raw;
        for (Task t : tasks()) {
            if (Sync.codeMatch(code, t)) {
                Toast.makeText(this, "已有相同待办：「" + t.title + "」（#" + Sync.taskCodeShort(t)
                        + "），无需重复导入", Toast.LENGTH_LONG).show();
                return;
            }
        }
        Toast.makeText(this, "正在局域网内查找…", Toast.LENGTH_SHORT).show();
        new Thread(new Runnable() {
            public void run() {
                Task got = null;
                Throwable err = null;
                try {
                    got = Sync.findByCode(MainActivity.this, code);
                } catch (Throwable e) {
                    err = e;
                }
                final Task found = got;
                final Throwable error = err;
                runOnUiThread(new Runnable() {
                    public void run() {
                        if (error != null) {
                            Toast.makeText(MainActivity.this, "导入失败：" + error.getMessage(),
                                    Toast.LENGTH_LONG).show();
                        } else {
                            afterImport(found);
                        }
                    }
                });
            }
        }).start();
    }

    private void afterImport(Task found) {
        if (found == null) {
            Toast.makeText(this, "没找到这个识别码对应的待办：请确认另一台设备在线、已登录同一账号并开启局域网同步",
                    Toast.LENGTH_LONG).show();
            return;
        }
        List<Task> l = tasks();
        // 再查一次重（刚才的同步线程可能刚好把它并进来了）
        for (Task t : l) {
            if (Sync.codeMatch(Sync.taskCode(t), found)) {
                Toast.makeText(this, "已有相同待办：「" + t.title + "」，无需重复导入",
                        Toast.LENGTH_LONG).show();
                return;
            }
        }
        Task nt = new Task();
        nt.id = Store.newId();
        nt.title = found.title;
        nt.kind = found.kind;
        nt.hour = found.hour;
        nt.minute = found.minute;
        nt.start = found.start;
        nt.end = found.end;
        nt.hasEnd = found.hasEnd;
        nt.total = found.total;
        nt.done = 0;
        nt.enabled = true;
        nt.lastFired = "";
        nt.createdAt = System.currentTimeMillis() / 1000L;
        l.add(nt);
        Store.saveTasks(this, l);
        touch(nt);
        Sched.scheduleAll(this);
        etCode.setText("");
        Toast.makeText(this, "已导入「" + nt.title + "」", Toast.LENGTH_LONG).show();
        go(0);
    }
    private RadioGroup rgKind;
    private LinearLayout impRow;
    private TextView impHintRow;

    /** 用当前表单内容算识别码（与保存后的结果一致） */
    private String formCode() {
        Task t = new Task();
        t.title = etName == null ? "" : etName.getText().toString();
        if (t.title.trim().isEmpty()) return "";
        t.kind = fKind;
        t.hour = fHour;
        t.minute = fMinute;
        t.total = fTotal;
        t.start = fStart;
        t.end = fEnd;
        t.hasEnd = fHasEnd;
        return Sync.taskCode(t);
    }

    private void refreshCodeText() {
        if (tvCode == null) return;
        String c = formCode();
        tvCode.setText(c.isEmpty() ? "先填写待办名称" : c);
    }

    private void copyTaskCode() {
        String c = formCode();
        if (c.isEmpty()) {
            Toast.makeText(this, "先填写待办名称", Toast.LENGTH_SHORT).show();
            return;
        }
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("每日待办识别码", c));
        Toast.makeText(this, "识别码已复制：" + c, Toast.LENGTH_SHORT).show();
    }

    private TextView label(String s) {
        TextView t = text(s, 13, C_TEXT2, false);
        t.setPadding(0, dp(4), 0, dp(6));
        return t;
    }

    private TextView dateBtn(String s) {
        TextView t = text(s, 14, C_TEXT, false);
        t.setGravity(Gravity.CENTER);
        t.setBackground(roundStroke(C_CTRL, 8, 0xFF3A3A3A, 1));
        t.setPadding(dp(10), dp(12), dp(10), dp(12));
        t.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { }
        });
        return t;
    }

    private void refreshEditVisibility() {
        editDateBox.setVisibility(fKind == 0 ? View.VISIBLE : View.GONE);
        editCountBox.setVisibility(fKind == 1 ? View.VISIBLE : View.GONE);
    }

    private void pickTime() {
        TimePickerDialog d = new TimePickerDialog(this, new TimePickerDialog.OnTimeSetListener() {
            public void onTimeSet(TimePicker tp, int h, int m) {
                fHour = h; fMinute = m;
                tvTime.setText(String.format(Locale.US, "%02d:%02d", h, m));
            }
        }, fHour, fMinute, true);
        d.show();
    }

    private void pickDate(final boolean isStart) {
        Calendar c = Calendar.getInstance();
        DatePickerDialog d = new DatePickerDialog(this, new DatePickerDialog.OnDateSetListener() {
            public void onDateSet(DatePicker dp, int y, int m, int day) {
                String s = String.format(Locale.US, "%04d-%02d-%02d", y, m + 1, day);
                if (isStart) { fStart = s; tvStart.setText(s); }
                else { fEnd = s; tvEnd.setText(s); }
            }
        }, c.get(Calendar.YEAR), c.get(Calendar.MONTH), c.get(Calendar.DAY_OF_MONTH));
        d.show();
    }

    private void openEdit(int idx) {
        List<Task> l = tasks();
        editing = idx;
        boolean adding = !(idx >= 0 && idx < l.size());
        // 导入只在「添加待办」时出现：修改页上导入会插进别的待办，容易误操作
        if (impRow != null) impRow.setVisibility(adding ? View.VISIBLE : View.GONE);
        if (impHintRow != null) impHintRow.setVisibility(adding ? View.VISIBLE : View.GONE);
        if (idx >= 0 && idx < l.size()) {
            Task t = l.get(idx);
            etName.setText(t.title);
            fHour = t.hour; fMinute = t.minute;
            fKind = t.kind; fTotal = t.total;
            fStart = t.start; fEnd = t.end; fHasEnd = t.hasEnd;
            tvEditTitle.setText("修改待办");
            editNoEndCb.setChecked(!t.hasEnd);
        } else {
            etName.setText("");
            Calendar c = Calendar.getInstance();
            fHour = c.get(Calendar.HOUR_OF_DAY);
            fMinute = 0;
            fKind = 0; fTotal = 3;
            fStart = Store.today(); fEnd = Store.dateAfter(30); fHasEnd = false;
            tvEditTitle.setText("添加待办");
            editNoEndCb.setChecked(true);
        }
        tvTime.setText(String.format(Locale.US, "%02d:%02d", fHour, fMinute));
        tvStart.setText(fStart);
        tvStart.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { pickDate(true); }
        });
        tvEnd.setText(fHasEnd ? fEnd : "无结束日期");
        tvEnd.setAlpha(fHasEnd ? 1f : 0.45f);
        tvEnd.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { if (fHasEnd) pickDate(false); }
        });
        tvTotal.setText(fTotal + " 次");
        if (rgKind != null) rgKind.check(fKind == 1 ? 1002 : 1001);
        refreshEditVisibility();
        moreBox.setVisibility(View.GONE);
        moreHeader.setText("更多选项  ▾");
        refreshCodeText();
        go(1);
    }

    private void commitEdit() {
        String name = etName.getText().toString().trim();
        if (name.isEmpty()) {
            Toast.makeText(this, "请填写待办名称", Toast.LENGTH_SHORT).show();
            return;
        }
        List<Task> l = tasks();
        Task t;
        if (editing >= 0 && editing < l.size()) {
            t = l.get(editing);
            touch(t);
        } else {
            t = new Task();
            t.id = Store.newId();
            t.createdAt = System.currentTimeMillis() / 1000L;
            l.add(t);
        }
        t.title = name;
        t.hour = fHour; t.minute = fMinute;
        t.kind = fKind; t.total = fTotal;
        t.start = fStart; t.end = fEnd; t.hasEnd = fHasEnd;
        t.enabled = true;

        // 识别码去重：内容相同（名称/时间/类型/区间或次数）视为同一条，不重复添加
        String code = Sync.taskCode(t);
        Task dup = null;
        for (Task o : l) {
            if (o == t) continue;
            if (code.equals(Sync.taskCode(o))) { dup = o; break; }
        }
        if (dup != null) {
            if (editing < 0) l.remove(t);
            Store.saveTasks(this, l);
            Toast.makeText(this, "这条待办已经存在（#" + Sync.taskCodeShort(dup) + "），已跳过",
                    Toast.LENGTH_SHORT).show();
            Sched.scheduleAll(this);
            go(0);
            return;
        }

        Store.saveTasks(this, l);
        if (editing >= 0 && editing < l.size()) touch(t);
        Sched.scheduleAll(this);
        go(0);
    }

    // ---------------------------------------------------------------- 账号页
    private void loadAccount() {
        String[] sess = Store.loadSession(this);
        email = sess[0];
        hash = sess[1];
        loggedIn = !(email == null || email.isEmpty() || hash == null || hash.isEmpty());
    }

    private void buildAccountPage() {
        ScrollView sc = new ScrollView(this);
        LinearLayout wrap = col();
        wrap.setPadding(dp(16), dp(16), dp(16), dp(24));
        sc.addView(wrap);
        wrap.setTag("accwrap");
        pageAccount = sc;
        content.addView(sc, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        pageAccount.setVisibility(View.GONE);
    }

    private void refreshAccountPage() {
        ScrollView sc = (ScrollView) pageAccount;
        LinearLayout wrap = (LinearLayout) sc.getChildAt(0);
        wrap.removeAllViews();
        loadAccount();

        wrap.addView(text("账号", 26, C_TEXT, true));
        wrap.addView(text("同一邮箱登录的设备，在同一局域网内自动同步待办", 13, C_TEXT2, false));
        wrap.addView(gap(14));

        LinearLayout cardBox = col();
        cardBox.setBackground(round(C_CARD, 12));
        cardBox.setPadding(dp(14), dp(14), dp(14), dp(14));

        if (!loggedIn) {
            cardBox.addView(label("邮箱"));
            etEmail = field("you@example.com", false);
            cardBox.addView(etEmail);
            cardBox.addView(gap(10));
            cardBox.addView(label("密码"));
            etPass = field("至少 6 位", true);
            cardBox.addView(etPass);
            cardBox.addView(gap(14));

            LinearLayout btns = row();
            Button reg = button("注册并登录", C_ACCENT, 0xFF10202A);
            Button login = button("登录", C_CTRL, C_TEXT);
            reg.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) { doAuth(true); }
            });
            login.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) { doAuth(false); }
            });
            LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            bl.rightMargin = dp(8);
            btns.addView(reg, bl);
            btns.addView(login, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            cardBox.addView(btns);
            cardBox.addView(gap(10));
            cardBox.addView(text("账号保存在本机，密码只以 SHA-256 摘要形式保存，不会联网上传", 11, C_TEXT3, false));
        } else {
            cardBox.addView(label("当前账号"));
            cardBox.addView(text(email, 17, C_TEXT, true));
            cardBox.addView(gap(6));
            String st = Sync.status;
            if (Sync.peerCount > 0) st = "已连接 " + Sync.peerCount + " 台设备";
            cardBox.addView(text(st, 13, C_TEXT2, false));
            cardBox.addView(gap(12));

            LinearLayout swRow = row();
            swRow.addView(text("局域网同步", 15, C_TEXT, false),
                    new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            Switch sw = new Switch(this);
            sw.setChecked(Prefs.syncOn(this) && !"0".equals(Store.loadSession(this)[2]));
            sw.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
                public void onCheckedChanged(android.widget.CompoundButton b, boolean v) {
                    Prefs.setSyncOn(MainActivity.this, v);
                    String[] s = Store.loadSession(MainActivity.this);
                    Store.saveSession(MainActivity.this, s[0], s[1], v);
                }
            });
            swRow.addView(sw);
            cardBox.addView(swRow);
            cardBox.addView(gap(12));

            LinearLayout btns = row();
            Button syncNow = button("立即同步", C_ACCENT, 0xFF10202A);
            Button logout = button("退出登录", C_CTRL, C_TEXT);
            syncNow.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    final Button b = (Button) v;
                    b.setText("同步中…");
                    new Thread(new Runnable() {
                        public void run() {
                            Sync.syncNow(getApplicationContext());
                            runOnUiThread(new Runnable() {
                                public void run() {
                                    refreshAccountPage();
                                    refreshList();
                                }
                            });
                        }
                    }).start();
                }
            });
            logout.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    Store.saveSession(MainActivity.this, "", "", false);
                    Sync.status = "未登录";
                    refreshAccountPage();
                }
            });
            LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            bl.rightMargin = dp(8);
            btns.addView(syncNow, bl);
            btns.addView(logout, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            cardBox.addView(btns);
            cardBox.addView(gap(10));
            cardBox.addView(text("局域网内使用同一邮箱登录的设备会自动交换任务（较新的改动生效）", 11, C_TEXT3, false));
        }
        wrap.addView(cardBox);
    }

    private void doAuth(boolean register) {
        String e = etEmail.getText().toString().trim().toLowerCase(Locale.US);
        String p = etPass.getText().toString();
        if (!e.contains("@") || e.length() < 5) {
            Toast.makeText(this, "请输入正确的邮箱", Toast.LENGTH_SHORT).show();
            return;
        }
        if (p.length() < 6) {
            Toast.makeText(this, "密码至少 6 位", Toast.LENGTH_SHORT).show();
            return;
        }
        String h = Sync.proofOf(e, p);
        Map<String, String> acc = Store.loadAccounts(this);
        String exist = acc.get(e);
        if (register) {
            if (exist != null && !exist.equals(h)) {
                Toast.makeText(this, "该邮箱已注册，密码不正确", Toast.LENGTH_SHORT).show();
                return;
            }
            acc.put(e, h);
            Store.saveAccounts(this, acc);
        } else {
            if (exist == null) {
                Toast.makeText(this, "该邮箱尚未注册，请先注册", Toast.LENGTH_SHORT).show();
                return;
            }
            if (!exist.equals(h)) {
                Toast.makeText(this, "密码不正确", Toast.LENGTH_SHORT).show();
                return;
            }
        }
        Store.saveSession(this, e, h, true);
        email = e;
        hash = h;
        loggedIn = true;
        Sync.status = "等待同一局域网内的其它设备";
        Toast.makeText(this, register ? "注册成功，已自动登录" : "登录成功", Toast.LENGTH_SHORT).show();
        refreshAccountPage();
    }

    // ---------------------------------------------------------------- 设置页
    private View pageSettingsView;
    private Switch swAuto, swSound, swPause;
    private TextView tvIsland;

    private void buildSettingsPage() {
        ScrollView sc = new ScrollView(this);
        LinearLayout wrap = col();
        wrap.setPadding(dp(16), dp(16), dp(16), dp(24));
        sc.addView(wrap);

        wrap.addView(text("设置", 26, C_TEXT, true));
        wrap.addView(gap(14));

        LinearLayout p1 = col();
        p1.setBackground(round(C_CARD, 12));
        p1.setPadding(dp(14), dp(6), dp(14), dp(6));

        swAuto = addSwitch(p1, "开机自动启动", "开机后自动恢复提醒（需系统允许自启动）");
        swAuto.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(android.widget.CompoundButton b, boolean v) {
                Prefs.setAutoStart(MainActivity.this, v);
                if (v) {
                    Sched.scheduleAll(MainActivity.this);
                    Sched.requestIgnoreBattery(MainActivity.this);
                }
            }
        });
        swSound = addSwitch(p1, "提醒时播放提示音", "系统通知默认会响铃，关闭后静音提醒");
        swSound.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(android.widget.CompoundButton b, boolean v) {
                Prefs.setSound(MainActivity.this, v);
            }
        });
        swPause = addSwitch(p1, "暂停全部提醒", "临时关闭提醒调度，待办数据完整保留");
        swPause.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(android.widget.CompoundButton b, boolean v) {
                Prefs.setPaused(MainActivity.this, v);
                Sched.scheduleAll(MainActivity.this);
            }
        });
        wrap.addView(p1);
        wrap.addView(gap(12));

        LinearLayout p2 = col();
        p2.setBackground(round(C_CARD, 12));
        p2.setPadding(dp(14), dp(12), dp(14), dp(12));
        p2.addView(text("数据文件", 15, C_TEXT, false));
        p2.addView(text("保存在应用私有目录，可导出备份", 12, C_TEXT2, false));
        p2.addView(gap(8));
        Button export = button("导出数据到「下载」", C_CTRL, C_TEXT);
        export.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { exportData(); }
        });
        p2.addView(export);
        wrap.addView(p2);
        wrap.addView(gap(12));

        Button test = button("发送测试通知", C_ACCENT, 0xFF10202A);
        test.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                Sched.show(MainActivity.this, "每日待办", "这是一条测试通知 —— 能看到它说明提醒通道正常");
                Toast.makeText(MainActivity.this, "已发送测试通知", Toast.LENGTH_SHORT).show();
            }
        });
        wrap.addView(test);
        wrap.addView(gap(12));

        LinearLayout p3 = col();
        p3.setBackground(round(C_CARD, 12));
        p3.setPadding(dp(14), dp(12), dp(14), dp(12));
        p3.addView(text("灵动岛（悬浮提醒）", 15, C_TEXT, false));
        tvIsland = text("", 12, C_TEXT2, false);
        p3.addView(tvIsland);
        p3.addView(gap(10));
        LinearLayout islRow = row();
        Button bSet = button("悬浮窗权限", C_CTRL, C_TEXT);
        bSet.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                if (Island.canDraw(MainActivity.this)) {
                    Toast.makeText(MainActivity.this, "悬浮窗权限已开启", Toast.LENGTH_SHORT).show();
                } else {
                    Island.requestPermission(MainActivity.this);
                }
            }
        });
        Button bIsl = button("测试灵动岛", C_CTRL, C_TEXT);
        bIsl.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                Sched.notifyIslandTest(MainActivity.this);
                Toast.makeText(MainActivity.this, "已发送，注意看屏幕顶部", Toast.LENGTH_SHORT).show();
            }
        });
        LinearLayout.LayoutParams lpA = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        LinearLayout.LayoutParams lpB = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        lpB.leftMargin = dp(8);
        islRow.addView(bSet, lpA);
        islRow.addView(bIsl, lpB);
        p3.addView(islRow);
        wrap.addView(p3);

        wrap.addView(gap(14));
        wrap.addView(text("每日待办 安卓版 1.3 · 常驻 / 临时双视图 · 灵动岛 · 识别码导入 · 局域网同步", 11, C_TEXT3, false));

        pageSettings = sc;
        pageSettingsView = wrap;
        content.addView(sc, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        pageSettings.setVisibility(View.GONE);
    }

    private Switch addSwitch(LinearLayout parent, String title, String sub) {
        LinearLayout row = col();
        row.setPadding(0, dp(10), 0, dp(10));
        LinearLayout top = row();
        TextView t = text(title, 15, C_TEXT, false);
        top.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        Switch sw = new Switch(this);
        top.addView(sw);
        row.addView(top);
        row.addView(text(sub, 12, C_TEXT2, false));
        parent.addView(row);
        return sw;
    }

    private void refreshSettings() {
        swAuto.setChecked(Prefs.autoStart(this));
        swSound.setChecked(Prefs.sound(this));
        swPause.setChecked(Prefs.paused(this));
        if (tvIsland != null) {
            if (!Island.canDraw(this)) {
                tvIsland.setText("未开启悬浮窗权限 —— 到点只会发通知，屏幕顶部不会浮出小岛。"
                        + "点下面的「悬浮窗权限」允许显示在其他应用上层即可");
            } else {
                tvIsland.setText("已开启 · 待办到点时屏幕顶部浮出小岛「有待办要完成」，"
                        + "点一下展开显示待办名称（太长会省略），再点一下进入确认页" +
                        (Sched.islandSupported(this) && Sched.hasFocusPermission(this)
                                ? "（系统超级岛权限已具备，若系统侧已白名单则同时走系统岛）" : ""));
            }
        }
    }

    private void exportData() {
        try {
            java.io.File dir = android.os.Environment.getExternalStoragePublicDirectory(
                    android.os.Environment.DIRECTORY_DOWNLOADS);
            if (!dir.exists()) dir.mkdirs();
            java.io.File out = new java.io.File(dir, "tasks_data.txt");
            java.io.FileOutputStream fos = new java.io.FileOutputStream(out);
            fos.write(Store.readAll(this, Store.F_TASKS).getBytes("UTF-8"));
            fos.close();
            Toast.makeText(this, "已导出到 " + out.getAbsolutePath(), Toast.LENGTH_LONG).show();
        } catch (Exception e) {
            Toast.makeText(this, "导出失败：" + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }
}
