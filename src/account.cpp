// 每日待办 — 账号（邮箱注册 / 登录）与局域网同步
//
// 设计要点
//  * 账号是本机保存的：邮箱 + SHA-256(email + ":" + 密码)。
//    密码不落盘；跨设备用同一个邮箱+密码就会得到同一个 hash，
//    这个 hash 同时充当局域网同步的凭据（同一局域网内配对）。
//  * 同步走局域网：UDP 48710 广播发现同伴，TCP 48711 交换任务快照。
//    每条任务带 updated 时间戳与 deleted 墓碑，按"较新者胜"合并。
//  * 网络线程只负责收发与合并到共享快照，UI 线程负责把结果并回 g_tasks。
#include "account.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <map>
#include <vector>
#include <string>

#include <ctime>
using std::map;
using std::string;
using namespace Gdiplus;

// ---------------------------------------------------------------- 小工具

static HotFn g_hotFn = nullptr;
void AccountSetHotCallback(HotFn fn) { g_hotFn = fn; }
static void Hot(int id, RECT r) { if (g_hotFn) g_hotFn(id, r); }

static wstring g_dir;   // 程序目录（含尾部反斜杠）

static wstring DirPath()
{
    if (!g_dir.empty()) return g_dir;
    wstring f = DataFilePath();
    size_t pos = f.find_last_of(L'\\');
    g_dir = (pos == wstring::npos) ? wstring(L".\\") : f.substr(0, pos + 1);
    return g_dir;
}

static wstring PathOf(const wchar_t* name) { return DirPath() + name; }


