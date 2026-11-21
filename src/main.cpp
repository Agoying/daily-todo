#include "app.h"
#include "account.h"
#include <dwmapi.h>
#include <imm.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>

using namespace Gdiplus;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

#define APP_TITLE   L"每日待办"
#define WM_TRAYMSG  (WM_APP + 1)

// ---------------------------------------------------------------- ids

enum {
    H_NONE = 0,
    H_NAV_TODOS = 10, H_NAV_SETTINGS = 11, H_ADD = 12, H_NAV_ACCOUNT = 15,
    H_VIEW0 = 13, H_VIEW1 = 14, H_IMPORT = 16,
    H_MIN = 20, H_MAX = 21, H_CLOSE = 22,
    H_BACK = 30, H_SAVE = 31, H_CANCEL = 32,
    H_KIND_PERM = 40, H_KIND_TEMP = 41,
    H_HUP = 42, H_HDN = 43, H_MUP = 44, H_MDN = 45,
    H_START = 46, H_END = 47, H_NOEND = 48,
    H_CNTUP = 49, H_CNTDN = 50,
    H_CHIP0 = 60, H_CHIP1 = 61, H_CHIP2 = 62, H_CHIP3 = 63,
    H_AUTOSTART = 70, H_TRAYCLOSE = 71, H_SOUND = 72, H_PAUSED = 73,
    H_TESTTOAST = 74, H_OPENFOLDER = 75,
    H_BLOCKINJECT = 76,
    H_CARD_BASE = 1000
};

// 设置页行的命中 id，顺序与 kRows / g_setRowY 一致
static const int kSetIds[6] = { H_AUTOSTART, H_TRAYCLOSE, H_SOUND, H_BLOCKINJECT,
                                H_PAUSED, H_OPENFOLDER };

enum { MS_NORMAL = 0, MS_HOVER = 1, MS_PRESS = 2 };

// ---------------------------------------------------------------- state

struct Hot { int id; RECT r; };
struct CardSlot { int idx; RECT card; bool narrow; };

static vector<Hot>  g_hots;
static vector<CardSlot> g_cards;
static vector<int>  g_orderTimeline, g_orderPerm, g_orderTemp;

static int  g_cw = 0, g_ch = 0;
static int  g_hover = H_NONE;
static int  g_active = H_NONE;
static bool g_layoutDirty = true;
static int  g_scroll = 0;
static int  g_scrollMax = 0;
static bool g_trackingLeave = false;

static int  g_confirmDelIdx = -1;
static ULONGLONG g_confirmDelTick = 0;
static ULONGLONG g_saveErrorTick = 0;

// 自定义拖动缩放（窗口没有 WS_THICKFRAME，见 CreateWindowExW 处注释）
// 0=未在缩放 1=左 2=右 3=上 4=下 5=左上 6=右上 7=左下 8=右下
static int   g_rzDir = 0;
static POINT g_rzStartPt = { 0, 0 };
static RECT  g_rzStartRect = { 0, 0, 0, 0 };

static HWND  g_hEdit = nullptr;
static HFONT g_hEditFont = nullptr;
static HBRUSH g_brField = nullptr;
static RECT  g_rcEditField{};
static RECT  g_rcEditCtl{};

static HWND g_hCal = nullptr;
static int  g_calTarget = 0;
static int  g_calYear = 0, g_calMonth = 0;

// layout scratch
static RECT g_rcNav{}, g_rcList{}, g_rcH1{};
static RECT g_segView{}, g_segKind{};
static RECT g_rcAddBtn{};
static RECT g_rcImportBtn{};
static RECT g_btnCap[3]{};                 // title bar buttons
static RECT g_rcNavItem[3]{};
static int  g_btnCount = 0;
static RECT g_rcTime[2][3]{};              // [0]=hour [1]=minute, [0]=- [1]=value [2]=+
static RECT g_rcChips[4]{};
static RECT g_rcDate[2]{};
static RECT g_rcNoEnd{};
static RECT g_rcCountL{}, g_rcCountV{}, g_rcCountR{};
static RECT g_rcSave{}, g_rcCancel{}, g_rcBack{};
static RECT g_rcLabel[8]{};
static int  g_formX = 0, g_formW = 0;

// settings geometry
static RECT g_rcPanel1{}, g_rcPanel2{};
static int  g_setRowY[6]{};
static RECT g_rcSetSwitch[5]{};
static RECT g_rcOpenFolder{}, g_rcTestToast{};

// buffer
static HDC     g_memDC = nullptr;
static HBITMAP g_memBmp = nullptr;
static HGDIOBJ g_memOld = nullptr;
static int     g_memW = 0, g_memH = 0;

// ---------------------------------------------------------------- utils

static void TLog(const wchar_t* fmt, ...);

static void AddHot(int id, RECT r)
{    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return;
    Hot h; h.id = id; h.r = r; g_hots.push_back(h);
}

static void AddHotClip(int id, RECT r, const RECT& clip)
{
    RECT o;
    o.left   = r.left   > clip.left   ? r.left   : clip.left;
    o.top    = r.top    > clip.top    ? r.top    : clip.top;
    o.right  = r.right  < clip.right  ? r.right  : clip.right;
    o.bottom = r.bottom < clip.bottom ? r.bottom : clip.bottom;
    AddHot(id, o);
}

static bool PtIn(const RECT& r, int x, int y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

static int HitTest(int x, int y)
{
    int found = H_NONE;
    for (size_t i = 0; i < g_hots.size(); ++i)
        if (PtIn(g_hots[i].r, x, y)) found = g_hots[i].id;
    return found;
}

void Repaint() { if (g_hMain) InvalidateRect(g_hMain, nullptr, FALSE); }
void InvalidateLayout() { g_layoutDirty = true; }

static bool EnsureBuffer(HDC ref, int w, int h)
{
    if (w <= 0 || h <= 0) return false;
    if (g_memDC && g_memW == w && g_memH == h) return true;
    if (g_memBmp) { SelectObject(g_memDC, g_memOld); DeleteObject(g_memBmp); g_memBmp = nullptr; }
    if (g_memDC) { DeleteDC(g_memDC); g_memDC = nullptr; }

    g_memDC = CreateCompatibleDC(ref);
    BITMAPINFO bi; ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    g_memBmp = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!g_memBmp) return false;
    g_memOld = SelectObject(g_memDC, g_memBmp);
    g_memW = w; g_memH = h;
    return true;
}

static HBRUSH FieldBrush()
{
    if (!g_brField) {
        g_brField = CreateSolidBrush(RGB(TH.control.GetR(), TH.control.GetG(), TH.control.GetB()));
    }
    return g_brField;
}

// ---------------------------------------------------------------- widgets

static void DrawSeg(Graphics& g, RECT r, const wchar_t* a, const wchar_t* b, int sel)
{
    FillRound(g, r, S(9), TH.control);
    StrokeRound(g, r, S(9), TH.border, 1.0f);
    int iw = (r.right - r.left) / 2;
    for (int i = 0; i < 2; ++i) {
        RECT ir = { r.left + i * iw, r.top, r.left + (i + 1) * iw, r.bottom };
        if (i == sel) {
            RECT pill = Inflate(ir, -S(2), -S(2));
            FillRound(g, pill, S(8), TH.card);
            StrokeRound(g, pill, S(8), TH.border, 1.0f);
        }
        DrawTextC(g, i == 0 ? a : b, ir, FK_SUB, (i == sel) ? TH.text : TH.text2, ALIGN_CENTER);
    }
}

static void DrawSwitch(Graphics& g, RECT r, bool on, int state)
{
    int h = r.bottom - r.top;
    int rad = h / 2;
    Color track;
    if (on) track = (state == MS_HOVER) ? TH.accentHover : TH.accent;
    else    track = (state == MS_HOVER) ? TH.controlPressed : TH.border;
    FillRound(g, r, rad, track);

    int kd = h - S(6);
    int kx = on ? (r.right - S(3) - kd) : (r.left + S(3));
    RECT knob = { kx, r.top + S(3), kx + kd, r.top + S(3) + kd };
    FillRound(g, knob, kd / 2, on ? TH.onAccent : TH.card);
}

static void DrawChip(Graphics& g, RECT r, const wstring& text, const Color& bg, const Color& fg)
{
    FillRound(g, r, (r.bottom - r.top) / 2, bg);
    DrawTextC(g, text, r, FK_SMALL, fg, ALIGN_CENTER);
}

static void DrawButton(Graphics& g, RECT r, const wstring& text, int kind, int state)
{
    Color bg, fg;
    if (kind == 1) {
        bg = (state == MS_HOVER || state == MS_PRESS) ? TH.accentHover : TH.accent;
        fg = TH.onAccent;
        FillRound(g, r, S(7), bg);
    } else if (kind == 2) {
        // danger ghost
        if (state != MS_NORMAL) FillRound(g, r, S(7), TH.dangerSoft);
        else StrokeRound(g, r, S(7), TH.border, 1.0f);
        fg = TH.danger;
    } else {
        bg = (state == MS_PRESS) ? TH.controlPressed
           : (state == MS_HOVER) ? TH.controlHover : TH.control;
        fg = TH.text;
        FillRound(g, r, S(7), bg);
        StrokeRound(g, r, S(7), TH.border, 1.0f);
    }
    DrawTextC(g, text, r, FK_SUB, fg, ALIGN_CENTER);
}

static void DrawIconBtn(Graphics& g, RECT r, Glyph gl, int state,
                        const Color& fg, const Color& hoverBg)
{
    if (state != MS_NORMAL) FillRound(g, r, S(6), state == MS_PRESS ? TH.controlPressed : hoverBg);
    int d = (r.bottom - r.top) * 58 / 100;
    if (d > (r.right - r.left) * 58 / 100) d = (r.right - r.left) * 58 / 100;
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    RECT box = { cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2 };
    DrawGlyph(g, gl, box, fg, 1.7f);
}

static void DrawFieldBg(Graphics& g, RECT r, bool focused, bool error = false)
{
    FillRound(g, r, S(7), TH.control);
    if (error) StrokeRound(g, r, S(7), TH.danger, 1.6f);
    else StrokeRound(g, r, S(7), focused ? TH.accent : TH.border, focused ? 1.6f : 1.0f);
}

// ---------------------------------------------------------------- ordering

static void BuildOrders(const SYSTEMTIME& now)
{
    g_orderTimeline.clear();
    g_orderPerm.clear();
    g_orderTemp.clear();

    struct Key { int idx; long long sec; bool fin; };
    vector<Key> keys;
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        NextOccurrence n = ComputeNext(g_tasks[i], now);
        Key k; k.idx = (int)i; k.sec = n.secondsLeft; k.fin = n.finished;
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
        if (a.fin != b.fin) return !a.fin;
        if (a.fin) return a.idx < b.idx;
        if (a.sec != b.sec) return a.sec < b.sec;
        return a.idx < b.idx;
    });
    for (size_t i = 0; i < keys.size(); ++i) g_orderTimeline.push_back(keys[i].idx);

    vector<int> perm, temp;
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        if (g_tasks[i].kind == KIND_PERMANENT) perm.push_back((int)i);
        else temp.push_back((int)i);
    }
    auto byTime = [](int a, int b) {
        int va = g_tasks[a].hour * 60 + g_tasks[a].minute;
        int vb = g_tasks[b].hour * 60 + g_tasks[b].minute;
        if (va != vb) return va < vb;
        return g_tasks[a].title < g_tasks[b].title;
    };
    std::sort(perm.begin(), perm.end(), byTime);
    std::sort(temp.begin(), temp.end(), byTime);
    g_orderPerm = perm;
    g_orderTemp = temp;
}

// ---------------------------------------------------------------- edit control

