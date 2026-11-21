package com.dailytodo.app;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.Charset;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * 局域网同步（与 Windows 版同一套协议，可跨平台互通）：
 *   UDP 48710 广播 "DAILYTODO DISCOVER 1"，对端回 "DAILYTODO HERE <instId> <tcpPort> <loggedIn>"
 *   TCP 48711 上跑极简 HTTP：GET /ping、POST /sync，正文是行协议：
 *     SYNC 1 / EMAIL .. / PROOF .. / COUNT n / TASK id|title|kind|hour|minute|start|end|hasEnd|total|done|enabled|updated|deleted|lastFired / END
 */
public class Sync {
    public static final int UDP_PORT = 48710;
    public static final int TCP_PORT = 48711;
    private static final Charset UTF8 = Charset.forName("UTF-8");

    public static String instId = "";
    public static String status = "未登录";
    public static int peerCount = 0;
    public static volatile boolean running = false;
    private static Thread srvThread, discThread;
    private static Context appCtx;

    public static String sha256Hex(String s) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] d = md.digest(s.getBytes(UTF8));
            StringBuilder sb = new StringBuilder();
            for (byte b : d) sb.append(String.format(Locale.US, "%02x", b & 0xff));
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }

    public static String proofOf(String email, String password) {
        return sha256Hex(email.toLowerCase(Locale.US) + ":" + password);
    }

    // 识别码：与 Windows 端 core.cpp 的 ComputeTaskCode() 逐字节一致
    //   常驻：P|标题|HH:MM|开始日期|结束日期(或 -)
    //   临时：T|标题|HH:MM|执行次数
    // 取 SHA-256 前 16 位十六进制。done/enabled/lastFired 不参与，否则提醒一次就变。
    public static String taskCode(Task t) {
        String title = t.title == null ? "" : t.title.trim().replaceAll("\\s+", " ");
        String head = (t.kind == 1 ? "T|" : "P|") + title
                + String.format(Locale.US, "|%02d:%02d|", t.hour, t.minute);
        String key;
        if (t.kind == 1) {
            key = head + t.total;
        } else {
            String end = t.hasEnd && t.end != null && !t.end.isEmpty() ? t.end : "-";
            key = head + t.start + "|" + end;
        }
        String h = sha256Hex(key);
        return h.length() > 16 ? h.substring(0, 16) : h;
    }

    public static String taskCodeShort(Task t) {
        String c = taskCode(t);
        return c.length() > 8 ? c.substring(0, 8) : c;
    }

    // ---------------------------------------------------------------- 记录
    public static class Rec {
        public String id = "";
        public String title = "";
        public int kind, hour, minute, total, done;
        public String start = "", end = "", lastFired = "";
        public boolean hasEnd, enabled = true;
        public long updated, deleted;
        public Task toTask() {
            Task t = new Task();
            t.id = id; t.title = title; t.kind = kind; t.hour = hour; t.minute = minute;
            t.start = start; t.end = end; t.hasEnd = hasEnd;
            t.total = total; t.done = done; t.enabled = enabled; t.lastFired = lastFired;
            t.createdAt = updated;
            return t;
        }
    }

    public static Rec toRec(Task t, long updated, boolean deleted) {
        Rec r = new Rec();
        r.id = t.id; r.title = t.title; r.kind = t.kind; r.hour = t.hour; r.minute = t.minute;
        r.start = t.start; r.end = t.end; r.hasEnd = t.hasEnd; r.total = t.total; r.done = t.done;
        r.enabled = t.enabled; r.lastFired = t.lastFired; r.updated = updated; r.deleted = deleted ? 1 : 0;
        return r;
    }

    public static String buildPayload(String email, String proof, Map<String, Rec> recs) {
        StringBuilder sb = new StringBuilder();
        sb.append("SYNC 1\nEMAIL ").append(email).append("\nPROOF ").append(proof).append("\n");
        sb.append("COUNT ").append(recs.size()).append("\n");
        for (Rec r : recs.values()) {
            sb.append("TASK ").append(r.id).append('|')
              .append(r.title == null ? "" : r.title.replace("|", "/")).append('|')
              .append(r.kind).append('|').append(r.hour).append('|').append(r.minute).append('|')
              .append(r.start).append('|').append(r.end).append('|').append(r.hasEnd ? 1 : 0).append('|')
              .append(r.total).append('|').append(r.done).append('|').append(r.enabled ? 1 : 0).append('|')
              .append(r.updated).append('|').append(r.deleted).append('|')
              .append(r.lastFired == null ? "" : r.lastFired).append('\n');
        }
        sb.append("END\n");
        return sb.toString();
    }

    public static boolean parsePayload(String body, String[] emailProof, Map<String, Rec> out) {
        if (body == null) return false;
        for (String line : body.split("\n")) {
            line = line.trim();
            if (line.isEmpty()) continue;
            if (line.startsWith("EMAIL ")) emailProof[0] = line.substring(6).trim();
            else if (line.startsWith("PROOF ")) emailProof[1] = line.substring(6).trim();
            else if (line.startsWith("TASK ")) {
                String[] f = line.substring(5).split("\\|", -1);
                if (f.length < 14) continue;
                Rec r = new Rec();
                try {
                    r.id = f[0]; r.title = f[1];
                    r.kind = Integer.parseInt(f[2]);
                    r.hour = Integer.parseInt(f[3]);
                    r.minute = Integer.parseInt(f[4]);
                    r.start = f[5]; r.end = f[6];
                    r.hasEnd = "1".equals(f[7]);
                    r.total = Integer.parseInt(f[8]);
                    r.done = Integer.parseInt(f[9]);
                    r.enabled = "1".equals(f[10]);
                    r.updated = Long.parseLong(f[11]);
                    r.deleted = Long.parseLong(f[12]);
                    r.lastFired = f.length > 13 ? f[13] : "";
                    out.put(r.id, r);
                } catch (Exception ignore) {
                }
            }
        }
        return true;
    }

    // ---------------------------------------------------------------- 合并
    /** 把远端记录并进本地（updated 较新者胜，墓碑删本地）。返回是否有变化。 */
    public static boolean merge(Context c, String body) {
        String[] ep = new String[]{"", ""};
        Map<String, Rec> remote = new HashMap<String, Rec>();
        if (!parsePayload(body, ep, remote)) return false;

        String[] sess = Store.loadSession(c);
        if (sess[1] != null && !sess[1].isEmpty() && !sess[1].equals(ep[1])) return false;

        List<Task> list = Store.loadTasks(c);
        Map<String, long[]> meta = Store.loadMeta(c);
        boolean changed = false;
        for (Rec r : remote.values()) {
            Task local = null;
            for (Task t : list) if (t.id != null && t.id.equals(r.id)) { local = t; break; }
            long localUp = 0;
            long[] m = meta.get(r.id);
            if (m != null) localUp = m[0];
            else if (local != null) localUp = local.createdAt;
            if (r.updated <= localUp) continue;
            if (r.deleted > 0) {
                if (local != null) { list.remove(local); changed = true; }
                meta.put(r.id, new long[]{r.updated, 1});
            } else {
                Task nt = r.toTask();
                if (local == null) {
                    // 识别码相同 = 同一件待办（在另一台设备上独立建过），跳过不重复添加
                    String code = taskCode(nt);
                    boolean dup = false;
                    for (Task t : list) if (code.equals(taskCode(t))) { dup = true; break; }
                    if (dup) continue;
                    list.add(nt);
                }
                else {
                    local.title = nt.title; local.kind = nt.kind; local.hour = nt.hour;
                    local.minute = nt.minute; local.start = nt.start; local.end = nt.end;
                    local.hasEnd = nt.hasEnd; local.total = nt.total; local.done = nt.done;
                    local.enabled = nt.enabled; local.lastFired = nt.lastFired;
                }
                meta.put(r.id, new long[]{r.updated, 0});
                changed = true;
            }
        }
        if (changed) {
            Store.saveTasks(c, list);
            Store.saveMeta(c, meta);
        }
        return changed;
    }

    /** 识别码匹配：支持 8 位短码与 16 位全码，任意一方是对方前缀即算命中 */
    public static boolean codeMatch(String code, Task t) {
        if (t == null || code == null) return false;
        String full = taskCode(t);
        if (full.equals(code)) return true;
        if (full.startsWith(code)) return true;
        String short8 = full.length() > 8 ? full.substring(0, 8) : full;
        return code.startsWith(short8);
    }

    /**
     * 到局域网里按识别码找待办（先看本机，再问同一账号的其它设备）。
     * 找到返回那条待办（还没写进本地），找不到返回 null。
     */
    public static Task findByCode(Context c, String code) {
        if (code == null || code.isEmpty()) return null;
        for (Task t : Store.loadTasks(c)) {
            if (codeMatch(code, t)) return t;
        }
        String[] sess = Store.loadSession(c);
        if (sess[0] == null || sess[0].isEmpty()) return null;   // 没登录就没法问别人
        String body = buildPayload(sess[0], sess[1], new HashMap<String, Rec>());
        for (String ip : discover()) {
            String resp = httpPost(ip, TCP_PORT, "/sync", body, 2500);
            if (resp == null || resp.isEmpty()) continue;
            String[] ep = new String[]{"", ""};
            Map<String, Rec> recs = new HashMap<String, Rec>();
            parsePayload(resp, ep, recs);
            for (Rec r : recs.values()) {
                if (r.deleted > 0) continue;
                Task t = r.toTask();
                if (codeMatch(code, t)) return t;
            }
        }
        return null;
    }

    public static Map<String, Rec> localRecords(Context c) {        List<Task> list = Store.loadTasks(c);
        Map<String, long[]> meta = Store.loadMeta(c);
        Map<String, Rec> out = new HashMap<String, Rec>();
        for (Task t : list) {
            long up = t.createdAt;
            long[] m = meta.get(t.id);
            if (m != null && m[0] > up) up = m[0];
            out.put(t.id, toRec(t, up, false));
        }
        for (Map.Entry<String, long[]> e : meta.entrySet()) {
            if (e.getValue()[1] == 1 && !out.containsKey(e.getKey())) {
                Rec r = new Rec();
                r.id = e.getKey();
                r.updated = e.getValue()[0];
                r.deleted = 1;
                out.put(r.id, r);
            }
        }
        return out;
    }

    // ---------------------------------------------------------------- 网络
    private static String httpPost(String ip, int port, String path, String body, int timeoutMs) {
        Socket s = new Socket();
        try {
            s.connect(new InetSocketAddress(ip, port), timeoutMs);
            s.setSoTimeout(timeoutMs);
            byte[] b = body == null ? new byte[0] : body.getBytes(UTF8);
            String head = "POST " + path + " HTTP/1.1\r\nHost: " + ip + "\r\nContent-Type: text/plain\r\n"
                    + "Content-Length: " + b.length + "\r\nConnection: close\r\n\r\n";
            OutputStream out = s.getOutputStream();
            out.write(head.getBytes(UTF8));
            if (b.length > 0) out.write(b);
            out.flush();
            InputStream in = s.getInputStream();
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
            String resp = new String(bo.toByteArray(), UTF8);
            int p = resp.indexOf("\r\n\r\n");
            if (p < 0) return "";
            return resp.substring(p + 4);
        } catch (Exception e) {
            return "";
        } finally {
            try { s.close(); } catch (Exception ignore) { }
        }
    }

    public static boolean syncNow(Context c) {
        String[] sess = Store.loadSession(c);
        if (sess[0] == null || sess[0].isEmpty()) { status = "未登录"; return false; }
        Map<String, Rec> recs = localRecords(c);
        String body = buildPayload(sess[0], sess[1], recs);

        ArrayList<String> ips = discover();
        peerCount = ips.size();
        boolean any = false;
        for (String ip : ips) {
            String resp = httpPost(ip, TCP_PORT, "/sync", body, 2500);
            if (resp != null && !resp.isEmpty()) {
                merge(c, resp);
                any = true;
            }
        }
        if (peerCount == 0) status = "等待同一局域网内的其它设备";
        else status = any ? ("已与 " + peerCount + " 台设备同步") : "发现设备但同步失败";
        return any;
    }

    private static ArrayList<String> discover() {
        ArrayList<String> out = new ArrayList<String>();
        DatagramSocket s = null;
        try {
            s = new DatagramSocket();
            s.setBroadcast(true);
            s.setSoTimeout(700);
            byte[] b = "DAILYTODO DISCOVER 1".getBytes(UTF8);
            DatagramPacket p = new DatagramPacket(b, b.length, InetAddress.getByName("255.255.255.255"), UDP_PORT);
            s.send(p);
            long end = System.currentTimeMillis() + 800;
            byte[] buf = new byte[512];
            while (System.currentTimeMillis() < end) {
                DatagramPacket r = new DatagramPacket(buf, buf.length);
                s.receive(r);
                String msg = new String(buf, 0, r.getLength(), UTF8);
                if (msg.startsWith("DAILYTODO HERE ")) {
                    String[] f = msg.trim().split(" ");
                    if (f.length >= 5 && "1".equals(f[4])) {
                        String ip = r.getAddress().getHostAddress();
                        if (!out.contains(ip) && !ip.equals("127.0.0.1")) out.add(ip);
                        else if (!out.contains(ip) && ip.equals("127.0.0.1")) out.add(ip);
                    }
                }
            }
        } catch (Exception ignore) {
        } finally {
            if (s != null) s.close();
        }
        return out;
    }

    // ---------------------------------------------------------------- 服务端（让 Windows 端也能主动拉本机）
    private static void serveLoop() {
        ServerSocket ss = null;
        try {
            ss = new ServerSocket(TCP_PORT);
        } catch (Exception e) {
            return;   // 端口被占（例如同机已有 Windows 版在跑），不影响主动同步
        }
        while (running) {
            try {
                final Socket sock = ss.accept();
                new Thread(new Runnable() {
                    public void run() { handle(sock); }
                }).start();
            } catch (Exception e) {
                if (!running) break;
            }
        }
        try { if (ss != null) ss.close(); } catch (Exception ignore) { }
    }

    private static void handle(Socket sock) {
        try {
            sock.setSoTimeout(4000);
            InputStream in = sock.getInputStream();
            ByteArrayOutputStream head = new ByteArrayOutputStream();
            int c;
            int state = 0;
            while ((c = in.read()) >= 0) {
                head.write(c);
                if (state == 0 && c == '\r') state = 1;
                else if (state == 1 && c == '\n') state = 2;
                else if (state == 2 && c == '\r') state = 3;
                else if (state == 3 && c == '\n') break;
                else state = 0;
                if (head.size() > 65536) break;
            }
            String h = new String(head.toByteArray(), UTF8);
            String body = "";
            int cl = 0;
            for (String line : h.split("\r\n")) {
                if (line.toLowerCase(Locale.US).startsWith("content-length:")) {
                    try { cl = Integer.parseInt(line.substring(15).trim()); } catch (Exception ignore) { }
                }
            }
            if (cl > 0 && cl < 4 * 1024 * 1024) {
                byte[] b = new byte[cl];
                int got = 0;
                while (got < cl) {
                    int n = in.read(b, got, cl - got);
                    if (n <= 0) break;
                    got += n;
                }
                body = new String(b, 0, got, UTF8);
            }

            String resp = "auth\n";
            String[] sess = Store.loadSession(appCtx);
            String[] ep = new String[]{"", ""};
            Map<String, Rec> remote = new HashMap<String, Rec>();
            parsePayload(body, ep, remote);
            if (sess[0] != null && !sess[0].isEmpty() && sess[1] != null
                    && sess[0].equalsIgnoreCase(ep[0]) && sess[1].equals(ep[1])) {
                // 合并远端 → 回全量
                merge(appCtx, body);
                Map<String, Rec> all = localRecords(appCtx);
                resp = buildPayload(sess[0], sess[1], all);
            }

            byte[] rb = resp.getBytes(UTF8);
            String headOut = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
                    + "Content-Length: " + rb.length + "\r\nConnection: close\r\n\r\n";
            OutputStream out = sock.getOutputStream();
            out.write(headOut.getBytes(UTF8));
            out.write(rb);
            out.flush();
        } catch (Exception ignore) {
        } finally {
            try { sock.close(); } catch (Exception ignore) { }
        }
    }

    public static void start(Context c) {
        appCtx = c.getApplicationContext();
        if (instId.isEmpty()) instId = Long.toHexString(System.nanoTime());
        running = true;
        srvThread = new Thread(new Runnable() { public void run() { serveLoop(); } });
        srvThread.start();
        discThread = new Thread(new Runnable() {
            public void run() {
                while (running) {
                    try {
                        String[] sess = Store.loadSession(appCtx);
                        if (sess[0] != null && !sess[0].isEmpty() && Prefs.syncOn(appCtx)) {
                            syncNow(appCtx);
                        }
                    } catch (Throwable ignore) {
                    }
                    try { Thread.sleep(12000); } catch (InterruptedException e) { break; }
                }
            }
        });
        discThread.start();
    }

    public static void stop() {
        running = false;
        if (srvThread != null) srvThread.interrupt();
        if (discThread != null) discThread.interrupt();
    }
}
