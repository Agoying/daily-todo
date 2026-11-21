#include "app.h"
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------- globals

vector<Task> g_tasks;
int  g_page = 0;
int  g_view = 0;
int  g_editIndex = -1;
EditForm g_form;
HWND g_hMain = nullptr;
bool g_autostart = false;
bool g_closeToTray = true;
bool g_soundOn = true;
bool g_paused = false;
bool g_blockInject = false;
wstring   g_bannerTitle;
wstring   g_bannerBody;
ULONGLONG g_bannerUntil = 0;

Theme TH;
int   g_dpi = 96;
float g_scale = 1.0f;

int S(int v) { return (int)(v * g_scale + 0.5f); }

// ---------------------------------------------------------------- string / time utils

static wstring Trim(const wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static wstring Pad2(int v)
{
    wchar_t b[8];
    _snwprintf(b, 8, L"%02d", v);
    return b;
}

wstring FormatTime(int h, int m) { return Pad2(h) + L":" + Pad2(m); }

wstring FormatDate(const SYSTEMTIME& st)
{
    wchar_t b[32];
    _snwprintf(b, 32, L"%04d-%02d-%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay);
    return b;
}

wstring FormatDateCN(const SYSTEMTIME& st)
{
    wchar_t b[48];
    _snwprintf(b, 48, L"%d-%02d-%02d", (int)st.wYear, (int)st.wMonth, (int)st.wDay);
    return b;
}

wstring WeekdayCN(const SYSTEMTIME& st)
{
    static const wchar_t* wd[] = { L"星期日", L"星期一", L"星期二", L"星期三",
                                   L"星期四", L"星期五", L"星期六" };
    SYSTEMTIME c = st;
    c.wHour = 12; c.wMinute = 0; c.wSecond = 0; c.wMilliseconds = 0;
    FILETIME ft; SystemTimeToFileTime(&c, &ft);
    WORD idx = c.wDayOfWeek;
    if (idx > 6) idx = 0;
    return wd[idx];
}

static long long FTtoLL(const FILETIME& ft)
{
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (long long)u.QuadPart;
}

SYSTEMTIME Today()
{
    SYSTEMTIME st; GetLocalTime(&st);
    st.wHour = 0; st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 0;
    return st;
}

SYSTEMTIME AddDays(const SYSTEMTIME& st, int days)
{
    SYSTEMTIME zero = st;
    zero.wHour = 0; zero.wMinute = 0; zero.wSecond = 0; zero.wMilliseconds = 0;
    FILETIME ft; SystemTimeToFileTime(&zero, &ft);
    long long t = FTtoLL(ft) + (long long)days * 864000000000LL;
    ULARGE_INTEGER u; u.QuadPart = (unsigned long long)t;
    FILETIME f2; f2.dwLowDateTime = u.LowPart; f2.dwHighDateTime = u.HighPart;
    SYSTEMTIME out; FileTimeToSystemTime(&f2, &out);
    out.wHour = st.wHour; out.wMinute = st.wMinute;
    out.wSecond = st.wSecond; out.wMilliseconds = st.wMilliseconds;
    return out;
}

int DaysBetween(const SYSTEMTIME& a, const SYSTEMTIME& b)
{
    SYSTEMTIME x = a, y = b;
    x.wHour = 0; x.wMinute = 0; x.wSecond = 0; x.wMilliseconds = 0;
    y.wHour = 0; y.wMinute = 0; y.wSecond = 0; y.wMilliseconds = 0;
    FILETIME fa, fb;
    SystemTimeToFileTime(&x, &fa);
    SystemTimeToFileTime(&y, &fb);
    return (int)((FTtoLL(fb) - FTtoLL(fa)) / 864000000000LL);
}

bool SameDate(const SYSTEMTIME& a, const SYSTEMTIME& b)
{
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}

bool DateInRange(const SYSTEMTIME& d, const SYSTEMTIME& lo, const SYSTEMTIME& hi)
{
    return DaysBetween(lo, d) >= 0 && DaysBetween(d, hi) >= 0;
}

static bool IsAfter(const SYSTEMTIME& a, const SYSTEMTIME& b) // a > b ?
{
    FILETIME fa, fb;
    SystemTimeToFileTime(&a, &fa);
    SystemTimeToFileTime(&b, &fb);
    return FTtoLL(fa) > FTtoLL(fb);
}

static long long SecondsBetween(const SYSTEMTIME& a, const SYSTEMTIME& b) // b - a
{
    FILETIME fa, fb;
    SystemTimeToFileTime(&a, &fa);
    SystemTimeToFileTime(&b, &fb);
    return (FTtoLL(fb) - FTtoLL(fa)) / 10000000LL;
}

bool ParseDate(const wstring& s, SYSTEMTIME& out)
{
    int y = 0, mo = 0, d = 0;
    if (swscanf(s.c_str(), L"%d-%d-%d", &y, &mo, &d) != 3) return false;
    if (y < 1900 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    ZeroMemory(&out, sizeof(out));
    out.wYear = (WORD)y; out.wMonth = (WORD)mo; out.wDay = (WORD)d;
    FILETIME ft;
    if (!SystemTimeToFileTime(&out, &ft)) return false;
    return true;
}

wstring PrettyLeft(long long sec)
{
    if (sec < 0) sec = 0;
    if (sec < 60) return L"即将提醒";
    long long m = sec / 60;
    if (m < 60) return std::to_wstring(m) + L" 分钟后";
    long long h = m / 60, rm = m % 60;
    if (h < 24) {
        wstring s = std::to_wstring(h) + L" 小时";
        if (rm) s += L" " + std::to_wstring(rm) + L" 分";
        return s + L"后";
    }
    long long d = h / 24, rh = h % 24;
    wstring s = std::to_wstring(d) + L" 天";
    if (rh) s += L" " + std::to_wstring(rh) + L" 小时";
    return s + L"后";
}

wstring TodayKey(const SYSTEMTIME& st, int h, int m)
{
    wstring k = FormatDate(st) + L" " + FormatTime(h, m);
    return k;
}

// ---------------------------------------------------------------- scheduling

NextOccurrence ComputeNext(const Task& t, const SYSTEMTIME& now)
{
    NextOccurrence r;
    if (!t.enabled) { r.finished = true; r.finishedText = L"已暂停"; return r; }

    if (t.kind == KIND_TEMPORARY && t.doneCount >= t.totalCount) {
        r.finished = true; r.finishedText = L"已完成";
        return r;
    }
    if (t.kind == KIND_PERMANENT && t.hasEnd && DaysBetween(t.endDate, now) > 0) {
        r.finished = true; r.finishedText = L"已结束";
        return r;
    }

    SYSTEMTIME day = now;
    day.wHour = 0; day.wMinute = 0; day.wSecond = 0; day.wMilliseconds = 0;

    if (t.kind == KIND_PERMANENT && DaysBetween(day, t.startDate) > 0)
        day = t.startDate;

    SYSTEMTIME cand = day;
    cand.wHour = (WORD)t.hour;
    cand.wMinute = (WORD)t.minute;
    cand.wSecond = 0;
    cand.wMilliseconds = 0;

    if (!IsAfter(cand, now)) {
        cand = AddDays(cand, 1);
        cand.wHour = (WORD)t.hour;
        cand.wMinute = (WORD)t.minute;
        cand.wSecond = 0;
        cand.wMilliseconds = 0;
    }

    if (t.kind == KIND_PERMANENT && t.hasEnd && DaysBetween(t.endDate, cand) > 0) {
        r.finished = true; r.finishedText = L"已结束";
        return r;
    }

    r.valid = true;
    r.when = cand;
    r.secondsLeft = SecondsBetween(now, cand);
    if (r.secondsLeft < 0) r.secondsLeft = 0;
    return r;
}

void SchedulerTick(HWND wnd, const SYSTEMTIME& now)
{
    (void)wnd;
    if (g_paused) return;

    SYSTEMTIME today = now;
    today.wHour = 0; today.wMinute = 0; today.wSecond = 0; today.wMilliseconds = 0;

    bool changed = false;
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        Task& t = g_tasks[i];
        if (!t.enabled) continue;
        if (t.kind == KIND_TEMPORARY && t.doneCount >= t.totalCount) continue;
        if (t.kind == KIND_PERMANENT) {
            if (DaysBetween(today, t.startDate) > 0) continue;              // 尚未开始
            if (t.hasEnd && DaysBetween(t.endDate, today) > 0) continue;   // 已结束
        }

        SYSTEMTIME sched = today;
        sched.wHour = (WORD)t.hour;
        sched.wMinute = (WORD)t.minute;
        sched.wSecond = 0;
        sched.wMilliseconds = 0;

        wstring key = TodayKey(today, t.hour, t.minute);
        if (t.lastFiredKey == key) continue;
        if (IsAfter(sched, now)) continue;      // 还没到点

        long long late = SecondsBetween(sched, now);
        if (late > 12LL * 3600LL) { t.lastFiredKey = key; changed = true; continue; }

        wstring body;
        if (t.kind == KIND_TEMPORARY) {
            body = L"临时待办 · 第 " + std::to_wstring(t.doneCount + 1) + L" / "
                 + std::to_wstring(t.totalCount) + L" 次 · " + FormatTime(t.hour, t.minute);
        } else {
            body = L"常驻待办 · " + FormatTime(t.hour, t.minute);
        }
        if (late > 180) body += L"（补提醒）";

        ShowToast(t.title, body, !g_soundOn);
        t.lastFiredKey = key;
        if (t.kind == KIND_TEMPORARY && t.doneCount < t.totalCount) t.doneCount++;
        changed = true;
    }
    if (changed) SaveTasks(g_tasks);
}

// ---------------------------------------------------------------- persistence

static wstring ExePath()
{
    wchar_t buf[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return buf;
}

wstring DataFilePath()
{
    wstring p = ExePath();
    size_t pos = p.find_last_of(L"\\/");
    wstring dir = (pos == wstring::npos) ? L"." : p.substr(0, pos);
    return dir + L"\\tasks_data.txt";
}

static wstring Utf8ToW(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

static std::string WToUtf8(const wstring& s)
{
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

static wstring EscapeField(const wstring& s)
{
    wstring o;
    for (wchar_t c : s) {
        if (c == L'\\') o += L"\\\\";
        else if (c == L'\t') o += L"\\t";
        else if (c == L'\r') o += L"\\r";
        else if (c == L'\n') o += L"\\n";
        else o += c;
    }
    return o;
}

static wstring UnescapeField(const wstring& s)
{
    wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[i + 1];
            if (n == L't') { o += L'\t'; ++i; }
            else if (n == L'r') { o += L'\r'; ++i; }
            else if (n == L'n') { o += L'\n'; ++i; }
            else if (n == L'\\') { o += L'\\'; ++i; }
            else o += s[i];
        } else {
            o += s[i];
        }
    }
    return o;
}

static void Split(const wstring& line, vector<wstring>& out)
{
    out.clear();
    wstring cur;
    for (wchar_t c : line) {
        if (c == L'\t') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
}

wstring NewId()
{
    static int counter = 0;
    wchar_t b[48];
    _snwprintf(b, 48, L"%08x%04x", (unsigned)GetTickCount(), (unsigned)(counter++ & 0xffff));
    return b;
}

// 识别码用的标题规范化：去掉首尾空白，把连续空白压成一个空格。
static wstring CodeTitle(const wstring& s)
{
    wstring out;
    bool pendingSpace = false;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') { pendingSpace = !out.empty(); continue; }
        if (pendingSpace) { out += L' '; pendingSpace = false; }
        out += c;
    }
    return out;
}

// 识别码 = SHA256(规范化内容) 的前 16 位十六进制。
// 规范化内容刻意只包含「定义这条待办是什么」的字段：
//   常驻：P|标题|HH:MM|开始日期|结束日期(或 -)
//   临时：T|标题|HH:MM|执行次数
// done/enabled/lastFired 这些「状态」字段不参与，否则提醒一次识别码就变了。
// 安卓端 Sync.java 里的 taskCode() 必须与这里逐字节一致。
wstring ComputeTaskCode(const Task& t)
{
    wstring key;
    wchar_t head[32];
    wstring title = CodeTitle(t.title);
    _snwprintf(head, 32, L"%c|", t.kind == KIND_TEMPORARY ? L'T' : L'P');
    key += head;
    key += title;
    _snwprintf(head, 32, L"|%02d:%02d|", t.hour, t.minute);
    key += head;
    if (t.kind == KIND_TEMPORARY) {
        _snwprintf(head, 32, L"%d", t.totalCount);
        key += head;
    } else {
        key += FormatDate(t.startDate);
        key += L"|";
        key += t.hasEnd ? FormatDate(t.endDate) : L"-";
    }
    wstring h = Sha256Hex(key);
    return h.size() >= 16 ? h.substr(0, 16) : h;
}

wstring TaskCodeShort(const wstring& code)
{
    return code.size() > 8 ? code.substr(0, 8) : code;
}

// 重新计算全部任务的识别码，并按识别码去重（内容相同的只保留一条）。
void RefreshTaskCodes(vector<Task>& list)
{
    vector<Task> keep;
    keep.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        Task t = list[i];
        t.code = ComputeTaskCode(t);
        bool dup = false;
        for (size_t j = 0; j < keep.size(); ++j) {
            if (keep[j].code == t.code) { dup = true; break; }
        }
        if (!dup) keep.push_back(t);
    }
    list.swap(keep);
}

bool LoadTasks(vector<Task>& out)
{
    out.clear();
    FILE* f = _wfopen(DataFilePath().c_str(), L"rb");
    if (!f) return false;

    std::string raw;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) raw.append(buf, got);
    fclose(f);

    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        raw.erase(0, 3);

    wstring text = Utf8ToW(raw);
    vector<wstring> lines;
    wstring cur;
    for (wchar_t c : text) {
        if (c == L'\n') { lines.push_back(cur); cur.clear(); }
        else if (c != L'\r') cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);

    vector<wstring> f2;
    for (const wstring& ln : lines) {
        if (ln.empty() || ln[0] == L'#') continue;
        Split(ln, f2);
        if (f2.size() < 12) continue;
        if (f2[0] != L"T") continue;

        Task t;
        t.id = f2[1];
        t.title = UnescapeField(f2[2]);
        t.kind = _wtoi(f2[3].c_str());
        t.hour = _wtoi(f2[4].c_str());
        t.minute = _wtoi(f2[5].c_str());
        ParseDate(f2[6], t.startDate);
        ParseDate(f2[7], t.endDate);
        t.hasEnd = _wtoi(f2[8].c_str()) != 0;
        t.totalCount = _wtoi(f2[9].c_str());
        t.doneCount = _wtoi(f2[10].c_str());
        t.enabled = _wtoi(f2[11].c_str()) != 0;
        if (f2.size() > 12) t.lastFiredKey = UnescapeField(f2[12]);
        if (f2.size() > 13) t.createdAt = _wtoi64(f2[13].c_str());

        if (t.id.empty()) t.id = NewId();
        if (t.hour < 0 || t.hour > 23) t.hour = 9;
        if (t.minute < 0 || t.minute > 59) t.minute = 0;
        if (t.totalCount < 1) t.totalCount = 1;
        if (t.doneCount < 0) t.doneCount = 0;
        if (t.startDate.wYear == 0) t.startDate = Today();
        if (t.endDate.wYear == 0) { t.endDate = AddDays(t.startDate, 30); t.hasEnd = false; }
        t.code = ComputeTaskCode(t);
        // 识别码相同 = 同一条待办（可能来自两端的重复同步），只保留第一条
        bool dup = false;
        for (size_t j = 0; j < out.size(); ++j) {
            if (out[j].code == t.code) { dup = true; break; }
        }
        if (!dup) out.push_back(t);
    }
    return true;
}

bool SaveTasks(const vector<Task>& list)
{
    wstring text = L"# 每日待办 数据文件 v2 —— 每行一条，\\t 分隔，请勿手工修改\r\n";
    text += L"# T id title kind hour minute start end hasEnd total done enabled lastFired createdAt\r\n";

    for (const Task& t : list) {
        wstring ln = L"T";
        ln += L"\t" + EscapeField(t.id);
        ln += L"\t" + EscapeField(t.title);
        ln += L"\t" + std::to_wstring(t.kind);
        ln += L"\t" + std::to_wstring(t.hour);
        ln += L"\t" + std::to_wstring(t.minute);
        ln += L"\t" + FormatDate(t.startDate);
        ln += L"\t" + FormatDate(t.endDate);
        ln += L"\t" + std::to_wstring(t.hasEnd ? 1 : 0);
        ln += L"\t" + std::to_wstring(t.totalCount);
        ln += L"\t" + std::to_wstring(t.doneCount);
        ln += L"\t" + std::to_wstring(t.enabled ? 1 : 0);
        ln += L"\t" + EscapeField(t.lastFiredKey);
        ln += L"\t" + std::to_wstring(t.createdAt);
        text += ln + L"\r\n";
    }

    std::string utf8 = WToUtf8(text);
    // 先写临时文件再原子替换，并在替换前留一份 .bak：
    // 这样即使进程在写盘瞬间被强杀/断电，也不会把 tasks_data.txt 截断成空文件。
    wstring path = DataFilePath();
    wstring tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return false;
    fwrite("\xEF\xBB\xBF", 1, 3, f);
    fwrite(utf8.data(), 1, utf8.size(), f);
    bool writeOk = (fclose(f) == 0);
    if (!writeOk) { DeleteFileW(tmp.c_str()); return false; }

    CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- system integration

void ShowToast(const wstring& title, const wstring& body, bool silent)
{
    if (!g_hMain) return;

    // 应用内横幅：即使系统通知被关闭（NOC_GLOBAL_SETTING_TOASTS_ENABLED=0）也能看见
    g_bannerTitle = title;
    g_bannerBody = body;
    g_bannerUntil = GetTickCount64() + 8000;
    FLASHWINFO fi;
    ZeroMemory(&fi, sizeof(fi));
    fi.cbSize = sizeof(fi);
    fi.hwnd = g_hMain;
    fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
    fi.uCount = 4;
    fi.dwTimeout = 0;
    FlashWindowEx(&fi);

    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hMain;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = silent ? (NIIF_NOSOUND | NIIF_INFO) : NIIF_INFO;
    nid.uTimeout = 10000;

    wstring tt = title.substr(0, 60);
    wstring bb = body.substr(0, 240);
    wcscpy_s(nid.szInfoTitle, tt.c_str());
    wcscpy_s(nid.szInfo, bb.c_str());

    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        // 托盘图标可能尚未添加，忽略 NIM_ADD 失败
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
        nid.uCallbackMessage = WM_APP + 1;
        nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        wcscpy_s(nid.szTip, L"每日待办");
        Shell_NotifyIconW(NIM_ADD, &nid);
    }
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunVal = L"每日待办";
static const wchar_t* kCfgKey = L"Software\\每日待办";

bool IsBlockInjectEnabled()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kCfgKey, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    DWORD v = 0, cb = sizeof(v), type = 0;
    LONG r = RegQueryValueExW(k, L"BlockThirdPartyDll", nullptr, &type, (LPBYTE)&v, &cb);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && v != 0;
}

void SetBlockInjectEnabled(bool on)
{
    HKEY k;
    DWORD disp = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kCfgKey, 0, nullptr, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &k, &disp) != ERROR_SUCCESS)
        return;
    DWORD v = on ? 1 : 0;
    RegSetValueExW(k, L"BlockThirdPartyDll", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
}

