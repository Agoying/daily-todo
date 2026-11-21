// 每日待办 — shared declarations
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>

using std::wstring;
using std::vector;

// ---------------------------------------------------------------- model

enum TaskKind { KIND_PERMANENT = 0, KIND_TEMPORARY = 1 };

struct Task
{
    wstring id;
    wstring title;
    int kind = KIND_PERMANENT;
    int hour = 9;
    int minute = 0;
    // 常驻：生效日期区间（含端点）
    SYSTEMTIME startDate{};   // 仅日期有效
    SYSTEMTIME endDate{};     // 仅日期有效
    bool hasEnd = true;       // false = 长期有效
    // 临时：执行次数
    int totalCount = 3;
    int doneCount = 0;
    bool enabled = true;
    wstring lastFiredKey;     // "YYYY-MM-DD HH:MM" 防止重复提醒
    long long createdAt = 0;  // FILETIME 排序用
    // 识别码：由「名称 + 时间 + 类型 + 区间/次数」算出的稳定摘要（16 位十六进制）。
    // 内容相同的待办在两台设备上识别码也相同，同步时据此跳过，不会重复。
    wstring code;
};

wstring ComputeTaskCode(const Task& t);   // 由内容算识别码（16 位十六进制）
wstring TaskCodeShort(const wstring& code); // 取前 8 位用于界面显示
wstring Sha256Hex(const wstring& text);   // SHA-256 十六进制摘要（UTF-8 输入）
void RefreshTaskCodes(vector<Task>& list); // 重算识别码并按识别码去重

// ---------------------------------------------------------------- theme

struct Theme
{
    bool dark = false;
    Gdiplus::Color bg, bgSoft, card, cardHover, border, divider;
    Gdiplus::Color text, text2, text3;
    Gdiplus::Color accent, accentHover, accentSoft, onAccent;
    Gdiplus::Color danger, dangerSoft, success, warn;
    Gdiplus::Color navHover, navActive, control, controlHover, controlPressed;
    Gdiplus::Color shadow;
};

extern Theme TH;
extern int   g_dpi;          // 96 = 100%
extern float g_scale;

int  S(int v);               // 逻辑像素 -> 物理像素
void InitTheme(bool dark);
void InitFonts(int dpi);

// ---------------------------------------------------------------- drawing helpers

enum FontKind { FK_H1, FK_H2, FK_CARD, FK_BODY, FK_SUB, FK_SMALL, FK_MONO, FK_COUNT };
Gdiplus::Font* F(FontKind k);

void FillRound(Gdiplus::Graphics& g, const RECT& r, int radius, const Gdiplus::Color& c);
void FillRound(Gdiplus::Graphics& g, const RECT& r, int radius, Gdiplus::Brush& b);
void StrokeRound(Gdiplus::Graphics& g, const RECT& r, int radius, const Gdiplus::Color& c, float w = 1.0f);
void FillRectC(Gdiplus::Graphics& g, const RECT& r, const Gdiplus::Color& c);

enum TextAlign { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };
void DrawTextC(Gdiplus::Graphics& g, const wstring& s, const RECT& r, FontKind fk,
               const Gdiplus::Color& c, TextAlign align = ALIGN_LEFT, bool wrap = false);
void DrawTextC(Gdiplus::Graphics& g, const wstring& s, const RECT& r, Gdiplus::Font* f,
               const Gdiplus::Color& c, TextAlign align = ALIGN_LEFT, bool wrap = false);
int  MeasureTextW(Gdiplus::Graphics& g, const wstring& s, FontKind fk);
RECT Inflate(RECT r, int dx, int dy);

// simple vector glyphs
enum Glyph { GL_PLUS, GL_EDIT, GL_TRASH, GL_GEAR, GL_CHEVRON_L, GL_CHEVRON_R,
             GL_CHEVRON_D, GL_CLOCK, GL_CHECK, GL_BELL, GL_POWER, GL_FOLDER, GL_SEARCH,
             GL_PERSON };
void DrawGlyph(Gdiplus::Graphics& g, Glyph gl, const RECT& box, const Gdiplus::Color& c, float stroke = 1.6f);

// ---------------------------------------------------------------- persistence

wstring DataFilePath();
bool    LoadTasks(vector<Task>& out);
bool    SaveTasks(const vector<Task>& list);
wstring NewId();
wstring FormatDate(const SYSTEMTIME& st);                 // 2026-10-30
wstring FormatDateCN(const SYSTEMTIME& st);               // 2026年10月30日
wstring FormatTime(int h, int m);                         // 09:00
wstring WeekdayCN(const SYSTEMTIME& st);
bool    ParseDate(const wstring& s, SYSTEMTIME& out);
SYSTEMTIME Today();
SYSTEMTIME AddDays(const SYSTEMTIME& st, int days);
int     DaysBetween(const SYSTEMTIME& a, const SYSTEMTIME& b);  // b - a
bool    SameDate(const SYSTEMTIME& a, const SYSTEMTIME& b);
bool    DateInRange(const SYSTEMTIME& d, const SYSTEMTIME& lo, const SYSTEMTIME& hi);

// ---------------------------------------------------------------- scheduling

struct NextOccurrence
{
    bool valid = false;
    SYSTEMTIME when{};        // 下次提醒时刻
    long long  secondsLeft = 0;
    bool finished = false;    // 已完成 / 已结束
    wstring finishedText;
};

NextOccurrence ComputeNext(const Task& t, const SYSTEMTIME& now);
wstring PrettyLeft(long long seconds);    // 2 小时 15 分
wstring TodayKey(const SYSTEMTIME& st, int h, int m);

// fire reminders
void SchedulerTick(HWND mainWnd, const SYSTEMTIME& now);

// ---------------------------------------------------------------- system integration

void ShowToast(const wstring& title, const wstring& body, bool silent);
bool IsAutoStartEnabled();
bool SetAutoStartEnabled(bool on);
void OpenInExplorer(const wstring& path);

// ---------------------------------------------------------------- app state

struct EditForm
{
    wstring title;
    int kind = KIND_PERMANENT;
    int hour = 9, minute = 0;
    SYSTEMTIME startDate{};
    SYSTEMTIME endDate{};
    bool hasEnd = true;
    int totalCount = 3;
    int doneCount = 0;
    bool enabled = true;
    wstring id;
};

extern vector<Task> g_tasks;
extern int  g_page;        // 0 待办 1 编辑 2 设置
extern int  g_view;        // 0 时间线 1 分栏
extern int  g_editIndex;   // -1 = 新增
extern EditForm g_form;
extern HWND g_hMain;
extern bool g_autostart;
extern bool g_closeToTray;
extern bool g_soundOn;
extern bool g_paused;
extern bool g_blockInject;   // 兼容模式：阻止非微软签名 DLL 注入本进程

bool IsBlockInjectEnabled();
void SetBlockInjectEnabled(bool on);

// 应用内提醒横幅：系统通知被系统设置关闭时也能看见提醒
extern wstring   g_bannerTitle;
extern wstring   g_bannerBody;
extern ULONGLONG g_bannerUntil;

void ReloadData();
bool CommitForm();          // 保存 g_form 到 g_tasks
void OpenEditFor(int index); // index=-1 新增
void Repaint();
void InvalidateLayout();