static string ToUtf8(const wstring& s)
{
    if (s.empty()) return string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

static wstring FromUtf8(const string& s)
{
    if (s.empty()) return wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

// ---------------------------------------------------------------- SHA-256

namespace {

struct Sha256
{
    unsigned int  h[8];
    unsigned char buf[64];
    unsigned long long bits;
    size_t        len;

    Sha256() { Reset(); }

    void Reset()
    {
        static const unsigned int init[8] = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        };
        for (int i = 0; i < 8; ++i) h[i] = init[i];
        bits = 0; len = 0;
    }

    static unsigned int Ror(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

    void Block(const unsigned char* p)
    {
        static const unsigned int k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
        };
        unsigned int w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16) |
                   ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            unsigned int s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            unsigned int s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            unsigned int S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
            unsigned int ch = (e & f) ^ (~e & g);
            unsigned int t1 = hh + S1 + ch + k[i] + w[i];
            unsigned int S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
            unsigned int mj = (a & b) ^ (a & c) ^ (b & c);
            unsigned int t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void Update(const unsigned char* p, size_t n)
    {
        bits += (unsigned long long)n * 8;
        while (n) {
            size_t take = 64 - len;
            if (take > n) take = n;
            memcpy(buf + len, p, take);
            len += take; p += take; n -= take;
            if (len == 64) { Block(buf); len = 0; }
        }
    }

    void Final(unsigned char out[32])
    {
        unsigned long long b = bits;
        unsigned char pad = 0x80;
        Update(&pad, 1);
        unsigned char zero = 0;
        while (len != 56) Update(&zero, 1);
        unsigned char lenbuf[8];
        for (int i = 0; i < 8; ++i) lenbuf[i] = (unsigned char)(b >> (56 - i * 8));
        Update(lenbuf, 8);
        for (int i = 0; i < 8; ++i) {
            out[i * 4]     = (unsigned char)(h[i] >> 24);
            out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
            out[i * 4 + 2] = (unsigned char)(h[i] >> 8);
            out[i * 4 + 3] = (unsigned char)(h[i]);
        }
    }
};

} // namespace

wstring Sha256Hex(const wstring& text)
{
    string utf8 = ToUtf8(text);
    Sha256 s;
    s.Update((const unsigned char*)utf8.data(), utf8.size());
    unsigned char dig[32];
    s.Final(dig);
    static const wchar_t* hexd = L"0123456789abcdef";
    wstring out;
    out.reserve(64);
    for (int i = 0; i < 32; ++i) {
        out.push_back(hexd[dig[i] >> 4]);
        out.push_back(hexd[dig[i] & 15]);
    }
    return out;
}

static wstring LowerAscii(const wstring& s)
{
    wstring o = s;
    for (size_t i = 0; i < o.size(); ++i)
        if (o[i] >= L'A' && o[i] <= L'Z') o[i] = (wchar_t)(o[i] - L'A' + L'a');
    return o;
}

static bool LooksLikeEmail(const wstring& s)
{
    size_t at = s.find(L'@');
    if (at == wstring::npos || at == 0 || at + 1 >= s.size()) return false;
    if (s.find(L'.', at) == wstring::npos) return false;
    if (s.find(L' ') != wstring::npos) return false;
    return true;
}

// ---------------------------------------------------------------- 数据

struct Rec
{
    Task      t;
    long long updated = 0;
    bool      deleted = false;
};

static CRITICAL_SECTION g_cs;
static bool             g_csInit = false;
static map<wstring, Rec> g_shared;      // 网络线程与 UI 线程共享的任务快照
static bool             g_sharedDirty = false;   // 服务端合并后有变化，UI 线程需要并回
static vector<wstring>  g_inbox;        // 拉取到的远端快照，UI 线程合并

static bool    g_loggedIn = false;
static wstring g_email;        // 已登录邮箱
static wstring g_selfHash;     // SHA256(email:password)
static bool    g_syncOn = true;
static wstring g_status;       // 状态文本
static long long g_lastSync = 0;
static int     g_peerCount = 0;

static bool    g_running = false;
static HANDLE  g_srvThread = nullptr;
static HANDLE  g_syncThread = nullptr;
static int     g_tcpPort = 48711;
static int     g_udpPort = 48710;
static wstring g_instId;

// UI 输入状态
static wstring g_emailBuf, g_passBuf;
static int     g_focus = 0;          // 0 无 1 邮箱 2 密码
static bool    g_showPass = false;
static wstring g_err;
static wstring g_info;

static long long NowUnix()
{
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

static void Lock()   { if (g_csInit) EnterCriticalSection(&g_cs); }
static void Unlock() { if (g_csInit) LeaveCriticalSection(&g_cs); }

// --- 账号文件 -----------------------------------------------------------

static wstring AccountsPath() { return PathOf(L"accounts.txt"); }
static wstring SessionPath()  { return PathOf(L"account_session.txt"); }
static wstring MetaPath()     { return PathOf(L"sync_meta.txt"); }

static bool LoadAccounts(map<wstring, wstring>& out)   // email -> hash
{
    out.clear();
    FILE* f = _wfopen(AccountsPath().c_str(), L"r, ccs=UTF-8");
    if (!f) return false;
    wchar_t line[1024];
    while (fgetws(line, 1024, f)) {
        wstring s = line;
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
        if (s.empty() || s[0] == L'#') continue;
        size_t p1 = s.find(L'\t');
        if (p1 == wstring::npos) continue;
        size_t p2 = s.find(L'\t', p1 + 1);
        wstring email = s.substr(0, p1);
        wstring hash = (p2 == wstring::npos) ? s.substr(p1 + 1) : s.substr(p1 + 1, p2 - p1 - 1);
        if (!email.empty() && !hash.empty()) out[email] = hash;
    }
    fclose(f);
    return true;
}

static void SaveAccounts(const map<wstring, wstring>& in)
{
    FILE* f = _wfopen(AccountsPath().c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"# 每日待办 账号文件 —— email \\t hash(SHA256(email:password)) \\t 创建时间\n");
    for (map<wstring, wstring>::const_iterator it = in.begin(); it != in.end(); ++it)
        fwprintf(f, L"%ls\t%ls\t%lld\n", it->first.c_str(), it->second.c_str(), NowUnix());
    fclose(f);
}

static void SaveSession()
{
    FILE* f = _wfopen(SessionPath().c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"%ls\n%ls\n%d\n", g_email.c_str(), g_selfHash.c_str(), g_syncOn ? 1 : 0);
    fclose(f);
}

static void LoadSession()
{
    FILE* f = _wfopen(SessionPath().c_str(), L"r, ccs=UTF-8");
    if (!f) return;
    wchar_t line[512];
    vector<wstring> lines;
    while (fgetws(line, 512, f)) {
        wstring s = line;
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
        lines.push_back(s);
    }
    fclose(f);
    if (lines.size() >= 2 && !lines[0].empty() && !lines[1].empty()) {
        g_email = lines[0];
        g_selfHash = lines[1];
        g_loggedIn = true;
        g_emailBuf = g_email;
    }
    if (lines.size() >= 3) g_syncOn = (lines[2] != L"0");
}

// --- 同步元数据 ---------------------------------------------------------

static void LoadMeta(map<wstring, std::pair<long long, bool> >& out)
{
    out.clear();
    FILE* f = _wfopen(MetaPath().c_str(), L"r, ccs=UTF-8");
    if (!f) return;
    wchar_t line[1024];
    while (fgetws(line, 1024, f)) {
        wstring s = line;
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
        if (s.empty() || s[0] == L'#') continue;
        size_t p1 = s.find(L'\t');
        if (p1 == wstring::npos) continue;
        size_t p2 = s.find(L'\t', p1 + 1);
        if (p2 == wstring::npos) continue;
        wstring id = s.substr(0, p1);
        long long up = _wtoi64(s.substr(p1 + 1, p2 - p1 - 1).c_str());
        bool del = (s.substr(p2 + 1) == L"1");
        if (!id.empty()) out[id] = std::make_pair(up, del);
    }
    fclose(f);
}

static void SaveMeta(const map<wstring, std::pair<long long, bool> >& in)
{
    FILE* f = _wfopen(MetaPath().c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"# 每日待办 同步元数据 —— id \\t 更新时间 \\t 已删除\n");
    for (map<wstring, std::pair<long long, bool> >::const_iterator it = in.begin(); it != in.end(); ++it)
        fwprintf(f, L"%ls\t%lld\t%d\n", it->first.c_str(), it->second.first, it->second.second ? 1 : 0);
    fclose(f);
}

// 用 g_tasks + 元数据重建共享快照（UI 线程调用）
static void PublishShared()
{
    map<wstring, std::pair<long long, bool> > meta;
    LoadMeta(meta);

    Lock();
    g_shared.clear();
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        const Task& t = g_tasks[i];
        Rec r; r.t = t; r.deleted = false;
        if (r.t.code.empty()) r.t.code = ComputeTaskCode(r.t);
        map<wstring, std::pair<long long, bool> >::iterator mi = meta.find(t.id);
        r.updated = (mi != meta.end() && mi->second.first > 0) ? mi->second.first
                                                              : (t.createdAt > 0 ? t.createdAt : NowUnix());
        g_shared[t.id] = r;
    }
    // 墓碑（已删除的任务）也要让同伴知道
    for (map<wstring, std::pair<long long, bool> >::iterator mi = meta.begin(); mi != meta.end(); ++mi) {
        if (!mi->second.second) continue;
        if (g_shared.count(mi->first)) continue;
        Rec r; r.updated = mi->second.first; r.deleted = true;
        r.t.id = mi->first;
        g_shared[mi->first] = r;
    }
    g_sharedDirty = false;
    Unlock();
}

// ---------------------------------------------------------------- 序列化

static wstring Sanitize(const wstring& s)
{
    wstring o = s;
    for (size_t i = 0; i < o.size(); ++i)
        if (o[i] == L'|' || o[i] == L'\t' || o[i] == L'\r' || o[i] == L'\n') o[i] = L' ';
    return o;
}

static wstring TaskLine(const Rec& r)
{
    const Task& t = r.t;
    wchar_t buf[128];
    wstring line;
    line += t.id.empty() ? L"-" : Sanitize(t.id);
    line += L'|';
    line += Sanitize(t.title);
    swprintf(buf, 128, L"|%d|%d|%d|", t.kind, t.hour, t.minute);
    line += buf;
    line += FormatDate(t.startDate);
    line += L'|';
    line += FormatDate(t.endDate);
    swprintf(buf, 128, L"|%d|%d|%d|%d|%lld|%d|", t.hasEnd ? 1 : 0, t.totalCount,
             t.doneCount, t.enabled ? 1 : 0, r.updated, r.deleted ? 1 : 0);
    line += buf;
    line += Sanitize(t.lastFiredKey);
    return line;
}

static wstring BuildPayload(const wstring& email, const wstring& proof)
{
    wstring body = L"SYNC 1\n";
    body += L"EMAIL " + email + L"\n";
    body += L"PROOF " + proof + L"\n";
    Lock();
    wchar_t cnt[64];
    swprintf(cnt, 64, L"COUNT %d\n", (int)g_shared.size());
    body += cnt;
    for (map<wstring, Rec>::iterator it = g_shared.begin(); it != g_shared.end(); ++it) {
        body += L"TASK ";
        body += TaskLine(it->second);
        body += L"\n";
    }
    Unlock();
    body += L"END\n";
    return body;
}

static vector<wstring> Split(const wstring& s, wchar_t sep)
{
    vector<wstring> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        if (p == wstring::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

static bool ParsePayload(const wstring& body, wstring& email, wstring& proof,
                         map<wstring, Rec>& out)
{
    out.clear();
    size_t pos = 0;
    bool sawHeader = false;
    while (pos <= body.size()) {
        size_t nl = body.find(L'\n', pos);
        wstring line = body.substr(pos, (nl == wstring::npos ? body.size() : nl) - pos);
        pos = (nl == wstring::npos) ? body.size() + 1 : nl + 1;
        while (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;
        if (line == L"END") break;
        if (line.compare(0, 5, L"SYNC ") == 0) { sawHeader = true; continue; }
        if (line.compare(0, 6, L"EMAIL ") == 0) { email = line.substr(6); continue; }
        if (line.compare(0, 6, L"PROOF ") == 0) { proof = line.substr(6); continue; }
        if (line.compare(0, 5, L"TASK ") == 0) {
            vector<wstring> f = Split(line.substr(5), L'|');
            if (f.size() < 14) continue;
            Rec r;
            r.t.id = (f[0] == L"-") ? L"" : f[0];
            r.t.title = f[1];
            r.t.kind = _wtoi(f[2].c_str());
            r.t.hour = _wtoi(f[3].c_str());
            r.t.minute = _wtoi(f[4].c_str());
            ParseDate(f[5], r.t.startDate);
            ParseDate(f[6], r.t.endDate);
            r.t.hasEnd = _wtoi(f[7].c_str()) != 0;
            r.t.totalCount = _wtoi(f[8].c_str());
            r.t.doneCount = _wtoi(f[9].c_str());
            r.t.enabled = _wtoi(f[10].c_str()) != 0;
            r.updated = _wtoi64(f[11].c_str());
            r.deleted = _wtoi(f[12].c_str()) != 0;
            r.t.lastFiredKey = f[13];
            r.t.code = ComputeTaskCode(r.t);   // 识别码由内容算出，不走网络传输
            if (!r.t.id.empty()) out[r.t.id] = r;
        }
    }
    return sawHeader;
}

// ---------------------------------------------------------------- HTTP

static bool RecvAll(SOCKET s, string& out, int maxBytes)
{
    char buf[2048];
    while ((int)out.size() < maxBytes) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return !out.empty();
        out.append(buf, n);
        if (out.find("\r\n\r\n") != string::npos) {
            size_t hdrEnd = out.find("\r\n\r\n") + 4;
            size_t cl = 0;
            string lower = out.substr(0, hdrEnd);
            for (size_t i = 0; i < lower.size(); ++i) lower[i] = (char)tolower((unsigned char)lower[i]);
            size_t p = lower.find("content-length:");
            if (p != string::npos) cl = (size_t)atol(out.c_str() + p + 15);
            if (out.size() >= hdrEnd + cl) return true;
        }
    }
    return true;
}

static string HttpBody(const string& raw)
{
    size_t p = raw.find("\r\n\r\n");
    return (p == string::npos) ? string() : raw.substr(p + 4);
}

static void SendAll(SOCKET s, const string& data)
{
    size_t off = 0;
    while (off < data.size()) {
        int n = send(s, data.data() + off, (int)(data.size() - off), 0);
        if (n <= 0) return;
        off += (size_t)n;
    }
}

static void SendResponse(SOCKET s, int code, const string& body)
{
    char hdr[256];
    const char* reason = (code == 200) ? "OK" : (code == 401 ? "Unauthorized" : "Bad Request");
    sprintf(hdr, "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
                 "Content-Length: %d\r\nConnection: close\r\n\r\n", code, reason, (int)body.size());
    string out = hdr;
    out += body;
    SendAll(s, out);
}

// 服务端：合并远端快照并返回合并后的完整快照
static void HandleRequest(SOCKET s)
{
    string raw;
    RecvAll(s, raw, 4 * 1024 * 1024);

    string firstLine = raw.substr(0, raw.find("\r\n"));
    if (firstLine.find("GET /ping") == 0) {
        string b = "DAILYTODO 1\nEMAIL " + ToUtf8(g_email) + "\n";
        SendResponse(s, 200, b);
        return;
    }
    if (firstLine.find("POST /sync") != 0) { SendResponse(s, 400, "bad request\n"); return; }

    wstring body = FromUtf8(HttpBody(raw));
    wstring email, proof;
    map<wstring, Rec> remote;
    if (!ParsePayload(body, email, proof, remote)) { SendResponse(s, 400, "bad payload\n"); return; }

    if (!g_loggedIn || g_selfHash.empty() || proof != g_selfHash || LowerAscii(email) != LowerAscii(g_email)) {
        SendResponse(s, 401, "auth\n");
        return;
    }

    Lock();
    int merged = 0;
    for (map<wstring, Rec>::iterator it = remote.begin(); it != remote.end(); ++it) {
        map<wstring, Rec>::iterator cur = g_shared.find(it->first);
        if (cur == g_shared.end()) {
            // 识别码相同视为同一件待办：快照里已有就不再加一条
            if (!it->second.deleted && !it->second.t.code.empty()) {
                bool dup = false;
                for (map<wstring, Rec>::iterator s = g_shared.begin(); s != g_shared.end(); ++s) {
                    if (!s->second.deleted && s->second.t.code == it->second.t.code) { dup = true; break; }
                }
                if (dup) continue;
            }
            g_shared[it->first] = it->second; ++merged;
        }
        else if (it->second.updated > cur->second.updated) { cur->second = it->second; ++merged; }
    }
    if (merged) g_sharedDirty = true;

    // 回包：合并后的完整快照
    wstring resp = L"SYNC 1\nEMAIL " + g_email + L"\nPROOF " + g_selfHash + L"\n";
    wchar_t cnt[64];
    swprintf(cnt, 64, L"COUNT %d\n", (int)g_shared.size());
    resp += cnt;
    for (map<wstring, Rec>::iterator it = g_shared.begin(); it != g_shared.end(); ++it) {
        resp += L"TASK ";
        resp += TaskLine(it->second);
        resp += L"\n";
    }
    resp += L"END\n";
    Unlock();

    SendResponse(s, 200, ToUtf8(resp));
}

static DWORD WINAPI SrvThread(LPVOID)
{
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return 0;

    BOOL yes = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)g_tcpPort);

    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0) { closesocket(ls); return 0; }
    listen(ls, 8);

    while (g_running) {
        fd_set rs; FD_ZERO(&rs); FD_SET(ls, &rs);
        timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
        int r = select(0, &rs, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        DWORD to = 4000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));
        HandleRequest(c);
        closesocket(c);
    }
    closesocket(ls);
    return 0;
}