bool IsAutoStartEnabled()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    wchar_t buf[1024] = { 0 };
    DWORD cb = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    LONG r = RegQueryValueExW(k, kRunVal, nullptr, &type, (LPBYTE)buf, &cb);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && cb > 2;
}

// 诊断日志：仅用于排查开机自启写注册表失败，写入 exe 同目录 autostart.log
static int IntegrityRid()
{
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return -1;
    DWORD sz = 0;
    GetTokenInformation(tok, TokenIntegrityLevel, nullptr, 0, &sz);
    int rid = -2;
    BYTE* buf = (BYTE*)malloc(sz ? sz : 64);
    if (buf && GetTokenInformation(tok, TokenIntegrityLevel, buf, sz, &sz)) {
        TOKEN_MANDATORY_LABEL* ml = (TOKEN_MANDATORY_LABEL*)buf;
        DWORD n = *GetSidSubAuthorityCount(ml->Label.Sid);
        rid = (int)*GetSidSubAuthority(ml->Label.Sid, n - 1);
    }
    free(buf);
    CloseHandle(tok);
    return rid;
}

static void LogAuto(const wchar_t* what, LONG e, const wstring& extra)
{
    wstring p = DataFilePath();
    size_t pos = p.find_last_of(L'\\');
    if (pos != wstring::npos) p = p.substr(0, pos);
    p += L"\\autostart.log";
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    wchar_t buf[1200];
    int n = swprintf(buf, 1200, L"%ls : err=%ld : integrity=%d : %ls\r\n",
                     what, e, IntegrityRid(), extra.c_str());
    if (n > 0) {
        DWORD w = 0;
        WriteFile(h, buf, (DWORD)n * (DWORD)sizeof(wchar_t), &w, nullptr);
    }
    CloseHandle(h);
}

