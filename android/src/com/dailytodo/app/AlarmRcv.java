package com.dailytodo.app;

import android.content.Context;
import android.content.Intent;

import java.util.List;

/**
 * 到点触发：发通知 + 超级岛。计数不在这里加，改到「确认待办」页里加——
 * 用户点了「完成本次」才算完成一次，避免只是弹了提醒就自动占用次数。
 */
public class AlarmRcv extends android.content.BroadcastReceiver {
    @Override
    public void onReceive(Context c, Intent i) {
        String id = i.getStringExtra(Sched.EXTRA_ID);
        boolean delayed = Sched.ACTION_DELAY.equals(i.getAction());
        Task t = null;
        List<Task> list = Store.loadTasks(c);
        for (Task x : list) if (x.id != null && x.id.equals(id)) { t = x; break; }
        if (t == null || !t.enabled || t.finished()) return;
        if (Prefs.paused(c)) return;

        Sched.notifyTask(c, t, delayed);
        // 自绘灵动岛（系统超级岛需要小米白名单，自签包拿不到）
        Island.show(c, t.id, t.title, Sched.subOf(t, delayed));

        // 延后提醒不改动每日固定调度，只有正常到点才重排下一次
        if (!delayed) Sched.scheduleAll(c);
    }
}
