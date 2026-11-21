package com.dailytodo.app;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * 本地存储。文件格式与 Windows 版完全一致（UTF-8 + 制表符），便于两端互换：
 *   tasks_data.txt      T id title kind hour minute start end hasEnd total done enabled lastFired createdAt
 *   accounts.txt        email \t hash \t created
 *   account_session.txt email / hash / syncOn
 *   sync_meta.txt       id \t updated \t deleted
 */
public class Store {
    public static final String F_TASKS = "tasks_data.txt";
    public static final String F_ACC = "accounts.txt";
    public static final String F_SESS = "account_session.txt";
    public static final String F_META = "sync_meta.txt";

    public static File file(Context c, String name) {
        return new File(c.getFilesDir(), name);
    }

    public static String readAll(Context c, String name) {
        File f = file(c, name);
        if (!f.exists()) return "";
        StringBuilder sb = new StringBuilder();
        try {
            FileInputStream in = new FileInputStream(f);
            BufferedReader r = new BufferedReader(new InputStreamReader(in, "UTF-8"));
            String line;
            while ((line = r.readLine()) != null) sb.append(line).append('\n');
            r.close();
        } catch (IOException e) {
            return sb.toString();
        }
        return sb.toString();
    }

    public static void writeAll(Context c, String name, String data) {
        try {
            File f = file(c, name);
            File tmp = new File(c.getFilesDir(), name + ".tmp");
            FileOutputStream out = new FileOutputStream(tmp);
            Writer w = new OutputStreamWriter(out, "UTF-8");
            w.write(data);
            w.flush();
            w.close();
            if (f.exists()) f.delete();
            tmp.renameTo(f);
        } catch (IOException ignore) {
        }
    }

    private static String esc(String s) {
        if (s == null) return "";
        return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n");
    }

    private static String unesc(String s) {
        if (s == null) return "";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch == '\\' && i + 1 < s.length()) {
                char n = s.charAt(++i);
                if (n == 't') sb.append('\t');
                else if (n == 'n') sb.append('\n');
                else if (n == '\\') sb.append('\\');
                else sb.append(n);
            } else {
                sb.append(ch);
            }
        }
        return sb.toString();
    }

    public static List<Task> loadTasks(Context c) {
        List<Task> out = new ArrayList<Task>();
        String all = readAll(c, F_TASKS);
        if (all.isEmpty()) return out;
        for (String line : all.split("\n")) {
            if (line == null || line.isEmpty() || line.startsWith("#")) continue;
            String[] f = line.split("\t", -1);
            if (f.length < 14 || !"T".equals(f[0])) continue;
            Task t = new Task();
            try {
                t.id = f[1];
                t.title = unesc(f[2]);
                t.kind = Integer.parseInt(f[3]);
                t.hour = Integer.parseInt(f[4]);
                t.minute = Integer.parseInt(f[5]);
                t.start = f[6];
                t.end = f[7];
                t.hasEnd = "1".equals(f[8]);
                t.total = Integer.parseInt(f[9]);
                t.done = Integer.parseInt(f[10]);
                t.enabled = "1".equals(f[11]);
                t.lastFired = f[12];
                t.createdAt = Long.parseLong(f[13]);
                out.add(t);
            } catch (Exception ignore) {
            }
        }
        return out;
    }

    public static void saveTasks(Context c, List<Task> list) {
        StringBuilder sb = new StringBuilder();
        sb.append("# 每日待办 数据文件 v2 —— 每行一条，tab 分隔\n");
        sb.append("# T id title kind hour minute start end hasEnd total done enabled lastFired createdAt\n");
        for (Task t : list) {
            sb.append("T\t").append(t.id).append('\t').append(esc(t.title)).append('\t')
              .append(t.kind).append('\t').append(t.hour).append('\t').append(t.minute).append('\t')
              .append(t.start).append('\t').append(t.end).append('\t')
              .append(t.hasEnd ? 1 : 0).append('\t').append(t.total).append('\t').append(t.done).append('\t')
              .append(t.enabled ? 1 : 0).append('\t').append(t.lastFired).append('\t')
              .append(t.createdAt).append('\n');
        }
        writeAll(c, F_TASKS, sb.toString());
    }

    public static Map<String, String> loadAccounts(Context c) {
        Map<String, String> m = new HashMap<String, String>();
        String all = readAll(c, F_ACC);
        for (String line : all.split("\n")) {
            if (line == null || line.isEmpty() || line.startsWith("#")) continue;
            String[] f = line.split("\t", -1);
            if (f.length >= 2) m.put(f[0].toLowerCase(Locale.US), f[1]);
        }
        return m;
    }

    public static void saveAccounts(Context c, Map<String, String> m) {
        StringBuilder sb = new StringBuilder();
        sb.append("# 每日待办 账号文件 —— email \t hash(SHA256(email:password)) \t 创建时间\n");
        long now = System.currentTimeMillis() / 1000L;
        for (Map.Entry<String, String> e : m.entrySet()) {
            sb.append(e.getKey()).append('\t').append(e.getValue()).append('\t').append(now).append('\n');
        }
        writeAll(c, F_ACC, sb.toString());
    }

    /** 返回 {email, hash, syncOn} */
    public static String[] loadSession(Context c) {
        String all = readAll(c, F_SESS);
        String email = "", hash = "", sync = "1";
        for (String line : all.split("\n")) {
            String[] f = line.split("\t", -1);
            if (f.length >= 2) {
                if ("email".equals(f[0])) email = f[1];
                else if ("hash".equals(f[0])) hash = f[1];
                else if ("syncOn".equals(f[0])) sync = f[1];
            }
        }
        return new String[]{email, hash, sync};
    }

    public static void saveSession(Context c, String email, String hash, boolean syncOn) {
        writeAll(c, F_SESS, "email\t" + email + "\nhash\t" + hash + "\nsyncOn\t" + (syncOn ? "1" : "0") + "\n");
    }

    public static Map<String, long[]> loadMeta(Context c) {
        Map<String, long[]> m = new HashMap<String, long[]>();
        String all = readAll(c, F_META);
        for (String line : all.split("\n")) {
            if (line == null || line.isEmpty() || line.startsWith("#")) continue;
            String[] f = line.split("\t", -1);
            if (f.length >= 3) {
                try {
                    m.put(f[0], new long[]{Long.parseLong(f[1]), Long.parseLong(f[2])});
                } catch (Exception ignore) {
                }
            }
        }
        return m;
    }

    public static void saveMeta(Context c, Map<String, long[]> m) {
        StringBuilder sb = new StringBuilder();
        for (Map.Entry<String, long[]> e : m.entrySet()) {
            sb.append(e.getKey()).append('\t').append(e.getValue()[0]).append('\t')
              .append(e.getValue()[1]).append('\n');
        }
        writeAll(c, F_META, sb.toString());
    }

    public static String today() {
        return new SimpleDateFormat("yyyy-MM-dd", Locale.US).format(new Date());
    }

    public static String dateAfter(int days) {
        Calendar c = Calendar.getInstance();
        c.add(Calendar.DAY_OF_MONTH, days);
        return new SimpleDateFormat("yyyy-MM-dd", Locale.US).format(c.getTime());
    }

    public static String newId() {
        return String.format(Locale.US, "%012x", System.currentTimeMillis() ^ (System.nanoTime() << 8));
    }
}