bool SetAutoStartEnabled(bool on)
{
    HKEY k;
    DWORD disp = 0;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                              KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &k, &disp);
    if (rc != ERROR_SUCCESS) {
        LogAuto(L"create-key", rc, kRunKey);
        return false;
    }

    bool ok = false;
    if (on) {
        wstring cmd = L"\"" + ExePath() + L"\" --autostart";
        LONG rv = RegSetValueExW(k, kRunVal, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                                 (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        ok = (rv == ERROR_SUCCESS);
        if (!ok) LogAuto(L"set-value", rv, cmd);
    } else {
        LONG r = RegDeleteValueW(k, kRunVal);
        ok = (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
        if (!ok) LogAuto(L"del-value", r, kRunVal);
    }
    RegCloseKey(k);
    return ok;
}

void OpenInExplorer(const wstring& path)
{
    wstring p = path;
    if (p.empty()) {
        wstring f = DataFilePath();
        size_t pos = f.find_last_of(L'\\');
        p = (pos == wstring::npos) ? L"." : f.substr(0, pos);
    }
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + DataFilePath() + L"\"").c_str(),
                  nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- app logic

void ReloadData()
{
    LoadTasks(g_tasks);
    g_autostart = IsAutoStartEnabled();
    g_blockInject = IsBlockInjectEnabled();
}

bool CommitForm()
{
    wstring title = Trim(g_form.title);
    if (title.empty()) return false;

    bool isNew = (g_editIndex < 0 || g_editIndex >= (int)g_tasks.size());
    Task* t = nullptr;
    if (isNew) {
        Task nt;
        nt.id = g_form.id.empty() ? NewId() : g_form.id;
        nt.createdAt = (long long)GetTickCount64();
        nt.lastFiredKey = L"";
        g_tasks.push_back(nt);
        t = &g_tasks.back();
    } else {
        t = &g_tasks[g_editIndex];
    }

    int oldHour = t->hour, oldMinute = t->minute;

    t->title = title;
    t->kind = g_form.kind;
    t->hour = g_form.hour;
    t->minute = g_form.minute;
    t->startDate = g_form.startDate;
    t->startDate.wHour = 0; t->startDate.wMinute = 0;
    t->endDate = g_form.endDate;
    t->endDate.wHour = 0; t->endDate.wMinute = 0;
    t->hasEnd = g_form.hasEnd;
    t->totalCount = g_form.totalCount < 1 ? 1 : g_form.totalCount;
    t->doneCount = g_form.doneCount;
    if (t->doneCount > t->totalCount) t->doneCount = t->totalCount;
    t->enabled = g_form.enabled;

    // 提醒时间变化后，允许当天重新提醒一次
    if (!isNew && (oldHour != t->hour || oldMinute != t->minute))
        t->lastFiredKey = L"";

    // 识别码：内容相同的待办视为同一条。
    // 新增时若已有同识别码的待办，则不重复添加（直接沿用已存在的那条）。
    t->code = ComputeTaskCode(*t);
    wstring newCode = t->code;
    int dupIdx = -1;
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        if (&g_tasks[i] == t) continue;
        if (g_tasks[i].code == newCode) { dupIdx = (int)i; break; }
    }
    if (dupIdx >= 0) {
        if (isNew) {
            g_tasks.pop_back();      // 撤销刚才的 push_back（t 随之失效，之后不再解引用）
            g_editIndex = dupIdx;    // 指向已存在的那条
        }
        ShowToast(L"这条待办已经存在", L"识别码相同（#" + TaskCodeShort(newCode) + L"），已跳过重复添加", true);
        SaveTasks(g_tasks);
        return true;
    }

    SaveTasks(g_tasks);
    return true;
}

void OpenEditFor(int index)
{
    g_editIndex = index;
    SYSTEMTIME today = Today();

    if (index >= 0 && index < (int)g_tasks.size()) {
        const Task& t = g_tasks[index];
        g_form.id = t.id;
        g_form.title = t.title;
        g_form.kind = t.kind;
        g_form.hour = t.hour;
        g_form.minute = t.minute;
        g_form.startDate = t.startDate;
        g_form.endDate = t.endDate;
        g_form.hasEnd = t.hasEnd;
        g_form.totalCount = t.totalCount;
        g_form.doneCount = t.doneCount;
        g_form.enabled = t.enabled;
    } else {
        g_form = EditForm();
        g_form.title = L"";
        g_form.kind = KIND_PERMANENT;
        g_form.hour = 9;
        g_form.minute = 0;
        g_form.startDate = today;
        g_form.endDate = AddDays(today, 30);
        g_form.hasEnd = false;
        g_form.totalCount = 3;
        g_form.doneCount = 0;
        g_form.enabled = true;
        g_form.id = L"";
    }
    g_page = 1;
}