// 客户端：向某个 IP 推送快照并取回合并结果
static bool PostSync(const char* ip, wstring& outBody)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    DWORD to = 2500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)g_tcpPort);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) { closesocket(s); return false; }

    string body = ToUtf8(BuildPayload(g_email, g_selfHash));
    char hdr[512];
    sprintf(hdr, "POST /sync HTTP/1.1\r\nHost: %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
                 "Content-Length: %d\r\nConnection: close\r\n\r\n", ip, (int)body.size());
    SendAll(s, string(hdr) + body);

    string raw;
    RecvAll(s, raw, 4 * 1024 * 1024);
    closesocket(s);

    size_t sp = raw.find(' ');
    if (sp != string::npos && raw.compare(sp + 1, 3, "200") != 0) return false;
    string respBody = HttpBody(raw);
    if (respBody.empty()) return false;
    outBody = FromUtf8(respBody);
    return true;
}

// ---------------------------------------------------------------- 发现 + 同步循环

// 按识别码向局域网内的其它设备查询单条待办（只读：只解析回包，不写本地）
bool AccountQueryTaskByCode(const wstring& code, Task& out)
{
    if (!g_loggedIn || g_selfHash.empty() || code.size() < 8) return false;

    SOCKET us = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (us == INVALID_SOCKET) return false;

    BOOL yes = TRUE;
    setsockopt(us, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
    sockaddr_in ua{};
    ua.sin_family = AF_INET;
    ua.sin_addr.s_addr = htonl(INADDR_ANY);
    ua.sin_port = 0;
    bind(us, (sockaddr*)&ua, sizeof(ua));
    u_long nb = 1;
    ioctlsocket(us, FIONBIO, &nb);

    sockaddr_in bc{};
    bc.sin_family = AF_INET;
    bc.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    bc.sin_port = htons((u_short)g_udpPort);
    const char* disc = "DAILYTODO DISCOVER 1";
    sendto(us, disc, (int)strlen(disc), 0, (sockaddr*)&bc, sizeof(bc));

    vector<string> peers;
    char buf[512];
    ULONGLONG until = GetTickCount64() + 800;
    while (GetTickCount64() < until) {
        fd_set rs; FD_ZERO(&rs); FD_SET(us, &rs);
        timeval tv; tv.tv_sec = 0; tv.tv_usec = 150000;
        if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
        sockaddr_in from{};
        int fl = sizeof(from);
        int n = recvfrom(us, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) continue;
        buf[n] = 0;
        char idb[128] = { 0 };
        int port = 0, logged = 0;
        if (sscanf(buf, "DAILYTODO HERE %127s %d %d", idb, &port, &logged) == 3) {
            if (!logged) continue;
            if (g_instId == FromUtf8(idb)) continue;   // 自己
            char ip[64];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            peers.push_back(string(ip));
        }
    }
    closesocket(us);

    for (size_t i = 0; i < peers.size(); ++i) {
        wstring resp;
        if (!PostSync(peers[i].c_str(), resp)) continue;

        wstring email, proof;
        map<wstring, Rec> remote;
        if (!ParsePayload(resp, email, proof, remote)) continue;
        if (!g_selfHash.empty() && proof != g_selfHash) continue;

        for (map<wstring, Rec>::iterator it = remote.begin(); it != remote.end(); ++it) {
            if (it->second.deleted) continue;
            Task t = it->second.t;
            wstring c = ComputeTaskCode(t);
            wstring c8 = c.size() > 8 ? c.substr(0, 8) : c;
            if (c == code || c.find(code) == 0 || c8.find(code) == 0) {
                t.code = c;
                out = t;
                return true;
            }
        }
    }
    return false;
}

static DWORD WINAPI SyncThread(LPVOID)
{
    BOOL yes = TRUE;

    // 监听 socket：固定端口，接收别的实例的 DISCOVER 并回包
    SOCKET ls = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ls == INVALID_SOCKET) return 0;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in la{};
    la.sin_family = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    la.sin_port = htons((u_short)g_udpPort);
    if (bind(ls, (sockaddr*)&la, sizeof(la)) != 0) { closesocket(ls); ls = INVALID_SOCKET; }
    {
        u_long nb0 = 1;
        ioctlsocket(ls, FIONBIO, &nb0);
    }

    // 发送 socket：临时端口，广播 DISCOVER 并接收 HERE 回包
    // （同一台机器上开两个实例时，回包各归各的 socket，不会互相抢走）
    SOCKET us = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (us == INVALID_SOCKET) { if (ls != INVALID_SOCKET) closesocket(ls); return 0; }
    setsockopt(us, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
    sockaddr_in ua{};
    ua.sin_family = AF_INET;
    ua.sin_addr.s_addr = htonl(INADDR_ANY);
    ua.sin_port = 0;
    bind(us, (sockaddr*)&ua, sizeof(ua));

    u_long nb = 1;
    ioctlsocket(us, FIONBIO, &nb);

    ULONGLONG lastRound = 0;
    char buf[512];
    while (g_running) {
        // 1) 处理别人的 DISCOVER 并回包（从监听 socket 回，源端口固定）
        if (ls != INVALID_SOCKET) {
            for (int i2 = 0; i2 < 16; ++i2) {
                sockaddr_in from{};
                int fl = sizeof(from);
                int n = recvfrom(ls, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
                if (n <= 0) break;
                buf[n] = 0;
                if (strncmp(buf, "DAILYTODO DISCOVER", 18) == 0) {
                    char rep[512];
                    _snprintf(rep, sizeof(rep), "DAILYTODO HERE %s %d %d", ToUtf8(g_instId).c_str(),
                              g_tcpPort, g_loggedIn ? 1 : 0);
                    rep[sizeof(rep) - 1] = 0;
                    sendto(ls, rep, (int)strlen(rep), 0, (sockaddr*)&from, fl);
                }
            }
        }

        ULONGLONG now = GetTickCount64();
        if (g_loggedIn && g_syncOn && now - lastRound > 12000) {
            lastRound = now;

            sockaddr_in bc{};
            bc.sin_family = AF_INET;
            bc.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            bc.sin_port = htons((u_short)g_udpPort);
            const char* disc = "DAILYTODO DISCOVER 1";
            sendto(us, disc, (int)strlen(disc), 0, (sockaddr*)&bc, sizeof(bc));

            vector<string> peers;
            ULONGLONG until = now + 700;
            while (GetTickCount64() < until) {
                fd_set rs; FD_ZERO(&rs); FD_SET(us, &rs);
                timeval tv; tv.tv_sec = 0; tv.tv_usec = 120000;
                if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
                sockaddr_in from{};
                int fl = sizeof(from);
                int n = recvfrom(us, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
                if (n <= 0) continue;
                buf[n] = 0;
                char idb[128] = { 0 };
                int port = 0, logged = 0;
                if (sscanf(buf, "DAILYTODO HERE %127s %d %d", idb, &port, &logged) == 3) {
                    if (!logged) continue;
                    if (g_instId == FromUtf8(idb)) continue;   // 自己
                    char ip[64];
                    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                    peers.push_back(string(ip));
                }
            }

            // 3) 逐个交换快照
            for (size_t i2 = 0; i2 < peers.size(); ++i2) {
                wstring resp;
                if (!PostSync(peers[i2].c_str(), resp)) continue;
                Lock();
                g_inbox.push_back(resp);
                Unlock();
                g_peerCount = (int)peers.size();
                g_lastSync = NowUnix();
            }
            if (peers.empty()) g_peerCount = 0;
        }
        Sleep(400);
    }
    closesocket(us);
    if (ls != INVALID_SOCKET) closesocket(ls);
    return 0;
}

// ---------------------------------------------------------------- 合并（UI 线程）

static bool MergeRemoteIntoLocal(const wstring& body)
{
    wstring email, proof;
    map<wstring, Rec> remote;
    if (!ParsePayload(body, email, proof, remote)) return false;
    if (!g_selfHash.empty() && proof != g_selfHash) return false;

    map<wstring, std::pair<long long, bool> > meta;
    LoadMeta(meta);

    bool changed = false;
    long long now = NowUnix();

    for (map<wstring, Rec>::iterator it = remote.begin(); it != remote.end(); ++it) {
        const wstring& id = it->first;
        Rec rr = it->second;

        int localIdx = -1;
        for (size_t i = 0; i < g_tasks.size(); ++i)
            if (g_tasks[i].id == id) { localIdx = (int)i; break; }

        long long localUp = 0;
        map<wstring, std::pair<long long, bool> >::iterator mi = meta.find(id);
        if (mi != meta.end()) localUp = mi->second.first;
        else if (localIdx >= 0) localUp = g_tasks[localIdx].createdAt;

        if (rr.updated <= localUp) continue;   // 本地更新，保留本地

        if (rr.deleted) {
            if (localIdx >= 0) { g_tasks.erase(g_tasks.begin() + localIdx); changed = true; }
            meta[id] = std::make_pair(rr.updated, true);
        } else {
            Task t = rr.t;
            if (t.createdAt <= 0) t.createdAt = rr.updated;
            t.code = ComputeTaskCode(t);
            // 识别码相同 = 同一件待办（在另一端独立创建过），跳过，不产生重复
            if (localIdx < 0) {
                bool dup = false;
                for (size_t i = 0; i < g_tasks.size(); ++i) {
                    if (!g_tasks[i].code.empty() && g_tasks[i].code == t.code) { dup = true; break; }
                }
                if (dup) continue;
            }
            if (localIdx >= 0) g_tasks[localIdx] = t; else g_tasks.push_back(t);
            meta[id] = std::make_pair(rr.updated, false);
            changed = true;
        }
    }

    // 本地任务若还没有元数据，补上（用创建时间）
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        const wstring& id = g_tasks[i].id;
        if (meta.count(id)) continue;
        meta[id] = std::make_pair(g_tasks[i].createdAt > 0 ? g_tasks[i].createdAt : now, false);
    }

    if (changed) {
        SaveTasks(g_tasks);
        SaveMeta(meta);
    }
    return changed;
}

void AccountMarkDeleted(const wstring& id)
{
    if (id.empty()) return;
    map<wstring, std::pair<long long, bool> > meta;
    LoadMeta(meta);
    long long now = NowUnix();
    map<wstring, std::pair<long long, bool> >::iterator mi = meta.find(id);
    if (mi != meta.end() && mi->second.first > now) now = mi->second.first;
    meta[id] = std::make_pair(now, true);
    SaveMeta(meta);

    // 同时更新共享快照，让本机其它连接/下一次推送立刻带上墓碑
    Rec r;
    r.updated = now;
    r.deleted = true;
    Lock();
    g_shared[id] = r;
    Unlock();
    PublishShared();
}

// 把共享快照里"更新"的变化并回本地（服务端合并过别人的数据）
static bool MergeSharedIntoLocal(){
    map<wstring, Rec> snap;
    Lock();
    snap = g_shared;
    g_sharedDirty = false;
    Unlock();

    map<wstring, std::pair<long long, bool> > meta;
    LoadMeta(meta);
    bool changed = false;

    for (map<wstring, Rec>::iterator it = snap.begin(); it != snap.end(); ++it) {
        const wstring& id = it->first;
        const Rec& rr = it->second;
        int localIdx = -1;
        for (size_t i = 0; i < g_tasks.size(); ++i)
            if (g_tasks[i].id == id) { localIdx = (int)i; break; }

        long long localUp = 0;
        map<wstring, std::pair<long long, bool> >::iterator mi = meta.find(id);
        if (mi != meta.end()) localUp = mi->second.first;
        else if (localIdx >= 0) localUp = g_tasks[localIdx].createdAt;

        if (rr.updated <= localUp) continue;

        if (rr.deleted) {
            if (localIdx >= 0) { g_tasks.erase(g_tasks.begin() + localIdx); changed = true; }
            meta[id] = std::make_pair(rr.updated, true);
        } else {
            Task t = rr.t;
            if (t.createdAt <= 0) t.createdAt = rr.updated;
            if (localIdx >= 0) g_tasks[localIdx] = t; else g_tasks.push_back(t);
            meta[id] = std::make_pair(rr.updated, false);
            changed = true;
        }
    }
    if (changed) {
        SaveTasks(g_tasks);
        SaveMeta(meta);
    }
    return changed;
}

// ---------------------------------------------------------------- 账号动作

bool AccountLoggedIn()   { return g_loggedIn; }
wstring AccountEmail()   { return g_email; }
bool AccountSyncEnabled(){ return g_syncOn; }
wstring AccountStatusText(){ return g_status; }
int  AccountPeerCount()  { return g_peerCount; }

static void RefreshStatus()
{
    if (!g_loggedIn) { g_status = L"未登录"; return; }
    if (!g_syncOn)   { g_status = L"已暂停同步"; return; }
    if (g_lastSync == 0) { g_status = g_peerCount ? L"已发现同伴，正在同步" : L"等待同一局域网内的其它设备"; return; }
    wstring s = L"已同步";
    if (g_peerCount > 0) { wchar_t b[64]; swprintf(b, 64, L" · %d 台设备", g_peerCount); s += b; }
    time_t tt = (time_t)g_lastSync;
    struct tm* tmv = localtime(&tt);
    wchar_t tb[64];
    if (tmv) swprintf(tb, 64, L" · %02d:%02d", tmv->tm_hour, tmv->tm_min);
    else wcscpy(tb, L"");
    s += tb;
    g_status = s;
}

static bool DoRegister(const wstring& email, const wstring& pass, wstring& err)
{
    wstring e = LowerAscii(email);
    if (!LooksLikeEmail(e)) { err = L"邮箱格式不正确"; return false; }
    if (pass.size() < 6)    { err = L"密码至少 6 位"; return false; }

    map<wstring, wstring> acc;
    LoadAccounts(acc);
    if (acc.count(e)) { err = L"该邮箱已注册，请直接登录"; return false; }

    wstring hash = Sha256Hex(e + L":" + pass);
    acc[e] = hash;
    SaveAccounts(acc);

    g_email = e; g_selfHash = hash; g_loggedIn = true; g_err.clear();
    g_info = L"注册成功，已自动登录";
    SaveSession();
    PublishShared();
    RefreshStatus();
    return true;
}

static bool DoLogin(const wstring& email, const wstring& pass, wstring& err)
{
    wstring e = LowerAscii(email);
    map<wstring, wstring> acc;
    LoadAccounts(acc);
    map<wstring, wstring>::iterator it = acc.find(e);
    if (it == acc.end()) { err = L"该邮箱尚未在本机注册"; return false; }
    wstring hash = Sha256Hex(e + L":" + pass);
    if (hash != it->second) { err = L"密码不正确"; return false; }

    g_email = e; g_selfHash = hash; g_loggedIn = true; g_err.clear();
    g_info = L"登录成功";
    SaveSession();
    PublishShared();
    RefreshStatus();
    return true;
}

static void DoLogout()
{
    g_loggedIn = false;
    g_email.clear();
    g_selfHash.clear();
    g_lastSync = 0;
    g_peerCount = 0;
    g_info = L"已退出登录";
    g_err.clear();
    DeleteFileW(SessionPath().c_str());
    PublishShared();
    RefreshStatus();
}

// ---------------------------------------------------------------- 生命周期

void AccountInit(HWND /*mainWnd*/)
{
    if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    g_instId.clear();
    wchar_t idb[32];
    swprintf(idb, 32, L"%08x%08x", (unsigned)GetTickCount(), (unsigned)(ULONG_PTR)&g_cs);
    g_instId = idb;

    LoadSession();
    PublishShared();
    RefreshStatus();

    g_running = true;
    g_srvThread = CreateThread(nullptr, 0, SrvThread, nullptr, 0, nullptr);
    g_syncThread = CreateThread(nullptr, 0, SyncThread, nullptr, 0, nullptr);

    g_emailBuf = g_email;
    g_status = g_loggedIn ? L"等待同一局域网内的其它设备" : L"未登录";
}

void AccountShutdown()
{
    g_running = false;
    if (g_srvThread) { WaitForSingleObject(g_srvThread, 2000); CloseHandle(g_srvThread); g_srvThread = nullptr; }
    if (g_syncThread) { WaitForSingleObject(g_syncThread, 2000); CloseHandle(g_syncThread); g_syncThread = nullptr; }
    WSACleanup();
    if (g_csInit) { DeleteCriticalSection(&g_cs); g_csInit = false; }
}

void AccountTick(HWND mainWnd)
{
    // 收集远端快照并合并
    vector<wstring> batch;
    bool serverMerged = false;
    Lock();
    batch.swap(g_inbox);
    serverMerged = g_sharedDirty;
    Unlock();

    bool changed = false;
    for (size_t i = 0; i < batch.size(); ++i)
        if (MergeRemoteIntoLocal(batch[i])) changed = true;
    if (serverMerged && MergeSharedIntoLocal()) changed = true;

    static int s_pubTick = 0;
    if (changed) {
        PublishShared();
        InvalidateLayout();
        if (mainWnd) InvalidateRect(mainWnd, nullptr, FALSE);
    }
    // 每 5 秒把本地最新数据发布到共享快照，供同伴拉取
    if (++s_pubTick >= 5) { s_pubTick = 0; PublishShared(); }

    wstring old = g_status;
    RefreshStatus();
    if (old != g_status && g_page == 3) InvalidateLayout();
}

// ---------------------------------------------------------------- 输入

bool AccountOnChar(wchar_t ch)
{
    if (g_page != 3 || !g_loggedIn == false) { /* 登录页才需要输入 */ }
    if (g_focus == 0) return false;

    wstring* buf = (g_focus == 1) ? &g_emailBuf : &g_passBuf;
    if (ch == L'\b') {
        if (!buf->empty()) buf->erase(buf->size() - 1);
        return true;
    }
    if (ch == L'\r' || ch == L'\n') {
        if (g_focus == 1) g_focus = 2; else if (g_focus == 2) AccountDispatch(H_ACC_LOGIN);
        return true;
    }
    if (ch == L'\t') { g_focus = (g_focus == 1) ? 2 : 1; return true; }
    if (ch < 32) return false;
    if (buf->size() > 120) return true;
    buf->push_back(ch);
    g_err.clear();
    return true;
}

bool AccountOnKey(int vk)
{
    if (g_page != 3) return false;
    if (vk == VK_TAB) { g_focus = (g_focus == 1) ? 2 : (g_focus == 2 ? 1 : 1); return true; }
    if (vk == VK_ESCAPE) { g_focus = 0; return true; }
    return false;
}

// ---------------------------------------------------------------- 布局

static RECT g_rcCard{}, g_rcEmailField{}, g_rcPassField{};
static RECT g_rcBtnPrimary{}, g_rcBtnSecond{}, g_rcBtnThird{};
static RECT g_rcSyncRow{};
static RECT g_rcShowPass{};

void AccountLayout(int w, int h)
{
    const int TITLEH = S(52);
    const int NAVW = S(216);
    const int PAD = S(32);

    g_rcCard = RECT{ 0, 0, 0, 0 };
    g_rcEmailField = RECT{ 0, 0, 0, 0 };
    g_rcPassField = RECT{ 0, 0, 0, 0 };
    g_rcBtnPrimary = RECT{ 0, 0, 0, 0 };
    g_rcBtnSecond = RECT{ 0, 0, 0, 0 };
    g_rcBtnThird = RECT{ 0, 0, 0, 0 };
    g_rcSyncRow = RECT{ 0, 0, 0, 0 };
    g_rcShowPass = RECT{ 0, 0, 0, 0 };

    int x = NAVW + PAD;
    int cw = w - x - S(32);
    if (cw > S(760)) cw = S(760);
    int y = TITLEH + S(96);

    if (!g_loggedIn) {
        g_rcCard = { x, y, x + cw, y + S(322) };
        int fx = x + S(28), fw = cw - S(56);
        g_rcEmailField = { fx, y + S(46), fx + fw, y + S(46) + S(46) };
        g_rcPassField  = { fx, y + S(134), fx + fw, y + S(134) + S(46) };
        g_rcShowPass   = { g_rcPassField.right - S(48), g_rcPassField.top + S(13),
                           g_rcPassField.right - S(12), g_rcPassField.bottom - S(13) };

        int bw = S(150), bh = S(44), by = y + S(206);
        g_rcBtnPrimary = { fx, by, fx + bw, by + bh };
        g_rcBtnSecond  = { fx + bw + S(12), by, fx + bw + S(12) + bw, by + bh };

        Hot(H_ACC_FIELD_EMAIL, g_rcEmailField);
        Hot(H_ACC_FIELD_PASS, g_rcPassField);
        Hot(H_ACC_SHOWPASS, g_rcShowPass);
        Hot(H_ACC_REGISTER, g_rcBtnPrimary);
        Hot(H_ACC_LOGIN, g_rcBtnSecond);
    } else {
        g_rcCard = { x, y, x + cw, y + S(310) };
        int fx = x + S(28), fw = cw - S(56);
        g_rcSyncRow = { fx, y + S(120), fx + fw, y + S(120) + S(56) };

        int bw = S(150), bh = S(44), by = y + S(212);
        g_rcBtnPrimary = { fx, by, fx + bw, by + bh };
        g_rcBtnSecond  = { fx + bw + S(12), by, fx + bw + S(12) + bw, by + bh };

        Hot(H_ACC_SYNCTOGGLE, g_rcSyncRow);
        Hot(H_ACC_SYNCNOW, g_rcBtnPrimary);
        Hot(H_ACC_LOGOUT, g_rcBtnSecond);
    }
}

// ---------------------------------------------------------------- 绘制

static void FieldBox(Graphics& g, const RECT& r, bool focused, const wstring& text,
                     const wstring& cue, bool masked, bool caret)
{
    FillRound(g, r, S(8), TH.control);
    StrokeRound(g, r, S(8), focused ? TH.accent : TH.border, focused ? 1.6f : 1.0f);

    wstring shown;
    if (text.empty()) shown = cue;
    else if (masked) shown = wstring(text.size(), L'●');
    else shown = text;

    RECT tx = { r.left + S(14), r.top, r.right - S(14), r.bottom };
    DrawTextC(g, shown, tx, FK_BODY, text.empty() ? TH.text3 : TH.text);

    if (focused && caret && (GetTickCount64() / 500) % 2 == 0) {
        int tw = text.empty() ? 0 : MeasureTextW(g, masked ? wstring(text.size(), L'●') : text, FK_BODY);
        RECT cr = { r.left + S(14) + tw + S(1), r.top + S(12), r.left + S(14) + tw + S(2), r.bottom - S(12) };
        FillRectC(g, cr, TH.accent);
    }
}

static void PrimaryBtn(Graphics& g, const RECT& r, const wstring& label)
{
    FillRound(g, r, S(8), TH.accent);
    DrawTextC(g, label, r, FK_BODY, TH.onAccent, ALIGN_CENTER);
}

static void GhostBtn(Graphics& g, const RECT& r, const wstring& label)
{
    FillRound(g, r, S(8), TH.control);
    StrokeRound(g, r, S(8), TH.border, 1.0f);
    DrawTextC(g, label, r, FK_BODY, TH.text, ALIGN_CENTER);
}

void DrawAccountPage(Graphics& g, int w, int h)
{
    const int TITLEH = S(52);
    const int NAVW = S(216);
    int x = NAVW + S(32);

    RECT r1 = { x, TITLEH + S(26), w - S(32), TITLEH + S(26) + S(38) };
    DrawTextC(g, L"账号", r1, FK_H1, TH.text);

    RECT r2 = { x, TITLEH + S(62), w - S(32), TITLEH + S(62) + S(22) };
    DrawTextC(g, L"同一邮箱登录的设备，在同一局域网内自动同步待办", r2, FK_SUB, TH.text2);

    FillRound(g, g_rcCard, S(10), TH.card);
    StrokeRound(g, g_rcCard, S(10), TH.border, 1.0f);

    int cx = g_rcCard.left + S(28);

    if (!g_loggedIn) {
        RECT l1 = { cx, g_rcCard.top + S(22), g_rcCard.right - S(28), g_rcCard.top + S(42) };
        DrawTextC(g, L"邮箱", l1, FK_SMALL, TH.text2);
        FieldBox(g, g_rcEmailField, g_focus == 1, g_emailBuf, L"you@example.com", false, true);

        RECT l2 = { cx, g_rcCard.top + S(110), g_rcCard.right - S(28), g_rcCard.top + S(130) };
        DrawTextC(g, L"密码", l2, FK_SMALL, TH.text2);
        FieldBox(g, g_rcPassField, g_focus == 2, g_passBuf, L"至少 6 位", !g_showPass, true);

        DrawTextC(g, g_showPass ? L"隐藏" : L"显示", g_rcShowPass, FK_SMALL, TH.accent, ALIGN_RIGHT);

        PrimaryBtn(g, g_rcBtnPrimary, L"注册并登录");
        GhostBtn(g, g_rcBtnSecond, L"登录");

        RECT h1 = { cx, g_rcCard.bottom - S(40), g_rcCard.right - S(28), g_rcCard.bottom - S(18) };
        DrawTextC(g, L"账号保存在本机，密码只以 SHA-256 摘要形式保存，不会联网上传",
                  h1, FK_SMALL, TH.text3);
    } else {
        RECT e1 = { cx, g_rcCard.top + S(26), g_rcCard.right - S(28), g_rcCard.top + S(50) };
        DrawTextC(g, L"当前账号", e1, FK_SMALL, TH.text2);
        RECT e2 = { cx, g_rcCard.top + S(52), g_rcCard.right - S(28), g_rcCard.top + S(82) };
        DrawTextC(g, g_email, e2, FK_H2, TH.text);

        RECT s1 = { cx, g_rcCard.top + S(90), g_rcCard.right - S(28), g_rcCard.top + S(112) };
        DrawTextC(g, g_status, s1, FK_SUB, TH.text2);

        // 同步开关行
        RECT rowBg = g_rcSyncRow;
        FillRound(g, rowBg, S(8), TH.control);
        RECT rt = { rowBg.left + S(14), rowBg.top, rowBg.right - S(90), rowBg.bottom };
        DrawTextC(g, L"局域网同步", rt, FK_BODY, TH.text);
        RECT sw = { rowBg.right - S(76), rowBg.top + S(13), rowBg.right - S(14), rowBg.bottom - S(13) };
        FillRound(g, sw, (sw.bottom - sw.top) / 2, g_syncOn ? TH.accent : TH.border);
        int kr = (sw.bottom - sw.top) / 2 - S(3);
        int kc = g_syncOn ? (sw.right - kr - S(3)) : (sw.left + kr + S(3));
        int kcy = (sw.top + sw.bottom) / 2;
        FillRound(g, RECT{ kc - kr, kcy - kr, kc + kr, kcy + kr }, kr, TH.card);

        PrimaryBtn(g, g_rcBtnPrimary, L"立即同步");
        GhostBtn(g, g_rcBtnSecond, L"退出登录");

        RECT h2 = { cx, g_rcCard.bottom - S(44), g_rcCard.right - S(28), g_rcCard.bottom - S(20) };
        DrawTextC(g, L"局域网内使用同一邮箱登录的设备会自动交换任务（较新的改动生效）",
                  h2, FK_SMALL, TH.text3);
    }

    if (!g_err.empty()) {
        RECT er = { cx, g_rcCard.bottom + S(14), g_rcCard.right - S(28), g_rcCard.bottom + S(40) };
        DrawTextC(g, g_err, er, FK_BODY, TH.danger);
    } else if (!g_info.empty()) {
        RECT ir = { cx, g_rcCard.bottom + S(14), g_rcCard.right - S(28), g_rcCard.bottom + S(40) };
        DrawTextC(g, g_info, ir, FK_BODY, TH.success);
    }
}

// ---------------------------------------------------------------- 交互

void AccountDispatch(int id)
{
    switch (id) {
    case H_ACC_FIELD_EMAIL: g_focus = 1; g_err.clear(); break;
    case H_ACC_FIELD_PASS:  g_focus = 2; g_err.clear(); break;
    case H_ACC_SHOWPASS:    g_showPass = !g_showPass; break;
    case H_ACC_REGISTER: {
        wstring err;
        if (DoRegister(g_emailBuf, g_passBuf, err)) { g_passBuf.clear(); g_focus = 0; }
        else { g_err = err; g_info.clear(); }
        break;
    }
    case H_ACC_LOGIN: {
        wstring err;
        if (DoLogin(g_emailBuf, g_passBuf, err)) { g_passBuf.clear(); g_focus = 0; }
        else { g_err = err; g_info.clear(); }
        break;
    }
    case H_ACC_LOGOUT:   DoLogout(); g_focus = 0; break;
    case H_ACC_SYNCTOGGLE:
        g_syncOn = !g_syncOn;
        SaveSession();
        RefreshStatus();
        g_info = g_syncOn ? L"已开启局域网同步" : L"已暂停局域网同步";
        break;
    case H_ACC_SYNCNOW: {
        // 让同步线程立刻跑一轮
        g_info = L"正在同步…";
        g_lastSync = 0;
        AccountTick(g_hMain);
        break;
    }
    default: return;
    }
    InvalidateLayout();
    Repaint();
}