static void EnsureEditFont()
{
    if (g_hEditFont) return;
    g_hEditFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void CreateEditCtl(HWND parent)
{
    if (g_hEdit) return;
    EnsureEditFont();
    g_hEdit = CreateWindowExW(0, L"EDIT", L"",
                              WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
                              0, 0, 10, 10, parent, (HMENU)101,
                              GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_hEditFont, TRUE);
    SendMessageW(g_hEdit, EM_SETLIMITTEXT, 60, 0);
    SendMessageW(g_hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(S(12), S(12)));
    SendMessageW(g_hEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"例如：喝水、起来活动一下");
}

static void DestroyEditCtl()
{
    if (g_hEdit) { DestroyWindow(g_hEdit); g_hEdit = nullptr; }
}

static void SyncEditPos()
{
    if (!g_hEdit) return;
    int w = g_rcEditCtl.right - g_rcEditCtl.left;
    int h = g_rcEditCtl.bottom - g_rcEditCtl.top;
    if (w <= 0 || h <= 0) return;
    SetWindowPos(g_hEdit, nullptr, g_rcEditCtl.left, g_rcEditCtl.top, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(g_hEdit, SW_SHOWNA);
}

// ---------------------------------------------------------------- layout

static void DoLayout(int w, int h)
{
    g_hots.clear();
    g_cards.clear();
    g_btnCount = 0;
    g_cw = w; g_ch = h;

    const int TITLEH = S(52);
    const int PAD = S(32);
    const int NAVW = S(216);

    g_rcNav = { 0, 0, NAVW, h };

    // 标题栏按钮
    {
        int bw = S(44), bh = S(30), by = (TITLEH - bh) / 2;
        g_btnCap[0] = { w - bw * 3 - S(10), by, w - bw * 2 - S(10), by + bh };
        g_btnCap[1] = { w - bw * 2 - S(10), by, w - bw - S(10), by + bh };
        g_btnCap[2] = { w - bw - S(10), by, w - S(10), by + bh };
        g_btnCount = 3;
        AddHot(H_MIN, g_btnCap[0]);
        AddHot(H_MAX, g_btnCap[1]);
        AddHot(H_CLOSE, g_btnCap[2]);
    }

    if (g_page == 0 || g_page == 2 || g_page == 3) {
        int ix = S(12), iw = NAVW - S(24), ih = S(42);
        int y = TITLEH + S(16);
        g_rcNavItem[0] = { ix, y, ix + iw, y + ih };
        g_rcNavItem[1] = { ix, y + ih + S(4), ix + iw, y + ih + S(4) + ih };
        g_rcNavItem[2] = { ix, y + (ih + S(4)) * 2, ix + iw, y + (ih + S(4)) * 2 + ih };
        AddHot(H_NAV_TODOS, g_rcNavItem[0]);
        AddHot(H_NAV_ACCOUNT, g_rcNavItem[1]);
        AddHot(H_NAV_SETTINGS, g_rcNavItem[2]);

        g_rcAddBtn = { ix, h - S(20) - S(42), ix + iw, h - S(20) };
        g_rcImportBtn = { ix, h - S(20) - S(42) - S(10) - S(36), ix + iw, h - S(20) - S(42) - S(10) };
        if (g_page == 0) {
            AddHot(H_ADD, g_rcAddBtn);
            AddHot(H_IMPORT, g_rcImportBtn);
        }
    }

    if (g_page == 3) {
        AccountLayout(w, h);
    }

    if (g_page == 0) {
        SYSTEMTIME now; GetLocalTime(&now);
        BuildOrders(now);

        int cx = NAVW + PAD;
        int cw = w - PAD - cx;
        int y = TITLEH + S(26);
        g_rcH1 = { cx, y, cx + cw, y + S(38) };

        int segW = S(196), segH = S(34);
        g_segView = { cx + cw - segW, y + S(2), cx + cw, y + S(2) + segH };
        AddHot(H_VIEW0, { g_segView.left, g_segView.top, g_segView.left + segW / 2, g_segView.bottom });
        AddHot(H_VIEW1, { g_segView.left + segW / 2, g_segView.top, g_segView.right, g_segView.bottom });

        int listTop = y + S(82);
        int listBottom = h - S(20);
        g_rcList = { cx, listTop, w - S(18), listBottom };

        int contentW = g_rcList.right - g_rcList.left - S(12);
        int listH = listBottom - listTop;

        if (g_view == 0) {
            int H = S(86), GAP = S(10);
            int n = (int)g_orderTimeline.size();
            int total = n * (H + GAP);
            g_scrollMax = total > listH ? (total - listH + GAP) : 0;
            if (g_scroll > g_scrollMax) g_scroll = g_scrollMax;
            if (g_scroll < 0) g_scroll = 0;

            int yy = listTop - g_scroll;
            for (int i = 0; i < n; ++i) {
                CardSlot cs;
                cs.idx = g_orderTimeline[i];
                cs.narrow = false;
                cs.card = { cx, yy, cx + contentW, yy + H };
                if (cs.card.bottom > listTop && cs.card.top < listBottom) g_cards.push_back(cs);
                yy += H + GAP;
            }
        } else {
            int colGap = S(16);
            int colW = (contentW - colGap) / 2;
            int hdrH = S(38);
            int H = S(92), GAP = S(10);
            int n0 = (int)g_orderPerm.size(), n1 = (int)g_orderTemp.size();
            int t0 = hdrH + (n0 ? n0 * (H + GAP) : S(70));
            int t1 = hdrH + (n1 ? n1 * (H + GAP) : S(70));
            int maxTotal = t0 > t1 ? t0 : t1;
            g_scrollMax = maxTotal > listH ? (maxTotal - listH + GAP) : 0;
            if (g_scroll > g_scrollMax) g_scroll = g_scrollMax;
            if (g_scroll < 0) g_scroll = 0;

            for (int c = 0; c < 2; ++c) {
                const vector<int>& ord = (c == 0) ? g_orderPerm : g_orderTemp;
                int x0 = cx + c * (colW + colGap);
                int yy = listTop - g_scroll + hdrH;
                for (int i = 0; i < (int)ord.size(); ++i) {
                    CardSlot cs;
                    cs.idx = ord[i];
                    cs.narrow = true;
                    cs.card = { x0, yy, x0 + colW, yy + H };
                    if (cs.card.bottom > listTop && cs.card.top < listBottom) g_cards.push_back(cs);
                    yy += H + GAP;
                }
            }
        }

        for (size_t i = 0; i < g_cards.size(); ++i) {
            const CardSlot& cs = g_cards[i];
            AddHotClip(H_CARD_BASE + cs.idx * 4 + 3, cs.card, g_rcList);

            int bw = S(60);
            int bh = S(30);
            int by = (cs.card.top + cs.card.bottom) / 2 - bh / 2;
            int bwDel = bw + ((g_confirmDelIdx == cs.idx) ? S(34) : 0);
            RECT bd = { cs.card.right - S(14) - bwDel, by, cs.card.right - S(14), by + bh };
            RECT be = { bd.left - bw - S(6), by, bd.left - S(6), by + bh };
            AddHotClip(H_CARD_BASE + cs.idx * 4 + 0, be, g_rcList);
            AddHotClip(H_CARD_BASE + cs.idx * 4 + 1, bd, g_rcList);
        }
    } else if (g_page == 1) {
        int x = S(48);
        int y = TITLEH + S(16);
        g_rcBack = { x, y, x + S(88), y + S(30) };
        AddHot(H_BACK, g_rcBack);

        y += S(50);
        g_rcH1 = { x, y, w - S(48), y + S(38) };
        y += S(54);

        int formW = S(640);
        if (formW > w - S(96)) formW = w - S(96);
        g_formW = formW;
        g_formX = x;
        int fx = g_formX;

        int labelH = S(18), gap1 = S(7), fieldH = S(42), gap2 = S(20);

        // 名称
        g_rcLabel[0] = { fx, y, fx + formW, y + labelH };
        y += labelH + gap1;
        g_rcEditField = { fx, y, fx + formW, y + fieldH };
        g_rcEditCtl = Inflate(g_rcEditField, -S(2), -S(2));
        y += fieldH + gap2;

        // 提醒时间
        g_rcLabel[1] = { fx, y, fx + formW, y + labelH };
        y += labelH + gap1;
        {
            int bw = S(32), boxW = S(62), g6 = S(6);
            int cx = fx;
            g_rcTime[0][0] = { cx, y, cx + bw, y + fieldH };                   cx += bw + g6;
            g_rcTime[0][1] = { cx, y, cx + boxW, y + fieldH };                 cx += boxW + g6;
            g_rcTime[0][2] = { cx, y, cx + bw, y + fieldH };                   cx += bw + g6;
            g_rcTime[1][0] = { cx, y, cx + bw, y + fieldH };                   cx += bw + g6;
            g_rcTime[1][1] = { cx, y, cx + boxW, y + fieldH };                 cx += boxW + g6;
            g_rcTime[1][2] = { cx, y, cx + bw, y + fieldH };                   cx += bw;
            AddHot(H_HDN, g_rcTime[0][0]);
            AddHot(H_HUP, g_rcTime[0][2]);
            AddHot(H_MDN, g_rcTime[1][0]);
            AddHot(H_MUP, g_rcTime[1][2]);

            int chipW = S(56), chipH = S(28), chipGap = S(6);
            int total = 4 * chipW + 3 * chipGap;
            int chipX = fx + formW - total;
            if (chipX < cx + S(16)) chipX = cx + S(16);
            int chipY = y + (fieldH - chipH) / 2;
            for (int i = 0; i < 4; ++i) {
                g_rcChips[i] = { chipX + i * (chipW + chipGap), chipY,
                                 chipX + i * (chipW + chipGap) + chipW, chipY + chipH };
                AddHot(H_CHIP0 + i, g_rcChips[i]);
            }
            y += fieldH + gap2;
        }

        // 类型
        g_rcLabel[2] = { fx, y, fx + formW, y + labelH };
        y += labelH + gap1;
        g_segKind = { fx, y, fx + S(248), y + S(38) };
        AddHot(H_KIND_PERM, { g_segKind.left, g_segKind.top, g_segKind.left + S(124), g_segKind.bottom });
        AddHot(H_KIND_TEMP, { g_segKind.left + S(124), g_segKind.top, g_segKind.right, g_segKind.bottom });
        y += S(38) + gap2;

        if (g_form.kind == KIND_PERMANENT) {
            int gapD = S(12);
            int dw = (formW - gapD) / 2;
            g_rcLabel[3] = { fx, y, fx + formW, y + labelH };
            y += labelH + gap1;
            g_rcDate[0] = { fx, y, fx + dw, y + fieldH };
            g_rcDate[1] = { fx + dw + gapD, y, fx + formW, y + fieldH };
            AddHot(H_START, g_rcDate[0]);
            AddHot(H_END, g_rcDate[1]);
            y += fieldH + S(8);

            g_rcNoEnd = { fx, y, fx + S(18), y + S(18) };
            AddHot(H_NOEND, { fx, y - S(3), fx + S(230), y + S(21) });
            y += S(18) + gap2;
        } else {
            g_rcLabel[3] = { fx, y, fx + formW, y + labelH };
            y += labelH + gap1;
            int bw = S(38), boxW = S(70);
            g_rcCountL = { fx, y, fx + bw, y + fieldH };
            g_rcCountV = { fx + bw + S(6), y, fx + bw + S(6) + boxW, y + fieldH };
            g_rcCountR = { g_rcCountV.right + S(6), y, g_rcCountV.right + S(6) + bw, y + fieldH };
            AddHot(H_CNTDN, g_rcCountL);
            AddHot(H_CNTUP, g_rcCountR);
            y += fieldH + gap2;
        }

        int fh = S(40);
        int fy = y + S(12);
        if (fy + fh > h - S(20)) fy = h - S(20) - fh;
        g_rcCancel = { fx + formW - S(100) - S(12) - S(100), fy,
                       fx + formW - S(100) - S(12), fy + fh };
        g_rcSave   = { fx + formW - S(100), fy, fx + formW, fy + fh };
        AddHot(H_CANCEL, g_rcCancel);
        AddHot(H_SAVE, g_rcSave);

        if (g_hEdit) SyncEditPos();
    } else if (g_page == 2) {
        int x = NAVW + PAD;
        int rw = w - x - S(32);
        if (rw > S(760)) rw = S(760);
        int y = TITLEH + S(26);
        g_rcH1 = { x, y, x + rw, y + S(38) };

        int rowH = S(66);
        y = g_rcH1.bottom + S(18);

        g_rcPanel1 = { x, y, x + rw, y + S(12) + 4 * rowH + S(8) };
        for (int i = 0; i < 4; ++i) {
            g_setRowY[i] = y + S(12) + i * rowH;
            g_rcSetSwitch[i] = { x + rw - S(62), g_setRowY[i] + S(24),
                                 x + rw - S(22), g_setRowY[i] + S(44) };
            AddHot(kSetIds[i], { x, g_setRowY[i], x + rw, g_setRowY[i] + rowH });
        }

        int y2 = g_rcPanel1.bottom + S(14);
        g_rcPanel2 = { x, y2, x + rw, y2 + S(12) + 2 * rowH + S(8) };
        for (int i = 0; i < 2; ++i) {
            g_setRowY[4 + i] = y2 + S(12) + i * rowH;
        }
        g_rcSetSwitch[4] = { x + rw - S(62), g_setRowY[4] + S(24),
                             x + rw - S(22), g_setRowY[4] + S(44) };
        AddHot(H_PAUSED, { x, g_setRowY[4], x + rw, g_setRowY[4] + rowH });

        g_rcOpenFolder = { x + rw - S(158), g_setRowY[5] + S(14),
                           x + rw - S(20), g_setRowY[5] + S(52) };
        AddHot(H_OPENFOLDER, g_rcOpenFolder);

        int y3 = g_rcPanel2.bottom + S(18);
        g_rcTestToast = { x, y3, x + S(168), y3 + S(38) };
        AddHot(H_TESTTOAST, g_rcTestToast);
    }
}

// ---------------------------------------------------------------- list page

static void DrawCard(Graphics& g, const CardSlot& cs, const SYSTEMTIME& now)
{
    const Task& t = g_tasks[cs.idx];
    NextOccurrence n = ComputeNext(t, now);
    int cardId = H_CARD_BASE + cs.idx * 4 + 3;
    bool hot = (g_hover == cardId);

    RECT card = cs.card;
    FillRound(g, card, S(10), hot ? TH.cardHover : TH.card);
    StrokeRound(g, card, S(10), TH.border, 1.0f);

    RECT bar = { card.left + S(1), card.top + S(13), card.left + S(4), card.bottom - S(13) };
    Color barC = (t.kind == KIND_PERMANENT) ? TH.accent : TH.warn;
    if (n.finished) barC = TH.text3;
    FillRound(g, bar, S(2), barC);

    bool narrow = cs.narrow;
    int left = card.left + S(18);
    int rightLimit = card.right - S(14);
    int bw = S(60);
    int bh = S(30);
    int by = (card.top + card.bottom) / 2 - bh / 2;
    int bwDel = bw + ((g_confirmDelIdx == cs.idx) ? S(34) : 0);
    RECT bd = { rightLimit - bwDel, by, rightLimit, by + bh };
    RECT be = { bd.left - bw - S(6), by, bd.left - S(6), by + bh };
    int textRight = be.left - S(10);

    if (!narrow) {
        RECT tr = { left, card.top + S(15), textRight, card.top + S(39) };
        DrawTextC(g, t.title, tr, FK_CARD, n.finished ? TH.text3 : TH.text);

        wstring meta;
        if (t.kind == KIND_PERMANENT) {
            meta = L"常驻 · 每天 " + FormatTime(t.hour, t.minute);
            meta += t.hasEnd ? (L" · 至 " + FormatDate(t.endDate)) : L" · 长期有效";
        } else {
            meta = L"临时 · " + FormatTime(t.hour, t.minute) + L" · 已执行 "
                 + std::to_wstring(t.doneCount) + L"/" + std::to_wstring(t.totalCount) + L" 次";
        }
        if (!t.code.empty()) meta += L"  ·  #" + TaskCodeShort(t.code);
        RECT mr = { left, card.top + S(43), textRight, card.top + S(63) };
        DrawTextC(g, meta, mr, FK_SMALL, TH.text2);

        wstring chipText;
        Color chipBg, chipFg;
        if (n.finished) {
            chipText = n.finishedText;
            chipBg = TH.bgSoft; chipFg = TH.text3;
        } else {
            chipText = PrettyLeft(n.secondsLeft);
            bool soon = n.secondsLeft < 3600;
            chipBg = soon ? TH.accentSoft : TH.bgSoft;
            chipFg = soon ? TH.accent : TH.text2;
        }
        int cw = MeasureTextW(g, chipText, FK_SMALL) + S(22);
        RECT cr = { be.left - S(14) - cw, card.top + S(15), be.left - S(14), card.top + S(39) };
        if (cr.left > left + S(250)) DrawChip(g, cr, chipText, chipBg, chipFg);
    } else {
        RECT tr = { left, card.top + S(11), textRight, card.top + S(33) };
        DrawTextC(g, t.title, tr, FK_CARD, n.finished ? TH.text3 : TH.text);

        wstring meta = (t.kind == KIND_PERMANENT)
            ? (L"每天 " + FormatTime(t.hour, t.minute))
            : (L"临时 " + FormatTime(t.hour, t.minute) + L" · "
               + std::to_wstring(t.doneCount) + L"/" + std::to_wstring(t.totalCount));
        RECT mr = { left, card.top + S(35), textRight, card.top + S(55) };
        DrawTextC(g, meta, mr, FK_SMALL, TH.text2);

        wstring tail = n.finished ? n.finishedText
                                  : PrettyLeft(n.secondsLeft);
        Color tf = n.finished ? TH.text3
                 : (n.secondsLeft < 3600 ? TH.accent : TH.text3);
        RECT rr = { left, card.top + S(57), textRight, card.top + S(77) };
        DrawTextC(g, tail, rr, FK_SMALL, tf);
    }

    // 修改
    int eid = H_CARD_BASE + cs.idx * 4 + 0;
    DrawButton(g, be, L"修改", 0,
               g_hover == eid ? MS_HOVER : (g_active == eid ? MS_PRESS : MS_NORMAL));

    // 删除（二次确认）
    int did = H_CARD_BASE + cs.idx * 4 + 1;
    bool confirm = (g_confirmDelIdx == cs.idx);
    DrawButton(g, bd, confirm ? L"确认删除" : L"删除", 2,
               (confirm || g_hover == did) ? MS_HOVER
               : (g_active == did ? MS_PRESS : MS_NORMAL));
}

static void DrawListPage(Graphics& g, int w, int h)
{
    (void)h;
    SYSTEMTIME now; GetLocalTime(&now);

    DrawTextC(g, L"今日待办", g_rcH1, FK_H1, TH.text);

    int active = 0;
    for (size_t i = 0; i < g_tasks.size(); ++i)
        if (!ComputeNext(g_tasks[i], now).finished) ++active;

    wstring sub = L"共 " + std::to_wstring(g_tasks.size()) + L" 项";
    if (active != (int)g_tasks.size()) sub += L" · 进行中 " + std::to_wstring(active) + L" 项";
    if (!g_orderTimeline.empty()) {
        const Task& t0 = g_tasks[g_orderTimeline[0]];
        NextOccurrence n0 = ComputeNext(t0, now);
        if (!n0.finished)
            sub += L" · 下一个 " + FormatTime(t0.hour, t0.minute) + L"（剩余 " + PrettyLeft(n0.secondsLeft) + L"）";
    }
    if (g_paused) sub += L" · 提醒已暂停";
    RECT sr = { g_rcH1.left, g_rcH1.bottom + S(2), g_rcH1.right, g_rcH1.bottom + S(24) };
    DrawTextC(g, sub, sr, FK_SUB, g_paused ? TH.warn : TH.text2);

    DrawSeg(g, g_segView, L"时间线", L"分栏", g_view);

    if (g_view == 1) {
        int contentW = g_rcList.right - g_rcList.left - S(12);
        int colGap = S(16);
        int colW = (contentW - colGap) / 2;
        const wchar_t* titles[2] = { L"常驻待办", L"临时代办" };
        for (int c = 0; c < 2; ++c) {
            int x0 = g_rcList.left + c * (colW + colGap);
            RECT hr = { x0, g_rcList.top, x0 + colW, g_rcList.top + S(24) };
            DrawTextC(g, titles[c], hr, FK_SUB, TH.text2);
            int n = (int)((c == 0) ? g_orderPerm.size() : g_orderTemp.size());
            wstring cn = std::to_wstring(n);
            int cwid = MeasureTextW(g, cn, FK_SMALL) + S(18);
            RECT cr = { x0 + S(66), g_rcList.top + S(2), x0 + S(66) + cwid, g_rcList.top + S(22) };
            DrawChip(g, cr, cn, TH.bgSoft, TH.text2);
            (void)n;
        }
    }

    GraphicsState st = g.Save();
    g.SetClip(Rect(g_rcList.left - S(4), g_rcList.top - S(4),
                   g_rcList.right - g_rcList.left + S(8),
                   g_rcList.bottom - g_rcList.top + S(4)));
    for (size_t i = 0; i < g_cards.size(); ++i) DrawCard(g, g_cards[i], now);
    g.Restore(st);

    if (g_tasks.empty()) {
        int cx = (g_rcList.left + g_rcList.right) / 2;
        int cy = (g_rcList.top + g_rcList.bottom) / 2 - S(36);
        RECT ic = { cx - S(22), cy - S(22), cx + S(22), cy + S(22) };
        DrawGlyph(g, GL_BELL, ic, TH.text3, 1.3f);
        RECT t1 = { cx - S(220), cy + S(34), cx + S(220), cy + S(62) };
        DrawTextC(g, L"还没有待办", t1, FK_CARD, TH.text2, ALIGN_CENTER);
        RECT t2 = { cx - S(220), cy + S(64), cx + S(220), cy + S(88) };
        DrawTextC(g, L"点击左下角「添加待办」创建第一条", t2, FK_SUB, TH.text3, ALIGN_CENTER);
    }

    if (g_scrollMax > 0) {
        int trackTop = g_rcList.top, trackH = g_rcList.bottom - g_rcList.top;
        int viewH = trackH;
        int contentH = viewH + g_scrollMax;
        int thumbH = (int)((long long)trackH * viewH / contentH);
        if (thumbH < S(28)) thumbH = S(28);
        int thumbY = trackTop + (int)((long long)(trackH - thumbH) * g_scroll / g_scrollMax);
        RECT thumb = { w - S(10), thumbY, w - S(4), thumbY + thumbH };
        FillRound(g, thumb, S(3), Color(70, 110, 110, 110));
    }
}

// ---------------------------------------------------------------- edit page

static void DrawTimeRow(Graphics& g)
{
    DrawIconBtn(g, g_rcTime[0][0], GL_CHEVRON_L, g_hover == H_HDN ? MS_HOVER : MS_NORMAL,
                TH.text2, TH.bgSoft);
    DrawFieldBg(g, g_rcTime[0][1], false);
    { wchar_t b[8]; _snwprintf(b, 8, L"%02d", g_form.hour);
      DrawTextC(g, b, g_rcTime[0][1], FK_H2, TH.text, ALIGN_CENTER); }
    DrawIconBtn(g, g_rcTime[0][2], GL_CHEVRON_R, g_hover == H_HUP ? MS_HOVER : MS_NORMAL,
                TH.text2, TH.bgSoft);

    RECT colon = { g_rcTime[0][2].right, g_rcTime[0][1].top,
                   g_rcTime[1][0].left, g_rcTime[0][1].bottom };
    DrawTextC(g, L":", colon, FK_H2, TH.text2, ALIGN_CENTER);

    DrawIconBtn(g, g_rcTime[1][0], GL_CHEVRON_L, g_hover == H_MDN ? MS_HOVER : MS_NORMAL,
                TH.text2, TH.bgSoft);
    DrawFieldBg(g, g_rcTime[1][1], false);
    { wchar_t b[8]; _snwprintf(b, 8, L"%02d", g_form.minute);
      DrawTextC(g, b, g_rcTime[1][1], FK_H2, TH.text, ALIGN_CENTER); }
    DrawIconBtn(g, g_rcTime[1][2], GL_CHEVRON_R, g_hover == H_MUP ? MS_HOVER : MS_NORMAL,
                TH.text2, TH.bgSoft);

    static const int ch[4][2] = { {8,0},{12,0},{18,0},{21,0} };
    for (int i = 0; i < 4; ++i) {
        bool sel = (g_form.hour == ch[i][0] && g_form.minute == ch[i][1]);
        RECT r = g_rcChips[i];
        if (sel) {
            FillRound(g, r, S(6), TH.accentSoft);
            StrokeRound(g, r, S(6), TH.accent, 1.2f);
            DrawTextC(g, FormatTime(ch[i][0], ch[i][1]), r, FK_SMALL, TH.accent, ALIGN_CENTER);
        } else {
            FillRound(g, r, S(6), g_hover == H_CHIP0 + i ? TH.controlHover : TH.control);
            StrokeRound(g, r, S(6), TH.border, 1.0f);
            DrawTextC(g, FormatTime(ch[i][0], ch[i][1]), r, FK_SMALL, TH.text2, ALIGN_CENTER);
        }
    }
}

static void DrawEditPage(Graphics& g, int w, int h)
{
    (void)w; (void)h;

    if (g_hover == H_BACK) FillRound(g, Inflate(g_rcBack, S(4), 0), S(6), TH.bgSoft);
    RECT chev = { g_rcBack.left + S(2), g_rcBack.top + S(6),
                  g_rcBack.left + S(22), g_rcBack.top + S(26) };
    DrawGlyph(g, GL_CHEVRON_L, chev, TH.text2, 1.6f);
    RECT bt = { g_rcBack.left + S(26), g_rcBack.top, g_rcBack.right, g_rcBack.bottom };
    DrawTextC(g, L"返回", bt, FK_SUB, TH.text2);

    DrawTextC(g, g_editIndex >= 0 ? L"修改待办" : L"添加待办", g_rcH1, FK_H1, TH.text);

    DrawTextC(g, L"待办名称", g_rcLabel[0], FK_SMALL, TH.text2);
    bool err = (GetTickCount64() - g_saveErrorTick < 2000);
    DrawFieldBg(g, g_rcEditField, g_hEdit && GetFocus() == g_hEdit, err);

    DrawTextC(g, L"提醒时间（每天）", g_rcLabel[1], FK_SMALL, TH.text2);
    DrawTimeRow(g);

    DrawTextC(g, L"待办类型", g_rcLabel[2], FK_SMALL, TH.text2);
    DrawSeg(g, g_segKind, L"常驻待办", L"临时代办", g_form.kind);

    if (g_form.kind == KIND_PERMANENT) {
        DrawTextC(g, L"生效区间", g_rcLabel[3], FK_SMALL, TH.text2);

        DrawFieldBg(g, g_rcDate[0], false);
        RECT r0 = { g_rcDate[0].left + S(14), g_rcDate[0].top,
                    g_rcDate[0].right - S(28), g_rcDate[0].bottom };
        DrawTextC(g, FormatDateCN(g_form.startDate) + L" " + WeekdayCN(g_form.startDate),
                  r0, FK_SUB, TH.text);
        RECT c0 = { g_rcDate[0].right - S(26), g_rcDate[0].top + S(12),
                    g_rcDate[0].right - S(26) + S(16), g_rcDate[0].top + S(30) };
        DrawGlyph(g, GL_CHEVRON_D, c0, TH.text3, 1.4f);

        DrawFieldBg(g, g_rcDate[1], false);
        RECT r1 = { g_rcDate[1].left + S(14), g_rcDate[1].top,
                    g_rcDate[1].right - S(28), g_rcDate[1].bottom };
        if (g_form.hasEnd)
            DrawTextC(g, FormatDateCN(g_form.endDate) + L" " + WeekdayCN(g_form.endDate),
                      r1, FK_SUB, TH.text);
        else
            DrawTextC(g, L"无结束日期", r1, FK_SUB, TH.text3);
        RECT c1 = { g_rcDate[1].right - S(26), g_rcDate[1].top + S(12),
                    g_rcDate[1].right - S(26) + S(16), g_rcDate[1].top + S(30) };
        DrawGlyph(g, GL_CHEVRON_D, c1, TH.text3, 1.4f);

        RECT cb = g_rcNoEnd;
        if (g_form.hasEnd) {
            FillRound(g, cb, S(4), g_hover == H_NOEND ? TH.controlHover : TH.control);
            StrokeRound(g, cb, S(4), TH.border, 1.0f);
        } else {
            FillRound(g, cb, S(4), TH.accent);
            DrawGlyph(g, GL_CHECK, Inflate(cb, -S(3), -S(3)), TH.onAccent, 1.8f);
        }
        RECT cl = { cb.right + S(8), g_rcNoEnd.top - S(3), g_rcNoEnd.right + S(220), g_rcNoEnd.bottom + S(3) };
        DrawTextC(g, L"无结束日期（长期有效）", cl, FK_SUB, TH.text2);
    } else {
        DrawTextC(g, L"执行次数", g_rcLabel[3], FK_SMALL, TH.text2);
        DrawIconBtn(g, g_rcCountL, GL_CHEVRON_L, g_hover == H_CNTDN ? MS_HOVER : MS_NORMAL,
                    TH.text2, TH.bgSoft);
        DrawFieldBg(g, g_rcCountV, false);
        DrawTextC(g, std::to_wstring(g_form.totalCount) + L" 次", g_rcCountV, FK_BODY, TH.text, ALIGN_CENTER);
        DrawIconBtn(g, g_rcCountR, GL_CHEVRON_R, g_hover == H_CNTUP ? MS_HOVER : MS_NORMAL,
                    TH.text2, TH.bgSoft);

        RECT hint = { g_rcCountR.right + S(16), g_rcCountV.top, g_formX + g_formW, g_rcCountV.bottom };
        DrawTextC(g, L"每次提醒后计数 +1，全部执行完自动完成", hint, FK_SMALL, TH.text3);
    }

    DrawButton(g, g_rcCancel, L"取消", 0,
               g_hover == H_CANCEL ? MS_HOVER : (g_active == H_CANCEL ? MS_PRESS : MS_NORMAL));
    DrawButton(g, g_rcSave, L"保存", 1,
               g_hover == H_SAVE ? MS_HOVER : (g_active == H_SAVE ? MS_PRESS : MS_NORMAL));

    RECT hint = { g_formX, g_rcSave.top + S(50), g_formX + g_formW, g_rcSave.top + S(72) };
    DrawTextC(g, L"保存后立即参与提醒调度。关闭窗口会最小化到托盘，提醒照常触发。",
              hint, FK_SMALL, TH.text3);

    // 修改已有待办时显示它的识别码（内容相同 = 识别码相同，同步时据此去重）
    if (g_editIndex >= 0 && g_editIndex < (int)g_tasks.size()) {
        RECT cr = { g_formX, g_rcSave.top + S(76), g_formX + g_formW, g_rcSave.top + S(98) };
        DrawTextC(g, L"识别码  #" + TaskCodeShort(g_tasks[g_editIndex].code)
                     + L"    （名称、时间、类型、区间/次数相同的待办识别码相同）",
                  cr, FK_SMALL, TH.text3);
    }
}

// ---------------------------------------------------------------- settings page

struct SetRow { const wchar_t* title; const wchar_t* desc; int id; };

static const SetRow kRows[6] = {
    { L"开机自动启动", L"登录 Windows 后自动运行，确保不会错过任何一次提醒", H_AUTOSTART },
    { L"关闭窗口时最小化到托盘", L"点关闭按钮后留在系统托盘继续运行，提醒不会中断", H_TRAYCLOSE },
    { L"提醒时播放提示音", L"发送系统通知的同时播放默认提示音", H_SOUND },
    { L"兼容模式：阻止第三方注入", L"仅作用于本程序。开启后可避免游戏加加等覆盖层把本程序带崩；代价是本程序内无法使用中文输入法输入待办名称", H_BLOCKINJECT },
    { L"暂停全部提醒", L"临时关闭提醒调度，待办数据完整保留", H_PAUSED },
    { L"数据文件", L"保存在程序目录的 tasks_data.txt，纯文本可直接备份", H_OPENFOLDER },
};

static void DrawSetRow(Graphics& g, int i, int x, int rw)
{
    int ry = g_setRowY[i];
    RECT row = { x, ry, x + rw, ry + S(66) };
    if (g_hover == kSetIds[i]) FillRound(g, Inflate(row, -S(1), S(1)), S(9), TH.cardHover);

    int rightPad = (i == 5) ? S(180) : S(78);
    RECT t = { x + S(20), ry + S(10), x + rw - rightPad, ry + S(32) };
    DrawTextC(g, kRows[i].title, t, FK_BODY, TH.text);
    RECT d = { x + S(20), ry + S(33), x + rw - rightPad, ry + S(55) };
    DrawTextC(g, kRows[i].desc, d, FK_SMALL, TH.text3);
}

static void DrawSettingsPage(Graphics& g, int w, int h)
{
    (void)w; (void)h;
    DrawTextC(g, L"设置", g_rcH1, FK_H1, TH.text);

    int x = g_rcPanel1.left;
    int rw = g_rcPanel1.right - g_rcPanel1.left;

    FillRound(g, g_rcPanel1, S(10), TH.card);
    StrokeRound(g, g_rcPanel1, S(10), TH.border, 1.0f);
    for (int i = 0; i < 4; ++i) {
        DrawSetRow(g, i, x, rw);
        if (i < 3) {
            RECT div = { x + S(20), g_setRowY[i] + S(66), x + rw - S(20), g_setRowY[i] + S(66) + 1 };
            FillRectC(g, div, TH.divider);
        }
    }

    bool vals[5] = { g_autostart, g_closeToTray, g_soundOn, g_blockInject, g_paused };
    for (int i = 0; i < 4; ++i) {
        DrawSwitch(g, g_rcSetSwitch[i], vals[i], g_hover == kSetIds[i] ? MS_HOVER : MS_NORMAL);
    }

    FillRound(g, g_rcPanel2, S(10), TH.card);
    StrokeRound(g, g_rcPanel2, S(10), TH.border, 1.0f);
    for (int i = 4; i < 6; ++i) {
        DrawSetRow(g, i, x, rw);
        if (i == 4) {
            RECT div = { x + S(20), g_setRowY[i] + S(66), x + rw - S(20), g_setRowY[i] + S(66) + 1 };
            FillRectC(g, div, TH.divider);
        }
    }
    DrawSwitch(g, g_rcSetSwitch[4], vals[4], g_hover == kSetIds[4] ? MS_HOVER : MS_NORMAL);
    DrawButton(g, g_rcOpenFolder, L"打开所在文件夹", 0,
               g_hover == H_OPENFOLDER ? MS_HOVER : MS_NORMAL);

    DrawButton(g, g_rcTestToast, L"发送测试通知", 0,
               g_hover == H_TESTTOAST ? MS_HOVER : MS_NORMAL);

    RECT about = { g_rcTestToast.right + S(18), g_rcTestToast.top,
                   g_rcTestToast.right + S(18) + S(420), g_rcTestToast.bottom };
    DrawTextC(g, L"每日待办 1.0 · 常驻 / 临时双视图 · Win11 风格", about, FK_SMALL, TH.text3);
}

// ---------------------------------------------------------------- calendar popup

static int DaysInMonth(int y, int m)
{
    static const int d[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return d[m - 1];
}

static int FirstWeekday(int y, int m)
{
    SYSTEMTIME st; ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)y; st.wMonth = (WORD)m; st.wDay = 1;
    FILETIME ft; SystemTimeToFileTime(&st, &ft);
    SYSTEMTIME out; FileTimeToSystemTime(&ft, &out);
    return out.wDayOfWeek;
}

static SYSTEMTIME* CalTarget()
{
    return g_calTarget == 0 ? &g_form.startDate : &g_form.endDate;
}

static int  g_calHover = -1;
static RECT g_calPrev{}, g_calNext{};
static int  g_calCellX[7], g_calCellY[6];
static int  g_calW = 0;

static void CalLayout(int w)
{
    g_calW = w;
    g_calPrev = { S(10), S(8), S(36), S(34) };
    g_calNext = { w - S(36), S(8), w - S(10), S(34) };
    int cw = (w - S(16)) / 7;
    for (int i = 0; i < 7; ++i) g_calCellX[i] = S(8) + i * cw;
    for (int i = 0; i < 6; ++i) g_calCellY[i] = S(74) + i * S(38);
    g_calCellX[7 - 1] = g_calCellX[6];
    (void)cw;
}

static void CalCellRect(int col, int row, RECT& out)
{
    int cw = (g_calW - S(16)) / 7;
    out.left = g_calCellX[col];
    out.top = g_calCellY[row];
    out.right = g_calCellX[col] + cw;
    out.bottom = g_calCellY[row] + S(38);
}

static void CalPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        SolidBrush bg(TH.card);
        g.FillRectangle(&bg, 0, 0, w, h);
        StrokeRound(g, { 0, 0, w - 1, h - 1 }, S(10), TH.border, 1.0f);

        CalLayout(w);

        wchar_t hdr[32];
        _snwprintf(hdr, 32, L"%d 年 %d 月", g_calYear, g_calMonth);
        RECT hr = { S(40), S(8), w - S(40), S(34) };
        DrawTextC(g, hdr, hr, FK_BODY, TH.text, ALIGN_CENTER);

        DrawIconBtn(g, g_calPrev, GL_CHEVRON_L, g_calHover == -2 ? MS_HOVER : MS_NORMAL,
                    TH.text2, TH.bgSoft);
        DrawIconBtn(g, g_calNext, GL_CHEVRON_R, g_calHover == -3 ? MS_HOVER : MS_NORMAL,
                    TH.text2, TH.bgSoft);

        const wchar_t* wd[7] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };
        for (int i = 0; i < 7; ++i) {
            RECT r = { g_calCellX[i], S(44), g_calCellX[i] + (w - S(16)) / 7, S(66) };
            DrawTextC(g, wd[i], r, FK_SMALL, TH.text3, ALIGN_CENTER);
        }

        int dim = DaysInMonth(g_calYear, g_calMonth);
        int fd = FirstWeekday(g_calYear, g_calMonth);
        SYSTEMTIME today = Today();
        SYSTEMTIME* tgt = CalTarget();

        for (int i = 0; i < dim; ++i) {
            int cell = fd + i;
            int col = cell % 7, row = cell / 7;
            if (row > 5) continue;
            RECT r; CalCellRect(col, row, r);
            r.left += S(2); r.right -= S(2); r.top += S(1); r.bottom -= S(1);

            SYSTEMTIME d; ZeroMemory(&d, sizeof(d));
            d.wYear = (WORD)g_calYear; d.wMonth = (WORD)g_calMonth; d.wDay = (WORD)(i + 1);

            bool isSel = (tgt && tgt->wYear == d.wYear && tgt->wMonth == d.wMonth && tgt->wDay == d.wDay);
            bool isToday = SameDate(d, today);
            bool hot = (g_calHover == cell);

            if (isSel) FillRound(g, r, S(7), TH.accent);
            else if (hot) FillRound(g, r, S(7), TH.bgSoft);
            else if (isToday) StrokeRound(g, Inflate(r, -S(1), -S(1)), S(7), TH.accent, 1.2f);

            Color fg = isSel ? TH.onAccent : (isToday ? TH.accent : TH.text);
            DrawTextC(g, std::to_wstring(i + 1), r, FK_SUB, fg, ALIGN_CENTER);
        }
    }
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK CalProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        CalPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT rc; GetClientRect(hwnd, &rc);
        CalLayout(rc.right);
        int old = g_calHover;
        g_calHover = -1;
        if (PtIn(g_calPrev, x, y)) g_calHover = -2;
        else if (PtIn(g_calNext, x, y)) g_calHover = -3;
        else {
            int dim = DaysInMonth(g_calYear, g_calMonth);
            int fd = FirstWeekday(g_calYear, g_calMonth);
            for (int i = 0; i < dim; ++i) {
                int cell = fd + i;
                int col = cell % 7, row = cell / 7;
                if (row > 5) break;
                RECT r; CalCellRect(col, row, r);
                if (PtIn(r, x, y)) { g_calHover = cell; break; }
            }
        }
        if (old != g_calHover) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT rc; GetClientRect(hwnd, &rc);
        CalLayout(rc.right);
        if (PtIn(g_calPrev, x, y)) {
            if (--g_calMonth < 1) { g_calMonth = 12; --g_calYear; }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (PtIn(g_calNext, x, y)) {
            if (++g_calMonth > 12) { g_calMonth = 1; ++g_calYear; }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        int dim = DaysInMonth(g_calYear, g_calMonth);
        int fd = FirstWeekday(g_calYear, g_calMonth);
        for (int i = 0; i < dim; ++i) {
            int cell = fd + i;
            int col = cell % 7, row = cell / 7;
            if (row > 5) break;
            RECT r; CalCellRect(col, row, r);
            if (!PtIn(r, x, y)) continue;

            SYSTEMTIME d; ZeroMemory(&d, sizeof(d));
            d.wYear = (WORD)g_calYear; d.wMonth = (WORD)g_calMonth; d.wDay = (WORD)(i + 1);
            if (g_calTarget == 0) {
                g_form.startDate = d;
                if (g_form.hasEnd && DaysBetween(g_form.startDate, g_form.endDate) < 0)
                    g_form.endDate = AddDays(d, 30);
            } else {
                if (DaysBetween(g_form.startDate, d) < 0) {
                    g_form.startDate = d;
                    g_form.endDate = AddDays(d, 30);
                } else {
                    g_form.endDate = d;
                }
                g_form.hasEnd = true;
            }
            ShowWindow(hwnd, SW_HIDE);
            Repaint();
            return 0;
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowCalendar(int target)
{
    if (!g_hMain) return;
    if (g_hCal && IsWindowVisible(g_hCal) && g_calTarget == target) {
        ShowWindow(g_hCal, SW_HIDE);
        return;
    }
    g_calTarget = target;

    if (!g_hCal) {
        static bool reg = false;
        if (!reg) {
            WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = CalProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = L"TdCalendarPopup";
            RegisterClassExW(&wc);
            reg = true;
        }
        g_hCal = CreateWindowExW(0, L"TdCalendarPopup", L"",
                                 WS_CHILD | WS_CLIPSIBLINGS, 0, 0, S(300), S(320),
                                 g_hMain, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    SYSTEMTIME* tgt = CalTarget();
    g_calYear = tgt->wYear ? tgt->wYear : 2026;
    g_calMonth = tgt->wMonth ? tgt->wMonth : 1;
    if (g_calMonth < 1 || g_calMonth > 12) g_calMonth = 1;
    g_calHover = -1;

    RECT anchor = (target == 0) ? g_rcDate[0] : g_rcDate[1];
    int w = S(300), h = S(320);
    int x = anchor.left;
    int y = anchor.bottom + S(6);
    RECT cr; GetClientRect(g_hMain, &cr);
    if (x + w > cr.right - S(8)) x = cr.right - S(8) - w;
    if (y + h > cr.bottom - S(8)) y = anchor.top - h - S(6);
    if (x < S(8)) x = S(8);
    if (y < S(8)) y = S(8);

    SetWindowPos(g_hCal, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
    InvalidateRect(g_hCal, nullptr, TRUE);
}

// ---------------------------------------------------------------- actions

static void SetPage(int p)
{
    g_page = p;
    g_scroll = 0;
    g_hover = H_NONE;
    g_active = H_NONE;
    g_confirmDelIdx = -1;

    if (p == 1) {
        CreateEditCtl(g_hMain);
        SetWindowTextW(g_hEdit, g_form.title.c_str());
        g_layoutDirty = true;
        DoLayout(g_cw, g_ch);
        SyncEditPos();
        SetFocus(g_hEdit);
    } else {
        DestroyEditCtl();
        SetFocus(g_hMain);
    }
    if (g_hCal) ShowWindow(g_hCal, SW_HIDE);
    g_layoutDirty = true;
    Repaint();
}

static void SaveFormFromUI()
{
    if (!g_hEdit) return;
    wchar_t buf[256];
    GetWindowTextW(g_hEdit, buf, 256);
    g_form.title = buf;
}

static void DoSave()
{
    SaveFormFromUI();
    if (g_form.title.size() == 0) {
        g_saveErrorTick = GetTickCount64();
        if (g_hEdit) SetFocus(g_hEdit);
        MessageBeep(MB_ICONWARNING);
        Repaint();
        return;
    }
    if (CommitForm()) {
        g_editIndex = -1;
        SetPage(0);
    }
}

static void DeleteTaskAt(int idx)
{
    if (idx < 0 || idx >= (int)g_tasks.size()) return;
    AccountMarkDeleted(g_tasks[idx].id);   // 写墓碑，让其它设备同步时也删掉
    g_tasks.erase(g_tasks.begin() + idx);
    SaveTasks(g_tasks);
    g_confirmDelIdx = -1;
    g_scroll = 0;
    g_layoutDirty = true;
    Repaint();
}

// 导入待办：读剪贴板里的识别码，本机有同码就提示重复，否则向局域网内的其它设备查询后导入
static void DoImportFromClipboard()
{
    wstring clip;
    if (OpenClipboard(g_hMain)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t* p = (const wchar_t*)GlobalLock(h);
            if (p) { clip = p; GlobalUnlock(h); }
        }
        CloseClipboard();
    }

    // 规范化：去掉 # 和空白（含全角空格），十六进制统一小写
    wstring code;
    for (size_t i = 0; i < clip.size(); ++i) {
        wchar_t c = clip[i];
        if (c == L'#' || c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000) continue;
        if (c >= L'A' && c <= L'F') c = (wchar_t)(c - L'A' + L'a');
        code.push_back(c);
    }
    if (code.size() < 8) {
        ShowToast(L"没读到识别码",
                  L"先在手机端「修改待办 → 更多选项 → 复制」，再回来点「导入识别码」（至少 8 位）",
                  false);
        return;
    }

    // 本机已有同码的待办？直接提示，不再重复添加
    for (size_t i = 0; i < g_tasks.size(); ++i) {
        wstring c = g_tasks[i].code.empty() ? ComputeTaskCode(g_tasks[i]) : g_tasks[i].code;
        wstring c8 = c.size() > 8 ? c.substr(0, 8) : c;
        if (c == code || c.find(code) == 0 || c8 == code) {
            ShowToast(L"已有相同待办",
                      L"「" + g_tasks[i].title + L"」（#" + TaskCodeShort(c) + L"），无需重复导入",
                      false);
            return;
        }
    }

    Task t;
    if (!AccountQueryTaskByCode(code, t)) {
        ShowToast(L"没找到这个识别码",
                  L"请确认另一台设备在线、已登录同一账号并开启了局域网同步", false);
        return;
    }

    t.id = NewId();
    t.createdAt = (long long)GetTickCount64();
    t.lastFiredKey = L"";          // 导入后今天到点仍会提醒
    t.code = ComputeTaskCode(t);
    g_tasks.push_back(t);
    SaveTasks(g_tasks);
    g_scroll = 0;
    g_layoutDirty = true;
    Repaint();
    ShowToast(L"已导入「" + t.title + L"」", L"#" + TaskCodeShort(t.code) + L" · 已加入待办列表", false);
}

static void Dispatch(int id)
{
    if (id >= H_CARD_BASE) {
        int rest = id - H_CARD_BASE;
        int idx = rest / 4;
        int sub = rest % 4;
        if (idx < 0 || idx >= (int)g_tasks.size()) return;
        if (sub == 0) { OpenEditFor(idx); SetPage(1); }
        else if (sub == 1) {
            if (g_confirmDelIdx == idx) DeleteTaskAt(idx);
            else {
                g_confirmDelIdx = idx;
                g_confirmDelTick = GetTickCount64();
                InvalidateLayout();
                Repaint();
            }
        }
        return;
    }

    switch (id) {
    case H_NAV_TODOS: SetPage(0); break;
    case H_NAV_SETTINGS: SetPage(2); break;
    case H_NAV_ACCOUNT: SetPage(3); break;
    case H_ADD: OpenEditFor(-1); SetPage(1); break;
    case H_IMPORT: DoImportFromClipboard(); break;
    case H_VIEW0: g_view = 0; g_scroll = 0; g_layoutDirty = true; Repaint(); break;
    case H_VIEW1: g_view = 1; g_scroll = 0; g_layoutDirty = true; Repaint(); break;
    case H_MIN: ShowWindow(g_hMain, SW_MINIMIZE); break;
    case H_MAX: ShowWindow(g_hMain, IsZoomed(g_hMain) ? SW_RESTORE : SW_MAXIMIZE); break;
    case H_CLOSE: PostMessageW(g_hMain, WM_CLOSE, 0, 0); break;
    case H_BACK:
    case H_CANCEL: g_editIndex = -1; SetPage(0); break;
    case H_SAVE: DoSave(); break;
    case H_KIND_PERM: g_form.kind = KIND_PERMANENT; g_layoutDirty = true; Repaint(); break;
    case H_KIND_TEMP: g_form.kind = KIND_TEMPORARY; g_layoutDirty = true; Repaint(); break;
    case H_HUP: g_form.hour = (g_form.hour + 1) % 24; Repaint(); break;
    case H_HDN: g_form.hour = (g_form.hour + 23) % 24; Repaint(); break;
    case H_MUP: g_form.minute = (g_form.minute + 5) % 60; Repaint(); break;
    case H_MDN: g_form.minute = (g_form.minute + 55) % 60; Repaint(); break;
    case H_CHIP0: g_form.hour = 8;  g_form.minute = 0; Repaint(); break;
    case H_CHIP1: g_form.hour = 12; g_form.minute = 0; Repaint(); break;
    case H_CHIP2: g_form.hour = 18; g_form.minute = 0; Repaint(); break;
    case H_CHIP3: g_form.hour = 21; g_form.minute = 0; Repaint(); break;
    case H_START: ShowCalendar(0); break;
    case H_END: ShowCalendar(1); break;
    case H_NOEND: g_form.hasEnd = !g_form.hasEnd; Repaint(); break;
    case H_CNTUP: if (g_form.totalCount < 99) ++g_form.totalCount; Repaint(); break;
    case H_CNTDN: if (g_form.totalCount > 1) --g_form.totalCount; Repaint(); break;
    case H_AUTOSTART: {
        bool want = !g_autostart;
        if (SetAutoStartEnabled(want)) {
            g_autostart = want;
        } else {
            g_autostart = IsAutoStartEnabled();
            ShowToast(L"无法修改开机自启",
                      L"写入注册表失败，请检查系统权限后重试（详情见程序目录 autostart.log）", false);
        }
        Repaint();
        break;
    }
    case H_TRAYCLOSE: g_closeToTray = !g_closeToTray; Repaint(); break;
    case H_SOUND: g_soundOn = !g_soundOn; Repaint(); break;
    case H_PAUSED: g_paused = !g_paused; Repaint(); break;
    case H_BLOCKINJECT: {
        bool want = !g_blockInject;
        SetBlockInjectEnabled(want);
        g_blockInject = IsBlockInjectEnabled();
        if (g_blockInject != want) {
            ShowToast(L"无法修改兼容模式", L"写入设置失败，请检查系统权限后重试。", true);
        } else if (want) {
            ShowToast(L"已开启兼容模式", L"重新打开本程序后生效：将阻止第三方覆盖层注入，代价是本程序内无法使用中文输入法。", false);
        } else {
            ShowToast(L"已关闭兼容模式", L"重新打开本程序后生效。", false);
        }
        Repaint();
        break;
    }
    case H_TESTTOAST:
        ShowToast(L"测试通知", L"提醒功能正常，系统通知已送达。", !g_soundOn);
        break;
    case H_OPENFOLDER: OpenInExplorer(L""); break;
    default:
        if (id >= H_ACC_BASE && id < H_ACC_BASE + 100) { AccountDispatch(id); return; }
        break;
    }
}

// ---------------------------------------------------------------- chrome

static void DrawTitleBar(Graphics& g, int w, int h)
{
    (void)h;
    int TITLEH = S(52);

    HICON ic = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 S(20), S(20), LR_DEFAULTCOLOR);
    if (ic) {
        HDC hdc = g.GetHDC();
        DrawIconEx(hdc, S(16), (TITLEH - S(20)) / 2, ic, S(20), S(20), 0, nullptr, DI_NORMAL);
        g.ReleaseHDC(hdc);
        DestroyIcon(ic);
    }
    RECT t = { S(46), 0, S(240), TITLEH };
    DrawTextC(g, APP_TITLE, t, FK_CARD, TH.text);

    for (int i = 0; i < g_btnCount; ++i) {
        RECT r = g_btnCap[i];
        int id = H_MIN + i;
        bool hot = (g_hover == id);
        if (hot) {
            if (i == 2) FillRound(g, Inflate(r, S(2), S(1)), S(5), TH.danger);
            else FillRound(g, Inflate(r, S(2), S(1)), S(5), TH.bgSoft);
        }
        Color fg = (hot && i == 2) ? TH.onAccent : TH.text2;
        int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        Pen pen(fg, (float)S(1));
        if (i == 0) {
            g.DrawLine(&pen, (REAL)(cx - S(5)), (REAL)cy, (REAL)(cx + S(5)), (REAL)cy);
        } else if (i == 1) {
            if (IsZoomed(g_hMain)) {
                g.DrawRectangle(&pen, (REAL)(cx - S(5)), (REAL)(cy - S(6)), (REAL)S(9), (REAL)S(9));
                g.DrawRectangle(&pen, (REAL)(cx - S(2)), (REAL)(cy - S(8)), (REAL)S(9), (REAL)S(9));
            } else {
                g.DrawRectangle(&pen, (REAL)(cx - S(5)), (REAL)(cy - S(5)), (REAL)S(10), (REAL)S(10));
            }
        } else {
            g.DrawLine(&pen, (REAL)(cx - S(5)), (REAL)(cy - S(5)), (REAL)(cx + S(5)), (REAL)(cy + S(5)));
            g.DrawLine(&pen, (REAL)(cx + S(5)), (REAL)(cy - S(5)), (REAL)(cx - S(5)), (REAL)(cy + S(5)));
        }
    }
    (void)w;
}

static void DrawNavRail(Graphics& g, int w, int h)
{
    (void)w;
    int TITLEH = S(52);
    RECT rail = { 0, TITLEH, g_rcNav.right, h };
    FillRectC(g, rail, TH.bgSoft);
    RECT sep = { g_rcNav.right - 1, TITLEH, g_rcNav.right, h };
    FillRectC(g, sep, TH.border);

    struct Item { const wchar_t* label; Glyph gl; int id; int page; };
    Item items[3] = {
        { L"待办", GL_BELL, H_NAV_TODOS, 0 },
        { L"账号", GL_PERSON, H_NAV_ACCOUNT, 3 },
        { L"设置", GL_GEAR, H_NAV_SETTINGS, 2 },
    };

    for (int i = 0; i < 3; ++i) {
        RECT r = g_rcNavItem[i];
        bool sel = (g_page == items[i].page);
        bool hot = (g_hover == items[i].id);
        if (sel) {
            FillRound(g, r, S(7), TH.card);
            StrokeRound(g, r, S(7), TH.border, 1.0f);
            RECT ind = { r.left - S(6), r.top + S(11), r.left - S(3), r.bottom - S(11) };
            FillRound(g, ind, S(2), TH.accent);
        } else if (hot) {
            FillRound(g, r, S(7), TH.navHover);
        }
        RECT ic = { r.left + S(14), r.top + S(12), r.left + S(14) + S(18), r.top + S(12) + S(18) };
        DrawGlyph(g, items[i].gl, ic, sel ? TH.accent : TH.text2, 1.5f);
        RECT tx = { r.left + S(44), r.top, r.right, r.bottom };
        DrawTextC(g, items[i].label, tx, FK_BODY, sel ? TH.text : TH.text2);
    }

    if (g_page == 0) {
        RECT ib = g_rcImportBtn;
        FillRound(g, ib, S(7), g_hover == H_IMPORT ? TH.controlHover : TH.control);
        StrokeRound(g, ib, S(7), TH.border, 1.0f);
        DrawTextC(g, L"导入识别码", ib, FK_SMALL, TH.text2);

        RECT ab = g_rcAddBtn;
        FillRound(g, ab, S(7), g_hover == H_ADD ? TH.accentHover : TH.accent);
        RECT ic = { ab.left + S(16), ab.top + S(12), ab.left + S(16) + S(18), ab.top + S(12) + S(18) };
        DrawGlyph(g, GL_PLUS, ic, TH.onAccent, 1.8f);
        RECT tx = { ab.left + S(44), ab.top, ab.right, ab.bottom };
        DrawTextC(g, L"添加待办", tx, FK_BODY, TH.onAccent);
    }
}

// ---------------------------------------------------------------- in-app banner

static void DrawBanner(Graphics& g, int w, int h)
{
    if (g_bannerUntil == 0 || GetTickCount64() > g_bannerUntil) return;

    int bw = S(380);
    if (bw > w - S(120)) bw = w - S(120);
    int bh = S(84);
    RECT r = { w - bw - S(28), h - bh - S(28), w - S(28), h - S(28) };

    FillRound(g, r, S(10), TH.card);
    StrokeRound(g, r, S(10), TH.accent, 1.4f);

    RECT bar = { r.left + S(1), r.top + S(12), r.left + S(4), r.bottom - S(12) };
    FillRound(g, bar, S(2), TH.accent);

    RECT t = { r.left + S(18), r.top + S(12), r.right - S(16), r.top + S(36) };
    DrawTextC(g, g_bannerTitle, t, FK_CARD, TH.text);

    RECT b = { r.left + S(18), r.top + S(42), r.right - S(16), r.bottom - S(10) };
    DrawTextC(g, g_bannerBody, b, FK_SMALL, TH.text2);
}

static void DrawFrame()
{
    TLog(L"paint enter");
    int w = g_cw, h = g_ch;
    if (w <= 0 || h <= 0) return;

    HDC hdc = GetDC(g_hMain);
    if (!EnsureBuffer(hdc, w, h)) { ReleaseDC(g_hMain, hdc); return; }

    {
        Graphics g(g_memDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        FillRectC(g, { 0, 0, w, h }, TH.bg);

        if (g_page != 1) DrawNavRail(g, w, h);
        DrawTitleBar(g, w, h);

        if (g_page == 0)      DrawListPage(g, w, h);
        else if (g_page == 1) DrawEditPage(g, w, h);
        else if (g_page == 2) DrawSettingsPage(g, w, h);
        else                  DrawAccountPage(g, w, h);

        DrawBanner(g, w, h);
    }

    BitBlt(hdc, 0, 0, w, h, g_memDC, 0, 0, SRCCOPY);
    ReleaseDC(g_hMain, hdc);
    TLog(L"paint leave");
}

// ---------------------------------------------------------------- diagnostics
//
// 默认不产生任何日志；设置环境变量 TD_DEBUG=1 后会在程序目录写 startup.log，
// 用于排查"启动即闪退 / 运行中消失"这类现场无法复现的问题。

static bool DebugOn()
{
    static int s = -1;
    if (s < 0) {
        wchar_t b[8] = { 0 };
        bool env = (GetEnvironmentVariableW(L"TD_DEBUG", b, 8) > 0 && (b[0] == L'1' || b[0] == L'y'));
        wstring flag = DataFilePath();
        size_t pos = flag.find_last_of(L'\\');
        flag = (pos == wstring::npos) ? wstring(L"debug.on") : flag.substr(0, pos) + L"\\debug.on";
        // 环境变量或程序目录下的 debug.on 标记文件存在时开启日志
        s = (env || GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    }
    return s == 1;
}

static wstring LogFilePath()
{
    wstring f = DataFilePath();
    size_t pos = f.find_last_of(L'\\');
    return (pos == wstring::npos) ? wstring(L"startup.log") : f.substr(0, pos) + L"\\startup.log";
}

static void TLog(const wchar_t* fmt, ...)
{
    if (!DebugOn()) return;
    FILE* fp = _wfopen(LogFilePath().c_str(), L"a, ccs=UTF-8");
    if (!fp) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fwprintf(fp, L"[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    vfwprintf(fp, fmt, ap);
    va_end(ap);
    fwprintf(fp, L"\n");
    fclose(fp);
}

static void LogException(const wchar_t* kind, EXCEPTION_POINTERS* ep)
{
    if (!DebugOn() || !ep || !ep->ExceptionRecord) return;
    void* a = ep->ExceptionRecord->ExceptionAddress;
    wchar_t mod[MAX_PATH] = L"?";
    HMODULE hm = nullptr;
    if (a && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCWSTR)a, &hm) && hm)
        GetModuleFileNameW(hm, mod, MAX_PATH);
    TLog(L"%ls code=0x%08lX addr=%p module=%ls", kind,
         (unsigned long)ep->ExceptionRecord->ExceptionCode, a, mod);
}

static LONG CALLBACK VectFilter(EXCEPTION_POINTERS* ep)
{
    static int n = 0;
    if (n < 40) { ++n; LogException(L"veh", ep); }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    LogException(L"unhandled", ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------- window proc

// 通知里显示的应用名（Windows 11 会把托盘气泡渲染成 toast，标题栏那个名字）：
// 不指定 AppUserModelID 时，系统会给托盘程序生成 NotifyIconGeneratedAumid_<hash>，
// 显示名取自 exe 的版本资源并且**缓存到注册表**——exe 改名后通知里仍是旧名字。
// 这里显式指定一个固定 AUMID 并写好显示名，既统一又叫得准。
// 声明在 shobjidl.h；这里不整体引入该头，单独声明即可（shell32 已链接）
extern "C" HRESULT WINAPI SetCurrentProcessExplicitAppUserModelID(PCWSTR appId);

static void InitAppIdentity()
{
    SetCurrentProcessExplicitAppUserModelID(L"DailyTodo.App");

    HKEY k;
    DWORD disp = 0;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\AppUserModelId\\DailyTodo.App",
                              0, nullptr, 0, KEY_WRITE, nullptr, &k, &disp);
    if (rc != ERROR_SUCCESS) {
        TLog(L"aumid reg failed rc=%ld", rc);
        return;
    }

    const wchar_t* name = L"每日待办";
    RegSetValueExW(k, L"DisplayName", 0, REG_SZ, (const BYTE*)name,
                   (DWORD)((wcslen(name) + 1) * sizeof(wchar_t)));

    // 通知右上角的小图标（未设置则用 exe 图标）
    wstring exe = DataFilePath();
    size_t slash = exe.find_last_of(L'\\');
    if (slash != wstring::npos) exe = exe.substr(0, slash) + L"\\每日待办.exe";
    RegSetValueExW(k, L"IconUri", 0, REG_SZ, (const BYTE*)exe.c_str(),
                   (DWORD)((exe.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

// DWM 会在窗口四周额外画一圈边框：聚焦时是灰色 (#B4B4B4)，失焦时变成接近白色 (#F4F7FC)，
// 画在自绘客户区之上，深色界面下失焦就像套了个大白框。用 DWMWA_BORDER_COLOR = DWMWA_COLOR_NONE
// 关掉它；注意该属性必须在窗口真正显示之后设置才生效，所以除了 WM_CREATE 还要在显示后与激活变化时重设。
static void ApplyDwmChrome(HWND hwnd)
{
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    COLORREF noBorder = (COLORREF)DWMWA_COLOR_NONE;
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));
    TLog(L"dwm border-none hr=0x%08lX", (unsigned long)hr);
}

// 返回 0=不在边框上；1=左 2=右 3=上 4=下 5=左上 6=右上 7=左下 8=右下
// 因为窗口没有 WS_THICKFRAME（为了去掉 DWM 画的白框），缩放全部由我们自己处理。
static int ResizeHitTest(int x, int y, int w, int h)
{
    int b = S(8);
    bool L = (x < b), R = (x >= w - b), T = (y < b), B = (y >= h - b);
    if (T && L) return 5;
    if (T && R) return 6;
    if (B && L) return 7;
    if (B && R) return 8;
    if (L) return 1;
    if (R) return 2;
    if (T) return 3;
    if (B) return 4;
    return 0;
}

static void UpdateResize(int x, int y, HWND hwnd)
{
    if (!g_rzDir) return;
    POINT sp = { x, y };
    ClientToScreen(hwnd, &sp);
    int dx = sp.x - g_rzStartPt.x, dy = sp.y - g_rzStartPt.y;
    RECT r = g_rzStartRect;

    if (g_rzDir == 1 || g_rzDir == 5 || g_rzDir == 7) r.left += dx;
    if (g_rzDir == 2 || g_rzDir == 6 || g_rzDir == 8) r.right += dx;
    if (g_rzDir == 3 || g_rzDir == 5 || g_rzDir == 6) r.top += dy;
    if (g_rzDir == 4 || g_rzDir == 7 || g_rzDir == 8) r.bottom += dy;

    int minW = S(900), minH = S(660);
    if (r.right - r.left < minW) {
        if (g_rzDir == 1 || g_rzDir == 5 || g_rzDir == 7) r.left = r.right - minW;
        else r.right = r.left + minW;
    }
    if (r.bottom - r.top < minH) {
        if (g_rzDir == 3 || g_rzDir == 5 || g_rzDir == 6) r.top = r.bottom - minH;
        else r.bottom = r.top + minH;
    }
    SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    TLog(L"resize apply dx=%d dy=%d -> %ld,%ld %ldx%ld", dx, dy,
         (long)r.left, (long)r.top, (long)(r.right - r.left), (long)(r.bottom - r.top));
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_IME_REQUEST) {
        // 主窗口没有文本输入，但输入法（如搜狗 TSF）仍会来要光标的字符位置。
        // 必须给出合法应答，否则第三方输入法在本进程内取到 NULL 后会直接崩掉宿主进程。
        switch (wp) {
        case IMR_QUERYCHARPOSITION: {
            IMECHARPOSITION* p = reinterpret_cast<IMECHARPOSITION*>(lp);
            RECT cr = { 0, 0, 0, 0 };
            if (g_hMain) GetClientRect(g_hMain, &cr);
            int cx = S(160), cy = S(80);
            if (g_page == 1 && g_hEdit && IsWindow(g_hEdit)) {
                RECT er; GetWindowRect(g_hEdit, &er);
                POINT p0 = { er.left, er.bottom };
                ScreenToClient(hwnd, &p0);
                cx = p0.x + S(6);
                cy = p0.y - S(4);
            }
            if (p && p->dwSize >= sizeof(IMECHARPOSITION)) {
                p->dwCharPos = 0;
                p->pt.x = cx;
                p->pt.y = cy;
                p->cLineHeight = S(20);
                p->rcDocument = cr;
                return TRUE;
            }
            return FALSE;
        }
        case IMR_COMPOSITIONWINDOW:
        case IMR_CANDIDATEWINDOW:
            return TRUE;
        case IMR_CONFIRMRECONVERTSTRING:
            return FALSE;
        default:
            return 0;
        }
    }
    if (msg == WM_IME_SETCONTEXT) {
        // 不在主窗口上显示输入法的组合窗口
        return DefWindowProcW(hwnd, msg, wp, lp & ~ISC_SHOWUICOMPOSITIONWINDOW);
    }
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hwnd;
        BOOL dark = FALSE;
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                          0, KEY_READ, &k) == ERROR_SUCCESS) {
            DWORD v = 1, cb = sizeof(v), type = 0;
            if (RegQueryValueExW(k, L"AppsUseLightTheme", nullptr, &type, (LPBYTE)&v, &cb) == ERROR_SUCCESS)
                dark = (v == 0);
            RegCloseKey(k);
        }
        InitTheme(dark != FALSE);
        if (g_brField) { DeleteObject(g_brField); g_brField = nullptr; }
        ApplyDwmChrome(hwnd);
        AccountSetHotCallback(&AddHot);
        AccountInit(hwnd);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        g_cw = rc.right; g_ch = rc.bottom;
        DoLayout(g_cw, g_ch);
        g_layoutDirty = false;
        DrawFrame();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        g_cw = LOWORD(lp); g_ch = HIWORD(lp);
        g_layoutDirty = true;
        Repaint();
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = S(900);
        mm->ptMinTrackSize.y = S(660);
        return 0;
    }

    case WM_NCCALCSIZE: {
        // 无 WS_THICKFRAME，客户区就是整个窗口矩形；最大化时也不再有边框外溢需要补偿。
        if (wp) return 0;
        break;
    }

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT wr; GetWindowRect(hwnd, &wr);
        int x = pt.x - wr.left, y = pt.y - wr.top;

        // 边框区域交回 HTCLIENT，由 WM_LBUTTONDOWN 里自己做缩放（见 ResizeHitTest）。
        // 只有标题栏区域交给系统处理拖动。
        if (y < S(52)) {
            for (int i = 0; i < g_btnCount; ++i)
                if (PtIn(g_btnCap[i], x, y)) return HTCLIENT;
            if (!IsZoomed(hwnd) && ResizeHitTest(x, y, wr.right - wr.left, wr.bottom - wr.top))
                return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (g_rzDir) { UpdateResize(x, y, hwnd); return 0; }
        if (g_layoutDirty) { DoLayout(g_cw, g_ch); g_layoutDirty = false; }
        int h = HitTest(x, y);
        if (h != g_hover) { g_hover = h; Repaint(); }
        if (!g_trackingLeave) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_trackingLeave = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_trackingLeave = false;
        if (g_hover != H_NONE) { g_hover = H_NONE; Repaint(); }
        return 0;

    case WM_SETCURSOR: {
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (!IsZoomed(hwnd)) {
                switch (ResizeHitTest(pt.x, pt.y, g_cw, g_ch)) {
                case 1: case 2: SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE;
                case 3: case 4: SetCursor(LoadCursorW(nullptr, IDC_SIZENS)); return TRUE;
                case 5: case 8: SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE)); return TRUE;
                case 6: case 7: SetCursor(LoadCursorW(nullptr, IDC_SIZENESW)); return TRUE;
                default: break;
                }
            }
            int h = HitTest(pt.x, pt.y);
            SetCursor(LoadCursorW(nullptr, h != H_NONE ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }

    case WM_CAPTURECHANGED:
        if (g_rzDir) g_rzDir = 0;
        break;

    case WM_LBUTTONDOWN: {
        if (g_layoutDirty) { DoLayout(g_cw, g_ch); g_layoutDirty = false; }
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);

        // 边框拖动缩放（窗口无 WS_THICKFRAME）
        if (!IsZoomed(hwnd)) {
            int dir = ResizeHitTest(x, y, g_cw, g_ch);
            if (dir) {
                g_rzDir = dir;
                POINT sp = { x, y };
                ClientToScreen(hwnd, &sp);
                g_rzStartPt = sp;
                GetWindowRect(hwnd, &g_rzStartRect);
                SetCapture(hwnd);
                TLog(L"resize begin dir=%d at %ld,%ld rect=%ld,%ld,%ld,%ld", dir,
                     (long)sp.x, (long)sp.y, (long)g_rzStartRect.left, (long)g_rzStartRect.top,
                     (long)g_rzStartRect.right, (long)g_rzStartRect.bottom);
                return 0;
            }
        }

        if (g_hCal && IsWindowVisible(g_hCal)) {
            RECT cr; GetWindowRect(g_hCal, &cr);
            POINT p = { x, y };
            ClientToScreen(hwnd, &p);
            if (!PtIn(cr, p.x, p.y)) ShowWindow(g_hCal, SW_HIDE);
        }
        if (g_confirmDelIdx >= 0) {
            int hid = HitTest(x, y);
            int idx = (hid >= H_CARD_BASE) ? (hid - H_CARD_BASE) / 4 : -1;
            if (idx != g_confirmDelIdx) { g_confirmDelIdx = -1; Repaint(); }
        }
        g_active = HitTest(x, y);
        if (g_active != H_NONE) SetCapture(hwnd);
        Repaint();
        return 0;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        if (g_rzDir) { g_rzDir = 0; return 0; }
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int id = HitTest(x, y);
        int was = g_active;
        g_active = H_NONE;
        if (id != H_NONE && id == was) { Repaint(); Dispatch(id); }
        else Repaint();
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int id = HitTest(x, y);
        if (id >= H_CARD_BASE) {
            int idx = (id - H_CARD_BASE) / 4;
            int sub = (id - H_CARD_BASE) % 4;
            if (sub == 3 && idx >= 0 && idx < (int)g_tasks.size()) {
                OpenEditFor(idx);
                SetPage(1);
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (PtIn(g_rcList, pt.x, pt.y) && g_scrollMax > 0) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            g_scroll -= (delta * S(60)) / 120;
            if (g_scroll < 0) g_scroll = 0;
            if (g_scroll > g_scrollMax) g_scroll = g_scrollMax;
            g_layoutDirty = true;
            Repaint();
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (g_page == 3 && AccountOnKey((int)wp)) { Repaint(); return 0; }
        if (wp == VK_ESCAPE) {
            if (g_hCal && IsWindowVisible(g_hCal)) { ShowWindow(g_hCal, SW_HIDE); return 0; }
            if (g_page == 1) { g_editIndex = -1; SetPage(0); return 0; }
        }
        break;
    }

    case WM_CHAR: {
        if (g_page == 3 && AccountOnChar((wchar_t)wp)) { Repaint(); return 0; }
        break;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(TH.text.GetR(), TH.text.GetG(), TH.text.GetB()));
        SetBkColor(hdc, RGB(TH.control.GetR(), TH.control.GetG(), TH.control.GetB()));
        return (LRESULT)FieldBrush();
    }

    case WM_DPICHANGED: {
        int nd = HIWORD(wp);
        if (nd > 0 && nd != g_dpi) {
            g_dpi = nd;
            g_scale = (float)nd / 96.0f;
            InitFonts(g_dpi);
            if (g_hEditFont) { DeleteObject(g_hEditFont); g_hEditFont = nullptr; }
            EnsureEditFont();
            if (g_hEdit) SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_hEditFont, TRUE);
            RECT* pr = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, pr->left, pr->top,
                         pr->right - pr->left, pr->bottom - pr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            g_layoutDirty = true;
            Repaint();
        }
        return 0;
    }

    case WM_CLOSE: {
        if (g_closeToTray) {
            ShowWindow(hwnd, SW_HIDE);
            ShowToast(L"每日待办仍在运行", L"已最小化到托盘，提醒照常触发。", true);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_DESTROY:
        AccountShutdown();
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;

    case WM_TRAYMSG: {
        UINT ev = LOWORD(lp);
        if (ev == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        } else if (ev == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 9001, L"打开主界面");
            AppendMenuW(menu, MF_STRING, 9002, L"添加待办");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | (g_paused ? MF_CHECKED : 0), 9003, L"暂停提醒");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 9004, L"退出");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 9001) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
        else if (id == 9002) {
            ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
            OpenEditFor(-1); SetPage(1);
        } else if (id == 9003) { g_paused = !g_paused; SetPage(2); Repaint(); }
        else if (id == 9004) { DestroyWindow(hwnd); }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- entry

static void Tick(HWND hwnd)
{
    TLog(L"tick enter");
    SYSTEMTIME now; GetLocalTime(&now);
    SchedulerTick(hwnd, now);
    AccountTick(hwnd);

    // 某些"游戏助手/性能监控"类软件（如游戏加加的 GPP64.dll）会把覆盖层 DLL
    // 注入到任意带窗口的进程里，并在其中崩溃把宿主进程一起带走。
    // 我们无法阻止注入，但可以在检测到之后明确告知用户。
    static int s_injectWarned = 0;
    if (!s_injectWarned && GetTickCount64() > 2000 &&
        (GetModuleHandleW(L"GPP64.dll") || GetModuleHandleW(L"GPP32.dll"))) {
        s_injectWarned = 1;
        TLog(L"third-party overlay detected: GPP64/32");
        ShowToast(L"检测到「游戏加加」注入本程序",
                  L"它可能导致本程序闪退。建议退出游戏加加（或关闭其游戏内覆盖）后重新打开本程序。", false);
    }

    if (g_confirmDelIdx >= 0 && GetTickCount64() - g_confirmDelTick > 4000) {
        g_confirmDelIdx = -1;
        InvalidateLayout();
        Repaint();
    }

    if (g_bannerUntil && GetTickCount64() > g_bannerUntil) {
        g_bannerUntil = 0;
        Repaint();
    }

    static int lastMinute = -1;
    if (now.wMinute != lastMinute) {
        lastMinute = now.wMinute;
        g_layoutDirty = true;
        Repaint();
    }
    TLog(L"tick leave");
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    // 关掉「损坏的映像 / 无法加载 DLL」这类系统硬错误弹窗：
    // 兼容模式会让第三方覆盖层 DLL 加载失败，若不禁用弹窗，Windows 会弹出模态错误框，
    // 加载失败本身是我们要的结果，不该打扰用户。
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // 通知里显示的应用名：显式指定 AppUserModelID 并登记显示名，
    // 否则 Windows 会给托盘程序自动生成一个 NotifyIconGeneratedAumid_xxx，
    // 名字取自 exe 版本资源；一旦生成就会缓存到注册表，改名后仍是旧名字。
    InitAppIdentity();

    AddVectoredExceptionHandler(1, VectFilter);
    SetUnhandledExceptionFilter(CrashFilter);

    // 兼容模式：必须在任何第三方 DLL 被注入之前生效。
    // 只影响本进程——其它程序（浏览器、Office、输入法在别处的使用）完全不受影响。
    if (IsBlockInjectEnabled()) {
        PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY sig;
        ZeroMemory(&sig, sizeof(sig));
        sig.MicrosoftSignedOnly = 1;
        BOOL ok = SetProcessMitigationPolicy(ProcessSignaturePolicy, &sig, sizeof(sig));
        TLog(L"block-inject policy applied=%d err=%lu", (int)ok, GetLastError());
    }
    TLog(L"==== start cmdline='%ls' nCmdShow=%d", lpCmdLine ? lpCmdLine : L"", nCmdShow);
    typedef BOOL(WINAPI * PFN_SPDAC)(HANDLE);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        PFN_SPDAC f = reinterpret_cast<PFN_SPDAC>(reinterpret_cast<void*>(
            GetProcAddress(u32, "SetProcessDpiAwarenessContext")));
        if (f) f((HANDLE)-4);
    }

    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    GdiplusStartup(&token, &gsi, nullptr);

    HDC sdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);
    if (g_dpi <= 0) g_dpi = 96;
    g_scale = (float)g_dpi / 96.0f;
    InitTheme(false);
    InitFonts(g_dpi);

    HICON appIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
                                      LR_DEFAULTSIZE | LR_DEFAULTCOLOR);

    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"TdMainWindow";
    SetLastError(0);
    ATOM atom = RegisterClassExW(&wc);
    if (!atom) return 0;

    int ww = S(1040), wh = S(760);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int wx = (sw - ww) / 2, wy = (sh - wh) / 2;

    SetLastError(0);
    // 注意：这里刻意不带 WS_THICKFRAME。
    // 只要窗口带 WS_THICKFRAME，DWM 就会在窗口矩形内侧再画一圈 8 物理像素的「尺寸边框」，
    // 聚焦时是灰色、失焦时接近白色，而且画在自绘客户区之上（DWMWA_BORDER_COLOR 也关不掉），
    // 深色界面下就成了一整圈刺眼的白框。缩放改为由 WM_NCHITTEST 返回 HTLEFT/HTTOP… 实现，
    // 同样能拖动边缘改变大小（含 WM_GETMINMAXINFO 的最小尺寸约束）。
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"TdMainWindow", APP_TITLE,
                                WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
                                wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 0;

    // 主窗口没有任何文本输入，主动与输入法解绑：
    // 部分第三方输入法（如搜狗 TSF）在被关联到这种自绘无边框窗口时会把握不到
    // 光标位置并直接把宿主进程带走。输入框是独立的 EDIT 子控件，自带输入上下文，
    // 中文输入不受影响。
    ImmAssociateContext(hwnd, nullptr);

    ReloadData();

    // 首次运行生成示例数据
    {
        FILE* f = _wfopen(DataFilePath().c_str(), L"rb");
        if (!f) {
            SYSTEMTIME t = Today();
            Task a; a.id = NewId(); a.title = L"喝水"; a.kind = KIND_PERMANENT;
            a.hour = 9; a.minute = 30; a.startDate = t; a.endDate = AddDays(t, 30);
            a.hasEnd = false; a.createdAt = 1;
            Task b; b.id = NewId(); b.title = L"站起来活动一下"; b.kind = KIND_PERMANENT;
            b.hour = 15; b.minute = 0; b.startDate = t; b.endDate = AddDays(t, 30);
            b.hasEnd = false; b.createdAt = 2;
            Task c; c.id = NewId(); c.title = L"整理今日工作记录"; c.kind = KIND_TEMPORARY;
            c.hour = 18; c.minute = 30; c.totalCount = 3; c.doneCount = 0;
            c.startDate = t; c.endDate = AddDays(t, 7); c.hasEnd = true; c.createdAt = 3;
            g_tasks.push_back(a);
            g_tasks.push_back(b);
            g_tasks.push_back(c);
            SaveTasks(g_tasks);
        } else {
            fclose(f);
        }
    }

    NOTIFYICONDATAW nid; ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYMSG;
    nid.hIcon = appIcon ? appIcon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wcscpy_s(nid.szTip, L"每日待办");
    Shell_NotifyIconW(NIM_ADD, &nid);

    bool autostartMode = (lpCmdLine && wcsstr(lpCmdLine, L"--autostart") != nullptr);

    // 某些启动方式（例如由 explorer 以“最小化”标志拉起）会把 nCmdShow 传成
    // SW_SHOWMINIMIZED / SW_SHOWMINNOACTIVE，那样窗口一出来就是最小化的，看着像没启动成功。
    // 本程序没有“静默启动到托盘”的需求，统一按正常显示处理。
    if (nCmdShow == SW_SHOWMINIMIZED || nCmdShow == SW_SHOWMINNOACTIVE) nCmdShow = SW_SHOWNORMAL;

    ShowWindow(hwnd, autostartMode ? SW_HIDE : (nCmdShow ? nCmdShow : SW_SHOW));
    if (!autostartMode && IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    ApplyDwmChrome(hwnd);
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 1000, nullptr);

    {
        SYSTEMTIME now; GetLocalTime(&now);
        SchedulerTick(hwnd, now);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_page == 1 && g_hEdit && (msg.hwnd == g_hEdit || IsChild(hwnd, msg.hwnd))) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) { DoSave(); continue; }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                g_editIndex = -1; SetPage(0); continue;
            }
        }
        if (msg.message == WM_TIMER && msg.hwnd == hwnd && msg.wParam == 1) {
            Tick(hwnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    GdiplusShutdown(token);
    return (int)msg.wParam;
}
