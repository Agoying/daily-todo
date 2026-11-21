// 每日待办 — 账号与局域网同步
#pragma once
#include "app.h"

// 页面内的可点击区域 id（由 account.cpp 自己分配，main.cpp 只负责转发）
enum {
    H_ACC_BASE        = 200,
    H_ACC_FIELD_EMAIL = 201,
    H_ACC_FIELD_PASS  = 202,
    H_ACC_REGISTER    = 203,
    H_ACC_LOGIN       = 204,
    H_ACC_LOGOUT      = 205,
    H_ACC_SYNCNOW     = 206,
    H_ACC_SYNCTOGGLE  = 207,
    H_ACC_SHOWPASS    = 208
};

typedef void (*HotFn)(int id, RECT r);
void AccountSetHotCallback(HotFn fn);

// 生命周期
void AccountInit(HWND mainWnd);   // 读账号/会话文件，启动网络线程
void AccountShutdown();

// 每帧（1 秒 timer）调用：处理同步队列、刷新状态
void AccountTick(HWND mainWnd);

// 页面
void AccountLayout(int w, int h);
void DrawAccountPage(Gdiplus::Graphics& g, int w, int h);
void AccountDispatch(int id);
bool AccountOnChar(wchar_t ch);   // 返回 true 表示已消费
bool AccountOnKey(int vk);

// 状态查询
bool    AccountLoggedIn();
wstring AccountEmail();
bool    AccountSyncEnabled();
wstring AccountStatusText();
int     AccountPeerCount();

// 本地删除一条待办时调用，写入墓碑，让对端同步时也删掉
void    AccountMarkDeleted(const wstring& id);

// 按识别码向局域网内的其它设备查询单条待办（只读：不合并、不改本地数据）
// 返回 true 时 out 是查到的那条待办（code 已填好）。耗时约 1 秒，勿在绘制路径调用。
bool    AccountQueryTaskByCode(const wstring& code, Task& out);
