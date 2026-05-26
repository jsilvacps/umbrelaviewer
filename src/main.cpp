/*
 * DMSViewer — Acesso Remoto
 * UI inspirada no AnyDesk: tema escuro, barra de endereço, IP grande em destaque.
 */
#include <windows.h>
#include <dwmapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objidl.h>
#include <objbase.h>
#include <gdiplus.h>
#include <urlmon.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <wtsapi32.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include "protocol.h"
#include "screencap.h"
#include "inputhandler.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wtsapi32.lib")

// ─── Versão e URLs de atualização ────────────────────────────────────────────
// Incrementar APP_VERSION_INT a cada release (ex: 1.0 = 100, 1.1 = 110, 2.0 = 200)
#define APP_VERSION_INT   100
#define APP_VERSION_STR   "1.0"
// version.txt deve conter apenas o número inteiro, ex: "110" para versão 1.1
#define UPDATE_VERSION_URL  "https://raw.githubusercontent.com/jsilvacps/umbrelaviewer/main/version.txt"
#define UPDATE_DOWNLOAD_URL "https://github.com/jsilvacps/umbrelaviewer/releases/latest/download/UmbrelaViewer.exe"

// ─── Dark title bar attribute ─────────────────────────────────────────────────
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ─── Palette (GDI+ Color) ─────────────────────────────────────────────────────
// CSS design-system: --bg:#0A0F1C --card:#111827 --primary:#2563EB
//   --primary-2:#22D3EE --success:#10B981 --warning:#F59E0B
//   --text:#E2E8F0 --muted:#94A3B8 --border:rgba(37,99,235,0.35)
namespace C {
    using Gc = Gdiplus::Color;
    const Gc BG      {255, 10, 15, 28};   // #0A0F1C
    const Gc TOOLBAR {255, 10, 16, 32};   // navbar gradient top
    const Gc TOOLBAR2{255, 15, 23, 42};   // navbar gradient bottom
    const Gc CARD    {255, 17, 24, 39};   // #111827
    const Gc CARD2   {255, 30, 41, 59};   // #1E293B
    const Gc ADDRBOX {255, 15, 23, 42};   // connect box fill (#0F172A)
    const Gc BORDER  { 90, 37, 99,235};   // rgba(37,99,235,0.35)
    const Gc BDRHI   {255, 34,211,238};   // #22D3EE – focused
    const Gc IP      {255, 34,211,238};   // #22D3EE – cyan
    const Gc IPGLOW  { 55, 34,211,238};   // IP glow layer
    const Gc PASS    {255, 52,211,153};   // #34D399 – green password
    const Gc PASSBG  { 30, 16,185,129};   // rgba(16,185,129,0.12)
    const Gc TEXT    {255,226,232,240};   // #E2E8F0
    const Gc TEXT2   {255,148,163,184};   // #94A3B8 (muted)
    const Gc BTNBG   {255, 37, 99,235};   // #2563EB
    const Gc BTNHOV  {255, 14,165,233};   // #0EA5E9
    const Gc BTNDN   {255, 30, 64,175};   // pressed
    const Gc MENUBTN {255,148,163,184};   // hamburger
    const Gc GREEN   {255, 16,185,129};   // #10B981 – success
    const Gc YELLOW  {255,245,158, 11};   // #F59E0B – warning
    const Gc SEP     { 90, 37, 99,235};   // separator = border
    const Gc ACCENT  {255, 34,211,238};   // #22D3EE
    const Gc PRIMARY2{255, 34,211,238};   // alias cyan
    const Gc ICONBG  {255, 37, 99,235};   // #2563EB icon bg
    constexpr COLORREF BG_CR    = RGB( 10, 15, 28);
    constexpr COLORREF ADDRB_CR = RGB( 15, 23, 42);
    constexpr COLORREF TEXT_CR  = RGB(226,232,240);
    constexpr COLORREF TEXT2_CR = RGB(148,163,184);
}

// ─── Layout constants ─────────────────────────────────────────────────────────
#define WIN_W     960   // wider — matches AnyDesk proportions
#define WIN_H     660
#define TOOL_H     46   // title bar
#define ADDR_Y     46   // address bar top
#define ADDR_H     50   // address bar height
#define HERO_Y     96   // "Este dispositivo" + senha section
#define HERO_H    130   // hero height
#define TAB_Y     226   // tab bar top   (HERO_Y+HERO_H)
#define TAB_H      38   // tab bar height
#define CONT_Y    264   // content below tabs  (TAB_Y+TAB_H)
#define CONT_H    364   // content height
#define FOOT_Y    628   // footer top   (CONT_Y+CONT_H)
#define FOOT_H     32   // footer height  (FOOT_Y+FOOT_H == WIN_H)

// ─── Virtual button system ────────────────────────────────────────────────────
#define VB_HAMBURGER   1
#define VB_CONNECT     2
#define VB_COPY_IP     3
#define VB_REGEN_PASS  4
#define VB_SHARE       5   // "Compartilhar" button
#define VB_TAB_HOME    6
#define VB_TAB_RECENT  7
#define VB_SESSION_0   10  // recent-session cards: 10..17 (up to 8)
#define VB_INSTALL_NOW  9  // "Instalar para todos os usuários" card button
#define VB_SVC_TOGGLE  18  // service card: install / stop service

struct VBtn { RECT rc; int id; };
static VBtn  g_btns[24] = {};
static int   g_nbtns    = 0;
static int   g_hotBtn  = 0;
static int   g_dnBtn   = 0;
static bool  g_tracking = false;

static void VBClear() { g_nbtns = 0; }
static void VBAdd(int id, int x, int y, int w, int h) {
    if (g_nbtns < 24) g_btns[g_nbtns++] = {{x,y,x+w,y+h}, id};
}
static int VBHitTest(int mx, int my) {
    for (int i = 0; i < g_nbtns; i++)
        if (PtInRect(&g_btns[i].rc, {mx,my})) return g_btns[i].id;
    return 0;
}

// ─── GDI+ ─────────────────────────────────────────────────────────────────────
static Gdiplus::GdiplusStartupInput g_gdip;
static ULONG_PTR g_gdipToken = 0;

static void FillRR(Gdiplus::Graphics& g, float x, float y, float w, float h,
                   float r, const Gdiplus::Brush& b) {
    Gdiplus::GraphicsPath p;
    p.AddArc(x,     y,     r*2,r*2, 180, 90);
    p.AddArc(x+w-r*2, y,   r*2,r*2, 270, 90);
    p.AddArc(x+w-r*2, y+h-r*2, r*2,r*2, 0, 90);
    p.AddArc(x,     y+h-r*2, r*2,r*2,  90, 90);
    p.CloseFigure();
    g.FillPath(&b, &p);
}

static void StrokeRR(Gdiplus::Graphics& g, float x, float y, float w, float h,
                     float r, const Gdiplus::Pen& pen) {
    Gdiplus::GraphicsPath p;
    p.AddArc(x,     y,     r*2,r*2, 180, 90);
    p.AddArc(x+w-r*2, y,   r*2,r*2, 270, 90);
    p.AddArc(x+w-r*2, y+h-r*2, r*2,r*2, 0, 90);
    p.AddArc(x,     y+h-r*2, r*2,r*2,  90, 90);
    p.CloseFigure();
    g.DrawPath(&pen, &p);
}

// Draw three-lines hamburger (≡)
static void DrawHamburger(Gdiplus::Graphics& g, int cx, int cy, bool hot) {
    Gdiplus::Color col = hot ? Gdiplus::Color(255,255,255,255) : Gdiplus::Color(200,180,190,220);
    Gdiplus::Pen pen(col, 2.0f);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    int half = 10;
    g.DrawLine(&pen, (float)(cx-half),(float)(cy-8),(float)(cx+half),(float)(cy-8));
    g.DrawLine(&pen, (float)(cx-half),(float)cy,    (float)(cx+half),(float)cy);
    g.DrawLine(&pen, (float)(cx-half),(float)(cy+8),(float)(cx+half),(float)(cy+8));
}

// ─── Shared icon drawing (UV monitor + speed lines + neon glow) ──────────────
// withCircle=true  → draws inside a circular badge (standalone .ico)
// withCircle=false → floats directly on background (toolbar badge)
static void DrawIconContent(Gdiplus::Graphics& g, float x, float y, float sz, bool withCircle) {
    float cx = x + sz * 0.5f, cy = y + sz * 0.5f;

    // ── Optional circular badge ───────────────────────────────────────────
    if (withCircle) {
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 5, 8, 22));
        g.FillEllipse(&bg, x, y, sz, sz);
        // Outer soft glow
        Gdiplus::Pen glow(Gdiplus::Color(48, 0, 100, 255), sz * 0.13f);
        g.DrawEllipse(&glow, x + sz*0.03f, y + sz*0.03f, sz*0.94f, sz*0.94f);
        // Bright cyan ring
        Gdiplus::Pen ring(Gdiplus::Color(185, 0, 207, 255), sz * 0.044f);
        g.DrawEllipse(&ring, x + sz*0.075f, y + sz*0.075f, sz*0.85f, sz*0.85f);
    }

    // ── Monitor dimensions ────────────────────────────────────────────────
    // Shift monitor slightly right to leave room for speed-lines on the left
    float mw = withCircle ? sz * 0.58f : sz * 0.63f;
    float mh = mw * 0.67f;
    float mx = cx - mw * 0.5f + sz * (withCircle ? 0.04f : 0.06f);
    float my = cy - mh * 0.5f + sz * (withCircle ? -0.04f : -0.01f);

    // Neon glow halo around monitor
    Gdiplus::Pen monGlow(Gdiplus::Color(50, 0, 207, 255), sz * 0.060f);
    StrokeRR(g, mx - sz*0.024f, my - sz*0.024f,
               mw + sz*0.048f, mh + sz*0.048f, sz*0.07f, monGlow);

    // Monitor fill – very dark navy (just visible against background)
    Gdiplus::SolidBrush monFill(Gdiplus::Color(255, 7, 12, 42));
    FillRR(g, mx, my, mw, mh, sz * 0.052f, monFill);

    // Monitor border – gradient top #00CFFF → bottom #007BFF
    {
        Gdiplus::LinearGradientBrush borderBr(
            Gdiplus::PointF(mx, my), Gdiplus::PointF(mx, my + mh),
            Gdiplus::Color(215, 0, 207, 255),
            Gdiplus::Color(175, 0, 123, 255));
        Gdiplus::Pen borderPen(&borderBr, sz * 0.030f);
        StrokeRR(g, mx, my, mw, mh, sz * 0.052f, borderPen);
    }

    // ── Monitor stand ─────────────────────────────────────────────────────
    if (sz >= 20) {
        float nkW = mw * 0.22f, nkH = sz * 0.058f;
        float bsW = mw * 0.46f, bsH = sz * 0.036f;
        Gdiplus::SolidBrush nkBr(Gdiplus::Color(145, 0, 85, 205));
        Gdiplus::SolidBrush bsBr(Gdiplus::Color(125, 0, 65, 175));
        FillRR(g, cx - nkW*0.5f, my+mh,      nkW, nkH, sz*0.010f, nkBr);
        FillRR(g, cx - bsW*0.5f, my+mh+nkH,  bsW, bsH, sz*0.010f, bsBr);
    }

    // ── Three dots inside monitor – top-right corner ───────────────────────
    if (sz >= 22) {
        float dr = sz * 0.024f;
        float dy = my + sz * 0.060f;
        float dx = mx + mw - sz * 0.068f;
        Gdiplus::SolidBrush dotBr(Gdiplus::Color(205, 0, 229, 255));
        for (int i = 0; i < 3; i++)
            g.FillEllipse(&dotBr, dx - i*dr*2.55f - dr, dy - dr, dr*2.f, dr*2.f);
    }

    // ── Speed / motion lines – left of monitor ────────────────────────────
    if (sz >= 24) {
        float lx2 = mx - sz * 0.055f;   // right anchor (just left of monitor)
        struct { float len; float yOff; BYTE alpha; } lines[] = {
            { sz*0.130f, mh*0.26f, 140 },   // top line (shorter)
            { sz*0.200f, mh*0.50f, 200 },   // middle line (longest)
            { sz*0.130f, mh*0.74f, 110 },   // bottom line (shorter)
        };
        for (auto& l : lines) {
            Gdiplus::Pen lp(Gdiplus::Color(l.alpha, 0, 207, 255), sz * 0.022f);
            lp.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
            float ly = my + l.yOff;
            g.DrawLine(&lp, lx2 - l.len, ly, lx2, ly);
            // Bright dot at the tip of each line
            Gdiplus::SolidBrush dp(Gdiplus::Color((BYTE)std::min(255,l.alpha+55), 0, 229, 255));
            float dr2 = sz * 0.019f;
            g.FillEllipse(&dp, lx2 - l.len - dr2, ly - dr2, dr2*2.f, dr2*2.f);
        }
    }

    // ── "UV" monogram inside monitor ──────────────────────────────────────
    if (sz >= 30) {
        Gdiplus::FontFamily ff(L"Segoe UI");
        float uSz = mh * 0.56f;
        Gdiplus::Font fntU(&ff, uSz,        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fntV(&ff, uSz * 0.86f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::RectF uMsr, vMsr;
        g.MeasureString(L"U", -1, &fntU, Gdiplus::PointF(0,0), &uMsr);
        g.MeasureString(L"V", -1, &fntV, Gdiplus::PointF(0,0), &vMsr);
        // Centre text in monitor, slightly right of monitor-centre
        float totalW = uMsr.Width * 0.82f + vMsr.Width * 0.80f;
        float tx = (mx + mw * 0.5f) - totalW * 0.5f + sz * 0.02f;
        float ty = my + (mh - uMsr.Height) * 0.46f;
        // "U" – neon cyan  #00E5FF
        Gdiplus::SolidBrush uBr(Gdiplus::Color(255, 0, 229, 255));
        g.DrawString(L"U", -1, &fntU, Gdiplus::PointF(tx, ty), &uBr);
        // "V" – metallic white  #F2F5F7, slightly overlapping U and offset down
        Gdiplus::SolidBrush vBr(Gdiplus::Color(240, 242, 245, 247));
        g.DrawString(L"V", -1, &fntV,
            Gdiplus::PointF(tx + uMsr.Width * 0.74f, ty + sz * 0.014f), &vBr);
    }
}

// Toolbar badge – no circular background, floats on dark gradient
static void DrawUmbrelaIcon(Gdiplus::Graphics& g, float x, float y, float sz) {
    DrawIconContent(g, x, y, sz, false);
}

// Draw lock icon
static void DrawLock(Gdiplus::Graphics& g, float x, float y, float sz, Gdiplus::Color col) {
    Gdiplus::Pen p(col, 1.5f);
    Gdiplus::SolidBrush b(col);
    // shackle arc
    Gdiplus::RectF arc(x+sz*0.2f, y, sz*0.6f, sz*0.55f);
    g.DrawArc(&p, arc, 180, 180);
    // body
    FillRR(g, x, y+sz*0.4f, sz, sz*0.55f, 2.0f, b);
    // keyhole
    Gdiplus::SolidBrush kb(Gdiplus::Color(200,25,30,50));
    g.FillEllipse(&kb, x+sz*0.35f, y+sz*0.52f, sz*0.3f, sz*0.22f);
}

static void DrawCircle(Gdiplus::Graphics& g, float cx, float cy, float r, Gdiplus::Color col) {
    Gdiplus::SolidBrush b(col);
    g.FillEllipse(&b, cx-r, cy-r, r*2, r*2);
}

// ─── Host / viewer state ──────────────────────────────────────────────────────
static SOCKET            g_srvSock      = INVALID_SOCKET;
static SOCKET            g_cliSock      = INVALID_SOCKET;
static std::atomic<bool> g_srvRunning   { false };
static std::atomic<bool> g_srvConnected { false };
static std::mutex        g_srvSendMtx;

static SOCKET            g_viewSock     = INVALID_SOCKET;
static std::atomic<bool> g_viewRunning  { false };
static std::atomic<bool> g_viewConnected{ false };
static std::mutex        g_viewSendMtx;
static int               g_remW=1920, g_remH=1080;

static std::mutex           g_frameMtx;
static std::vector<uint8_t> g_frameData;

static std::string g_sessionPass;
static std::string g_defaultPass;
static char        g_localIP[64] = "Obtendo...";

// ─── Service globals ──────────────────────────────────────────────────────────
static bool                  g_runningAsService = false;
static SERVICE_STATUS        g_svcStatus        = {};
static SERVICE_STATUS_HANDLE g_svcHandle        = nullptr;
static HANDLE                g_svcStopEvt       = nullptr;
static constexpr char        SVC_NAME[]         = "UmbrelaViewer";
static constexpr wchar_t     SVC_NAMEW[]        = L"UmbrelaViewer";
static constexpr wchar_t     SVC_DISPLAYW[]     = L"Umbrela Viewer Remote Access";
static constexpr wchar_t     SVC_DESCW[]        =
    L"Provides remote desktop access including UAC secure desktop capture.";

// ─── Recent sessions ──────────────────────────────────────────────────────────
struct RecentSession { std::string ip, name; time_t lastConn = 0; };
static std::vector<RecentSession> g_recentSessions;
static int         g_activeTab     = 0;    // 0=Início  1=Sessões Recentes
static std::string g_connectingIP;          // IP being dialled (saved on WM_CONNECT_OK)
// Access-request synchronization (host side — empty-password connection)
static HANDLE              g_accessEvent    = nullptr; // manual-reset event
static std::atomic<bool>   g_accessDecision { false };  // true = aceitar
static int         g_hostStatus  = 0; // 0=idle 1=waiting 2=connected

// ── Mini-dialog helpers ───────────────────────────────────────────────────────
static HWND g_activeDlgEdit  = nullptr;
static char g_dlgPassBuf[65] = {};
static int  g_dlgResultCode  = IDCANCEL;
static bool g_dlgDone        = false;

// ─── Window handles ───────────────────────────────────────────────────────────
static HWND g_mainWnd    = nullptr;
static HWND g_viewerWnd  = nullptr;
static HWND g_viewPanel  = nullptr;
static HWND g_vChatLog   = nullptr;
static HWND g_vChatIn    = nullptr;
static HWND g_vStatus    = nullptr;
static HWND g_editRemIP  = nullptr;  // address bar edit control
static HWND g_btnConnect = nullptr;
static NOTIFYICONDATAA g_nid = {};
static HINSTANCE g_hInst = nullptr;

// ─── Custom messages ──────────────────────────────────────────────────────────
#define WM_UPDATE_AVAILABLE     (WM_USER+20)
#define WM_CONNECT_OK           (WM_USER+31)
#define WM_CONNECT_FAIL         (WM_USER+32)
#define WM_HOST_STATUS          (WM_USER+10)
#define WM_HOST_ACCESS_REQUEST  (WM_USER+40)  // host: incoming access request (no password)
#define WM_VIEW_FRAME        (WM_USER+11)
#define WM_VIEW_CONNECTED    (WM_USER+12)
#define WM_VIEW_DISCONNECTED (WM_USER+13)
#define WM_VIEW_CHAT         (WM_USER+14)

// Tray & menu IDs
#define WM_TRAYICON       (WM_USER+1)
#define ID_TRAY           1
#define IDM_REGEN         301
#define IDM_SETDEFAULT    302
#define IDM_CLEARDEFAULT  303
#define IDM_ABOUT         304
#define IDM_SHOW          305
#define IDM_EXIT          306

// ─── Config ───────────────────────────────────────────────────────────────────
static std::string GetConfigPath() {
    char buf[MAX_PATH];
    SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf);
    return std::string(buf) + "\\UmbrelaViewer\\config.ini";
}
static void LoadConfig() {
    std::ifstream f(GetConfigPath()); std::string ln;
    while (std::getline(f,ln)) {
        while(!ln.empty()&&(ln.back()=='\r'||ln.back()=='\n')) ln.pop_back();
        if (ln.rfind("default_password=",0)==0) {
            g_defaultPass=ln.substr(17);
            while(!g_defaultPass.empty()&&(g_defaultPass.back()=='\r'||g_defaultPass.back()==' '))
                g_defaultPass.pop_back();
        } else if (ln.rfind("recent=",0)==0) {
            std::string v=ln.substr(7);
            auto p1=v.find('|'), p2=v.rfind('|');
            if(p1!=std::string::npos && p2!=p1) {
                RecentSession rs;
                rs.ip  =v.substr(0,p1);
                rs.name=v.substr(p1+1,p2-p1-1);
                try { rs.lastConn=(time_t)std::stoll(v.substr(p2+1)); } catch(...) {}
                if(!rs.ip.empty()) g_recentSessions.push_back(rs);
            }
        }
    }
}
static void SaveConfig() {
    auto path = GetConfigPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if(!f) return;
    if(!g_defaultPass.empty()) f<<"default_password="<<g_defaultPass<<"\n";
    for(auto& s : g_recentSessions)
        f<<"recent="<<s.ip<<"|"<<s.name<<"|"<<(long long)s.lastConn<<"\n";
}

// ─── Password ────────────────────────────────────────────────────────────────
static std::string GeneratePassword() {
    const char pool[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string p(6,' ');
    srand((unsigned)time(nullptr)^(unsigned)(uintptr_t)&p);
    for (auto& c:p) c=pool[rand()%(sizeof(pool)-1)];
    return p;
}

// ─── Network helpers ──────────────────────────────────────────────────────────
static void GetLocalIP() {
    char host[256]; gethostname(host,sizeof(host));
    addrinfo hints={},*res=nullptr; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(host,nullptr,&hints,&res)==0){
        for(auto* p=res;p;p=p->ai_next){
            inet_ntop(AF_INET,&((sockaddr_in*)p->ai_addr)->sin_addr,g_localIP,sizeof(g_localIP));
            break;
        }
        freeaddrinfo(res);
    }
}

static bool SrvSend(PacketType t,const void* d,uint32_t sz){
    if(g_cliSock==INVALID_SOCKET) return false;
    PacketHeader h={t,sz};
    std::lock_guard<std::mutex> lk(g_srvSendMtx);
    if(send(g_cliSock,(char*)&h,sizeof(h),0)!=sizeof(h)) return false;
    if(sz&&d){uint32_t s=0;while(s<sz){int n=send(g_cliSock,(char*)d+s,sz-s,0);if(n<=0)return false;s+=n;}}
    return true;
}
static bool ViewSend(PacketType t,const void* d,uint32_t sz){
    if(g_viewSock==INVALID_SOCKET) return false;
    PacketHeader h={t,sz};
    std::lock_guard<std::mutex> lk(g_viewSendMtx);
    if(send(g_viewSock,(char*)&h,sizeof(h),0)!=sizeof(h)) return false;
    if(sz&&d){uint32_t s=0;while(s<sz){int n=send(g_viewSock,(char*)d+s,sz-s,0);if(n<=0)return false;s+=n;}}
    return true;
}
static bool RecvAll(SOCKET sock,void* buf,uint32_t sz){
    uint32_t g=0;
    while(g<sz){int n=recv(sock,(char*)buf+g,sz-g,0);if(n<=0)return false;g+=n;}
    return true;
}

static void AppendChat(const char* who,const char* text){
    if(!g_vChatLog) return;
    int len=GetWindowTextLengthA(g_vChatLog);
    SendMessage(g_vChatLog,EM_SETSEL,len,len);
    std::string line=std::string(who)+": "+text+"\r\n";
    SendMessageA(g_vChatLog,EM_REPLACESEL,FALSE,(LPARAM)line.c_str());
    SendMessage(g_vChatLog,WM_VSCROLL,SB_BOTTOM,0);
}

// ─── Host logic ───────────────────────────────────────────────────────────────
static void HostStreamThread(){
    ScreenCapture cap;
    cap.SetServiceMode(g_runningAsService);
    if(!cap.Init()) return;
    ScreenInfoData si={(uint32_t)cap.GetWidth(),(uint32_t)cap.GetHeight()};
    SrvSend(PacketType::SCREEN_INFO,&si,sizeof(si));
    std::vector<uint8_t> frame;
    while(g_srvConnected&&g_srvRunning){
        if(cap.CaptureToJPEG(frame,90))
            if(!SrvSend(PacketType::SCREEN_FRAME,frame.data(),(uint32_t)frame.size())) break;
        Sleep(33);
    }
}

static void HostClientThread(SOCKET sock){
    PacketHeader hdr; ConnectRequestData crd={};
    if(!RecvAll(sock,&hdr,sizeof(hdr))||hdr.type!=PacketType::CONNECT_REQUEST
       ||hdr.dataSize!=sizeof(crd)||!RecvAll(sock,&crd,sizeof(crd))){
        closesocket(sock); return;
    }
    bool emptyPass = (crd.password[0] == '\0');
    if(emptyPass){
        bool allowed = false;
        if(g_runningAsService){
            // ── Modo serviço: envia diálogo para a sessão interativa via WTS ──
            DWORD sessionId = WTSGetActiveConsoleSessionId();
            static const wchar_t* title =
                L"Umbrela Viewer — Solicitação de Acesso";
            static const wchar_t* text  =
                L"Um técnico solicita acesso remoto a este computador.\n\n"
                L"Deseja permitir a conexão?\n\n"
                L"Se não reconhece esta solicitação, clique em NÃO.";
            DWORD response = 0;
            BOOL ok = WTSSendMessageW(
                WTS_CURRENT_SERVER_HANDLE, sessionId,
                (LPWSTR)title, (DWORD)(wcslen(title)*sizeof(wchar_t)),
                (LPWSTR)text,  (DWORD)(wcslen(text) *sizeof(wchar_t)),
                MB_YESNO|MB_ICONQUESTION|MB_TOPMOST|MB_DEFBUTTON2,
                30, &response, TRUE);
            allowed = (ok && response == IDYES);
        } else {
            // ── Modo normal: pede ao usuário via janela principal ──
            if(g_accessEvent) {
                ResetEvent(g_accessEvent);
                g_accessDecision = false;
                PostMessage(g_mainWnd, WM_HOST_ACCESS_REQUEST, 0, 0);
                DWORD r = WaitForSingleObject(g_accessEvent, 30000);
                allowed = (r == WAIT_OBJECT_0 && g_accessDecision.load());
            }
        }
        if(!allowed){
            PacketHeader deny={PacketType::CONNECT_DENY,0};
            send(sock,(char*)&deny,sizeof(deny),0);
            closesocket(sock); return;
        }
    } else {
        bool passOk = (g_sessionPass==crd.password) ||
                      (!g_defaultPass.empty() && g_defaultPass==crd.password);
        if(!passOk){
            PacketHeader deny={PacketType::CONNECT_DENY,0};
            send(sock,(char*)&deny,sizeof(deny),0); closesocket(sock); return;
        }
    }
    PacketHeader ok={PacketType::CONNECT_ACCEPT,0};
    send(sock,(char*)&ok,sizeof(ok),0);
    g_cliSock=sock; g_srvConnected=true;
    PostMessage(g_mainWnd,WM_HOST_STATUS,2,0);
    std::thread(HostStreamThread).detach();

    std::vector<uint8_t> buf;
    while(g_srvConnected&&g_srvRunning){
        if(!RecvAll(sock,&hdr,sizeof(hdr))) break;
        if(hdr.dataSize>MAX_PACKET_SIZE) break;
        if(hdr.dataSize>0){buf.resize(hdr.dataSize);if(!RecvAll(sock,buf.data(),hdr.dataSize)) break;}
        switch(hdr.type){
        case PacketType::MOUSE_MOVE:   {auto*d=(MouseMoveData*)buf.data();InputMouseMove(d->x,d->y);break;}
        case PacketType::MOUSE_BUTTON: {auto*d=(MouseButtonData*)buf.data();InputMouseButton(d->button,d->pressed!=0);break;}
        case PacketType::MOUSE_WHEEL:  {auto*d=(MouseWheelData*)buf.data();InputMouseWheel(d->delta);break;}
        case PacketType::KEYBOARD_EVENT:{auto*d=(KeyEventData*)buf.data();InputKey(d->vkCode,d->pressed!=0);break;}
        case PacketType::CHAT_MESSAGE:
            if(hdr.dataSize>0&&hdr.dataSize<2048){
                std::string msg(buf.begin(),buf.begin()+hdr.dataSize);
                MessageBoxA(nullptr,msg.c_str(),"Chat — Umbrela Viewer",MB_OK|MB_ICONINFORMATION|MB_TOPMOST);
            }
            break;
        case PacketType::FILE_SEND_START:{
            auto*fsd=(FileSendStartData*)buf.data();
            char desk[MAX_PATH];SHGetFolderPathA(nullptr,CSIDL_DESKTOPDIRECTORY,nullptr,0,desk);
            std::string dst=std::string(desk)+"\\"+fsd->filename;
            std::ofstream f(dst,std::ios::binary);
            uint64_t rem=fsd->fileSize; std::vector<uint8_t> chunk;
            while(rem>0){
                PacketHeader fh;if(!RecvAll(sock,&fh,sizeof(fh))) goto done;
                if(fh.type==PacketType::FILE_SEND_END) break;
                if(fh.type!=PacketType::FILE_SEND_DATA||!fh.dataSize) goto done;
                chunk.resize(fh.dataSize);if(!RecvAll(sock,chunk.data(),fh.dataSize)) goto done;
                f.write((char*)chunk.data(),fh.dataSize);rem-=fh.dataSize;
            }
            MessageBoxA(nullptr,dst.c_str(),"Arquivo recebido (Desktop)",MB_OK|MB_ICONINFORMATION);
            break;
        }
        case PacketType::DISCONNECT: goto done;
        case PacketType::PING: SrvSend(PacketType::PONG,nullptr,0); break;
        default: break;
        }
    }
done:
    g_srvConnected=false; g_cliSock=INVALID_SOCKET; closesocket(sock);
    PostMessage(g_mainWnd,WM_HOST_STATUS,1,0);
}

static void HostAcceptThread(){
    while(g_srvRunning){
        sockaddr_in addr={}; int len=sizeof(addr);
        SOCKET c=accept(g_srvSock,(sockaddr*)&addr,&len);
        if(c==INVALID_SOCKET){if(g_srvRunning)Sleep(50);continue;}
        if(g_srvConnected){
            PacketHeader busy={PacketType::CONNECT_BUSY,0};
            send(c,(char*)&busy,sizeof(busy),0);closesocket(c);continue;
        }
        std::thread(HostClientThread,c).detach();
    }
}

static bool StartHosting(){
    g_srvSock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(g_srvSock==INVALID_SOCKET) return false;
    sockaddr_in srv={}; srv.sin_family=AF_INET; srv.sin_addr.s_addr=INADDR_ANY;
    srv.sin_port=htons(DEFAULT_PORT);
    int opt=1; setsockopt(g_srvSock,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
    if(bind(g_srvSock,(sockaddr*)&srv,sizeof(srv))!=0||listen(g_srvSock,1)!=0){
        closesocket(g_srvSock);g_srvSock=INVALID_SOCKET;return false;
    }
    g_srvRunning=true;
    std::thread(HostAcceptThread).detach();
    return true;
}
static void StopHosting(){
    g_srvRunning=g_srvConnected=false;
    if(g_cliSock!=INVALID_SOCKET){SrvSend(PacketType::DISCONNECT,nullptr,0);closesocket(g_cliSock);g_cliSock=INVALID_SOCKET;}
    if(g_srvSock!=INVALID_SOCKET){closesocket(g_srvSock);g_srvSock=INVALID_SOCKET;}
}

// ─── Viewer: receive thread ───────────────────────────────────────────────────
static void ViewRecvThread(){
    PacketHeader hdr; std::vector<uint8_t> buf;
    while(g_viewRunning){
        if(!RecvAll(g_viewSock,&hdr,sizeof(hdr))) break;
        if(hdr.dataSize>MAX_PACKET_SIZE) break;
        if(hdr.dataSize>0){buf.resize(hdr.dataSize);if(!RecvAll(g_viewSock,buf.data(),hdr.dataSize)) break;}
        switch(hdr.type){
        case PacketType::SCREEN_INFO:
            if(hdr.dataSize>=sizeof(ScreenInfoData)){auto*s=(ScreenInfoData*)buf.data();g_remW=(int)s->width;g_remH=(int)s->height;}
            break;
        case PacketType::SCREEN_FRAME:
            if(hdr.dataSize>0){
                {std::lock_guard<std::mutex> lk(g_frameMtx);g_frameData=buf;}
                if(g_viewPanel) PostMessage(g_viewPanel,WM_VIEW_FRAME,0,0);
            }
            break;
        case PacketType::CHAT_MESSAGE:
            if(hdr.dataSize>0&&hdr.dataSize<2048){
                auto*m=new std::string(buf.begin(),buf.begin()+hdr.dataSize);
                if(g_viewerWnd) PostMessage(g_viewerWnd,WM_VIEW_CHAT,0,(LPARAM)m);
            }
            break;
        case PacketType::DISCONNECT: goto vdone;
        default: break;
        }
    }
vdone:
    g_viewRunning=g_viewConnected=false;
    closesocket(g_viewSock);g_viewSock=INVALID_SOCKET;
    if(g_viewerWnd) PostMessage(g_viewerWnd,WM_VIEW_DISCONNECTED,0,0);
}

// ─── Viewer panel ─────────────────────────────────────────────────────────────
static LRESULT CALLBACK ViewPanelProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hw,&ps);
        RECT rc; GetClientRect(hw,&rc); int cw=rc.right,ch=rc.bottom;
        if(!g_viewConnected){
            FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
            SetTextColor(hdc,RGB(80,80,80));SetBkMode(hdc,TRANSPARENT);
            DrawTextA(hdc,"Aguardando conexao...",-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            EndPaint(hw,&ps);return 0;
        }
        std::vector<uint8_t> frame;{std::lock_guard<std::mutex> lk(g_frameMtx);frame=g_frameData;}
        if(!frame.empty()){
            HGLOBAL hg=GlobalAlloc(GHND,frame.size());
            if(hg){memcpy(GlobalLock(hg),frame.data(),frame.size());GlobalUnlock(hg);
                IStream*s=nullptr;
                if(SUCCEEDED(CreateStreamOnHGlobal(hg,TRUE,&s))){
                    Gdiplus::Bitmap*bmp=Gdiplus::Bitmap::FromStream(s);s->Release();
                    if(bmp&&bmp->GetLastStatus()==Gdiplus::Ok){
                        Gdiplus::Graphics g(hdc);
                        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                        float sx=(float)cw/bmp->GetWidth(),sy=(float)ch/bmp->GetHeight(),sc=sx<sy?sx:sy;
                        int dw=(int)(bmp->GetWidth()*sc),dh=(int)(bmp->GetHeight()*sc);
                        FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
                        g.DrawImage(bmp,(cw-dw)/2,(ch-dh)/2,dw,dh);
                    }delete bmp;
                }
            }
        } else FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hw,&ps);return 0;
    }
    if(msg==WM_VIEW_FRAME){InvalidateRect(hw,nullptr,FALSE);return 0;}
    // Mouse/keyboard forwarding
    if(msg==WM_MOUSEMOVE&&g_viewConnected){
        RECT rc;GetClientRect(hw,&rc);int cw=rc.right,ch=rc.bottom,mx=LOWORD(lp),my=HIWORD(lp);
        float sx=(float)cw/g_remW,sy=(float)ch/g_remH,sc=sx<sy?sx:sy;
        int dw=(int)(g_remW*sc),dh=(int)(g_remH*sc),dx=(cw-dw)/2,dy=(ch-dh)/2;
        float rx=(float)(mx-dx)/dw,ry=(float)(my-dy)/dh;
        if(rx<0)rx=0;if(rx>1)rx=1;if(ry<0)ry=0;if(ry>1)ry=1;
        MouseMoveData d={(int32_t)(rx*10000),(int32_t)(ry*10000)};
        ViewSend(PacketType::MOUSE_MOVE,&d,sizeof(d));return 0;
    }
    if((msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP||msg==WM_RBUTTONDOWN||msg==WM_RBUTTONUP||msg==WM_MBUTTONDOWN||msg==WM_MBUTTONUP)&&g_viewConnected){
        uint32_t btn=(msg==WM_RBUTTONDOWN||msg==WM_RBUTTONUP)?1:(msg==WM_MBUTTONDOWN||msg==WM_MBUTTONUP)?2:0;
        uint32_t dn=(msg==WM_LBUTTONDOWN||msg==WM_RBUTTONDOWN||msg==WM_MBUTTONDOWN)?1:0;
        MouseButtonData d={btn,dn};ViewSend(PacketType::MOUSE_BUTTON,&d,sizeof(d));
        if(dn)SetCapture(hw);else ReleaseCapture();return 0;
    }
    if(msg==WM_MOUSEWHEEL&&g_viewConnected){
        MouseWheelData d={GET_WHEEL_DELTA_WPARAM(wp)};ViewSend(PacketType::MOUSE_WHEEL,&d,sizeof(d));return 0;
    }
    if((msg==WM_KEYDOWN||msg==WM_KEYUP||msg==WM_SYSKEYDOWN||msg==WM_SYSKEYUP)&&g_viewConnected){
        KeyEventData d={(uint32_t)wp,(msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN)?1u:0u};
        ViewSend(PacketType::KEYBOARD_EVENT,&d,sizeof(d));return 0;
    }
    return DefWindowProcA(hw,msg,wp,lp);
}

// ─── Viewer window ────────────────────────────────────────────────────────────
#define VTH 36
#define VCW 220
static void DoViewerLayout(HWND hw){
    RECT rc;GetClientRect(hw,&rc);int W=rc.right,H=rc.bottom,sh=20,ch=H-VTH-sh,vw=W-VCW;
    int x=5,y=7;
    SetWindowPos(GetDlgItem(hw,401),nullptr,x,y,110,22,SWP_NOZORDER);x+=115;
    SetWindowPos(GetDlgItem(hw,402),nullptr,x,y,120,22,SWP_NOZORDER);
    SetWindowPos(g_viewPanel,nullptr,0,VTH,vw,ch,SWP_NOZORDER);
    SetWindowPos(g_vChatLog,nullptr,vw,VTH,VCW,ch-50,SWP_NOZORDER);
    SetWindowPos(g_vChatIn,nullptr,vw,VTH+ch-50,VCW-46,50,SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hw,403),nullptr,vw+VCW-44,VTH+ch-50,44,50,SWP_NOZORDER);
    SetWindowPos(g_vStatus,nullptr,0,H-sh,W,sh,SWP_NOZORDER);
}

static LRESULT CALLBACK ViewerWndProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_SIZE: DoViewerLayout(hw); return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)){
        case 401: // Disconnect
            ViewSend(PacketType::DISCONNECT,nullptr,0);
            g_viewRunning=g_viewConnected=false;
            if(g_viewSock!=INVALID_SOCKET){closesocket(g_viewSock);g_viewSock=INVALID_SOCKET;}
            PostMessage(hw,WM_VIEW_DISCONNECTED,0,0); break;
        case 402: { // Send file
            if(!g_viewConnected) break;
            OPENFILENAMEA ofn={};char path[MAX_PATH]={};
            ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=hw;ofn.lpstrFile=path;ofn.nMaxFile=MAX_PATH;
            ofn.lpstrTitle="Enviar arquivo ao host";ofn.Flags=OFN_FILEMUSTEXIST;
            if(!GetOpenFileNameA(&ofn)) break;
            std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f) break;
            uint64_t fsz=f.tellg();f.seekg(0);
            char*fn=strrchr(path,'\\');fn=fn?fn+1:path;
            FileSendStartData fsd={};fsd.fileSize=fsz;strncpy_s(fsd.filename,fn,255);
            ViewSend(PacketType::FILE_SEND_START,&fsd,sizeof(fsd));
            std::vector<uint8_t> chunk(65536);
            while(!f.eof()){f.read((char*)chunk.data(),chunk.size());int n=(int)f.gcount();if(n<=0)break;
                PacketHeader h={PacketType::FILE_SEND_DATA,(uint32_t)n};
                std::lock_guard<std::mutex> lk(g_viewSendMtx);
                send(g_viewSock,(char*)&h,sizeof(h),0);send(g_viewSock,(char*)chunk.data(),n,0);}
            ViewSend(PacketType::FILE_SEND_END,nullptr,0);
            MessageBoxA(hw,"Arquivo enviado!","Umbrela Viewer",MB_OK|MB_ICONINFORMATION); break;
        }
        case 403: { // Chat send
            char buf[512]={};GetWindowTextA(g_vChatIn,buf,sizeof(buf));
            if(buf[0]&&g_viewConnected){ViewSend(PacketType::CHAT_MESSAGE,buf,(uint32_t)strlen(buf));AppendChat("Eu",buf);SetWindowTextA(g_vChatIn,"");}
            SetFocus(g_vChatIn);break;
        }
        } return 0;
    case WM_VIEW_CONNECTED:
        SetWindowTextA(g_vStatus,"Conectado");EnableWindow(GetDlgItem(hw,402),TRUE);SetFocus(g_viewPanel);return 0;
    case WM_VIEW_DISCONNECTED:{
        SetWindowTextA(g_vStatus,"Desconectado");EnableWindow(GetDlgItem(hw,402),FALSE);
        {std::lock_guard<std::mutex> lk(g_frameMtx);g_frameData.clear();}
        InvalidateRect(g_viewPanel,nullptr,TRUE);
        if(g_mainWnd)EnableWindow(g_btnConnect,TRUE);
        DestroyWindow(hw);return 0;
    }
    case WM_VIEW_CHAT:{auto*m=(std::string*)lp;AppendChat("Host",m->c_str());delete m;return 0;}
    case WM_DESTROY:
        g_viewerWnd=nullptr;g_viewPanel=nullptr;g_vChatLog=nullptr;g_vChatIn=nullptr;g_vStatus=nullptr;return 0;
    }
    return DefWindowProcA(hw,msg,wp,lp);
}

static void CreateViewerWindow(){
    g_viewerWnd=CreateWindowA("ARViewerWnd","Umbrela Viewer",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1250,750,nullptr,nullptr,g_hInst,nullptr);
    SetWindowTextW(g_viewerWnd,L"Umbrela Viewer — Sessão Remota");
    BOOL dark=TRUE;DwmSetWindowAttribute(g_viewerWnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    g_viewPanel=CreateWindowA("ARViewPanel","",WS_CHILD|WS_VISIBLE,0,VTH,900,600,g_viewerWnd,(HMENU)400,g_hInst,nullptr);
    g_vChatLog=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
        0,0,1,1,g_viewerWnd,(HMENU)404,g_hInst,nullptr);
    g_vChatIn=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,
        0,0,1,1,g_viewerWnd,(HMENU)405,g_hInst,nullptr);
    SendMessageW(g_vChatIn,EM_SETCUEBANNER,0,(LPARAM)L"Mensagem...");
    CreateWindowA("BUTTON",">>",WS_CHILD|WS_VISIBLE,0,0,1,1,g_viewerWnd,(HMENU)403,g_hInst,nullptr);
    CreateWindowA("BUTTON","Desconectar",WS_CHILD|WS_VISIBLE,0,0,1,1,g_viewerWnd,(HMENU)401,g_hInst,nullptr);
    HWND bf=CreateWindowA("BUTTON","Enviar Arquivo",WS_CHILD|WS_VISIBLE,0,0,1,1,g_viewerWnd,(HMENU)402,g_hInst,nullptr);
    EnableWindow(bf,FALSE);
    g_vStatus=CreateWindowA(STATUSCLASSNAME,"Conectando...",WS_CHILD|WS_VISIBLE,0,0,0,0,g_viewerWnd,nullptr,g_hInst,nullptr);
    DoViewerLayout(g_viewerWnd);
}

// ─── Session helpers ──────────────────────────────────────────────────────────
static void AddRecentSession(const std::string& ip) {
    // Remove duplicate
    g_recentSessions.erase(
        std::remove_if(g_recentSessions.begin(), g_recentSessions.end(),
            [&](const RecentSession& s){ return s.ip==ip; }),
        g_recentSessions.end());
    RecentSession rs; rs.ip=ip; rs.name=ip; rs.lastConn=time(nullptr);
    g_recentSessions.insert(g_recentSessions.begin(), rs);
    if(g_recentSessions.size()>8) g_recentSessions.resize(8);
    SaveConfig();
    g_activeTab=1;  // switch to Sessões Recentes tab
    if(g_mainWnd) InvalidateRect(g_mainWnd,nullptr,FALSE);
}

static void DoShare() {
    char buf[256];
    snprintf(buf,sizeof(buf),"IP: %s\nSenha: %s",g_localIP,g_sessionPass.c_str());
    if(OpenClipboard(g_mainWnd)){
        EmptyClipboard();
        HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,strlen(buf)+1);
        if(hg){ memcpy(GlobalLock(hg),buf,strlen(buf)+1); GlobalUnlock(hg);
                SetClipboardData(CF_TEXT,hg); }
        CloseClipboard();
    }
    MessageBoxW(g_mainWnd,
        L"Informações copiadas para a área de transferência!\n\n"
        L"Compartilhe com o técnico para iniciar a sessão remota.",
        L"Compartilhar Acesso",MB_OK|MB_ICONINFORMATION);
}

// ─── Connect ──────────────────────────────────────────────────────────────────
static void RunDlgLoop(HWND dlg, HWND editCtrl, HWND disableParent); // defined below

// Converts dotless numeric IP "192168110" → "192.168.1.10".
// Passes dotted IPs and hostnames through unchanged.
static std::string NormIP(const std::string& raw) {
    if(raw.find('.')!=std::string::npos) return raw;
    for(char c:raw) if(!isdigit((unsigned char)c)) return raw;
    int len=(int)raw.size();
    if(len<4||len>12) return raw;
    // Greedy: longest first-two octets, shortest third+fourth (favors x.x.1.xx over x.x.11.x)
    for(int i=std::min(3,len-3);i>=1;i--){
        int a=std::stoi(raw.substr(0,i)); if(a>255) continue;
        for(int j=std::min(3,len-i-2);j>=1;j--){
            int b=std::stoi(raw.substr(i,j)); if(b>255) continue;
            for(int k=1;k<=std::min(3,len-i-j-1);k++){
                int c=std::stoi(raw.substr(i+j,k)); if(c>255) continue;
                int dl=len-i-j-k; if(dl<1||dl>3) continue;
                int d=std::stoi(raw.substr(i+j+k)); if(d>255) continue;
                char buf[32]; snprintf(buf,sizeof(buf),"%d.%d.%d.%d",a,b,c,d);
                return buf;
            }
        }
    }
    return raw;
}

static void DoConnect(){
    char ipRaw[128]={};
    GetWindowTextA(g_editRemIP,ipRaw,sizeof(ipRaw));
    if(!ipRaw[0]){
        MessageBoxW(g_mainWnd,L"Digite o IP do host na barra de endereço.",L"Umbrela Viewer",MB_OK|MB_ICONWARNING);
        return;
    }
    std::string ip=NormIP(ipRaw);

    // Ask for password (optional — empty = request access)
    g_dlgPassBuf[0]=0; g_dlgResultCode=IDCANCEL; g_dlgDone=false;
    HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,L"UVDlg",L"Umbrela Viewer — Conectar",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,250,185,330,168,nullptr,nullptr,g_hInst,nullptr);
    HFONT hf=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mkc=[&](const char*cls,const char*t,DWORD st,int x,int y,int w,int h,int id)->HWND{
        HWND c=CreateWindowA(cls,t,WS_CHILD|WS_VISIBLE|st,x,y,w,h,dlg,(HMENU)(intptr_t)id,g_hInst,nullptr);
        SendMessage(c,WM_SETFONT,(WPARAM)hf,TRUE);return c;
    };
    mkc("STATIC","Senha (opcional):",SS_LEFT,10,10,290,18,0);
    HWND editP=mkc("EDIT","",WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL,10,32,244,24,100);
    SendMessageW(editP,EM_SETCUEBANNER,TRUE,(LPARAM)L"Deixe vazio para solicitar acesso");
    mkc("BUTTON","Mostrar",BS_PUSHBUTTON,258,32,52,24,200);
    mkc("STATIC",
        "Sem senha: o usuário do host precisará aceitar a conexão.",
        SS_LEFT,10,62,300,28,0);
    mkc("BUTTON","Cancelar",BS_PUSHBUTTON,120,98,90,26,IDCANCEL);
    mkc("BUTTON","Conectar",BS_DEFPUSHBUTTON,220,98,90,26,IDOK);
    g_activeDlgEdit=editP; SetFocus(editP);
    RunDlgLoop(dlg,editP,g_mainWnd);
    if(g_dlgResultCode!=IDOK) return;

    g_connectingIP = ip;   // saved so WM_CONNECT_OK can add to recent sessions
    EnableWindow(g_btnConnect,FALSE);

    // TCP connection runs in background — avoids freezing the UI
    struct ConnArgs { std::string ip; char pass[65]; };
    auto* args=new ConnArgs; args->ip=ip; strncpy_s(args->pass,g_dlgPassBuf,64);

    std::thread([](ConnArgs* a){
        auto fail=[](const wchar_t* msg){
            MessageBoxW(nullptr,msg,L"Umbrela Viewer",MB_OK|MB_ICONERROR);
            if(g_mainWnd) PostMessage(g_mainWnd,WM_CONNECT_FAIL,0,0);
        };

        // Resolve address (handles both IPs and hostnames)
        addrinfo hints={},*res=nullptr;
        hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
        char portStr[8]; snprintf(portStr,sizeof(portStr),"%d",DEFAULT_PORT);
        if(getaddrinfo(a->ip.c_str(),portStr,&hints,&res)!=0||!res){
            delete a; fail(L"Endereço inválido ou host não encontrado."); return;
        }

        SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(s==INVALID_SOCKET){ freeaddrinfo(res); delete a; fail(L"Erro interno de socket."); return; }

        // Non-blocking connect with 5-second timeout
        u_long nb=1; ioctlsocket(s,FIONBIO,&nb);
        connect(s,res->ai_addr,(int)res->ai_addrlen);
        freeaddrinfo(res);

        fd_set wr,ex; FD_ZERO(&wr); FD_ZERO(&ex); FD_SET(s,&wr); FD_SET(s,&ex);
        timeval tv={5,0};
        if(select(0,nullptr,&wr,&ex,&tv)<=0||FD_ISSET(s,&ex)){
            closesocket(s); delete a;
            fail(L"Não foi possível conectar ao host.\nVerifique o IP e se o programa está aberto no host.");
            return;
        }
        int err=0,elen=sizeof(err);
        getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&err,&elen);
        if(err){ closesocket(s); delete a; fail(L"Não foi possível conectar ao host."); return; }

        // Back to blocking with recv/send timeout
        // If no password: 35s timeout (host user has 30s to accept/deny)
        nb=0; ioctlsocket(s,FIONBIO,&nb);
        bool noPass = (a->pass[0]=='\0');
        DWORD to = noPass ? 35000u : 5000u;
        setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&to,sizeof(to));
        setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&to,sizeof(to));

        ConnectRequestData crd={};
        strncpy_s(crd.password,a->pass,63);
        PacketHeader req={PacketType::CONNECT_REQUEST,sizeof(crd)};
        send(s,(char*)&req,sizeof(req),0);
        send(s,(char*)&crd,sizeof(crd),0);

        PacketHeader resp={};
        if(recv(s,(char*)&resp,sizeof(resp),MSG_WAITALL)!=sizeof(resp)||
           resp.type!=PacketType::CONNECT_ACCEPT){
            const wchar_t* msg=L"Sem resposta do servidor.";
            if(resp.type==PacketType::CONNECT_DENY)
                msg = noPass ? L"Acesso recusado pelo usuário." : L"Senha incorreta.";
            else if(resp.type==PacketType::CONNECT_BUSY)
                msg=L"Host ocupado. Aguarde a sessão atual terminar.";
            closesocket(s); delete a; fail(msg); return;
        }

        // Connected — remove timeouts and hand socket to viewer
        DWORD noT=0;
        setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&noT,sizeof(noT));
        setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&noT,sizeof(noT));
        delete a;
        if(g_mainWnd) PostMessage(g_mainWnd,WM_CONNECT_OK,(WPARAM)s,0);
    },args).detach();
}

// ─── Install helpers ──────────────────────────────────────────────────────────
// "Todos os usuários" → C:\Program Files\UmbrelaViewer  (precisa de admin)
static std::string GetAllUsersInstallDir() {
    char buf[MAX_PATH];
    SHGetFolderPathA(nullptr,CSIDL_PROGRAM_FILES,nullptr,0,buf);
    return std::string(buf)+"\\UmbrelaViewer";
}
static std::string GetAllUsersInstallExe() {
    return GetAllUsersInstallDir()+"\\UmbrelaViewer.exe";
}
static bool IsInstalledForAllUsers() {
    return GetFileAttributesA(GetAllUsersInstallExe().c_str())!=INVALID_FILE_ATTRIBUTES;
}

// Chamado em instância elevada (/install-all): executa instalação real
static void DoInstallAllUsersNow() {
    std::error_code ec;
    std::filesystem::create_directories(GetAllUsersInstallDir(),ec);
    char src[MAX_PATH]; GetModuleFileNameA(nullptr,src,MAX_PATH);
    std::string dst=GetAllUsersInstallExe();
    // Se arquivo existe e está em uso, renomeia o antigo e copia o novo
    if(!CopyFileA(src,dst.c_str(),FALSE)){
        std::string bak=dst+".bak";
        MoveFileExA(dst.c_str(),bak.c_str(),MOVEFILE_REPLACE_EXISTING);
        CopyFileA(src,dst.c_str(),FALSE);
    }
    // Autorun HKLM — todos os usuários
    HKEY key;
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){
        std::string val="\""+dst+"\"";
        RegSetValueExA(key,"UmbrelaViewer",0,REG_SZ,(BYTE*)val.c_str(),(DWORD)val.size()+1);
        RegCloseKey(key);
    }
    // Atalho Desktop público
    CoInitialize(nullptr);
    IShellLinkA* psl=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,
            IID_IShellLinkA,(void**)&psl))){
        psl->SetPath(dst.c_str());
        psl->SetDescription("Umbrela Viewer");
        psl->SetIconLocation(dst.c_str(),0);
        IPersistFile* ppf=nullptr;
        if(SUCCEEDED(psl->QueryInterface(IID_IPersistFile,(void**)&ppf))){
            char desk[MAX_PATH];
            SHGetFolderPathA(nullptr,CSIDL_COMMON_DESKTOPDIRECTORY,nullptr,0,desk);
            std::wstring lnk=std::filesystem::path(desk).append("UmbrelaViewer.lnk").wstring();
            ppf->Save(lnk.c_str(),TRUE); ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
    // Lança versão instalada
    ShellExecuteA(nullptr,"open",dst.c_str(),nullptr,nullptr,SW_SHOW);
}

// Botão "Instalar agora" — pede UAC e delega para instância elevada
static void DoInstallAllUsers() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr,exePath,MAX_PATH);
    SHELLEXECUTEINFOA sei={};
    sei.cbSize=sizeof(sei);
    sei.lpVerb="runas";           // pede elevação (UAC)
    sei.lpFile=exePath;
    sei.lpParameters="/install-all";
    sei.nShow=SW_SHOW;
    ShellExecuteExA(&sei);
    // Continua rodando sem instalar — usuário pode usar normalmente
}

// ─── Windows Service infrastructure ─────────────────────────────────────────
static bool IsServiceInstalled() {
    SC_HANDLE hscm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hscm) return false;
    SC_HANDLE hsvc = OpenServiceW(hscm, SVC_NAMEW, SERVICE_QUERY_STATUS);
    bool r = (hsvc != nullptr);
    if (hsvc) CloseServiceHandle(hsvc);
    CloseServiceHandle(hscm);
    return r;
}

static bool IsServiceRunning() {
    SC_HANDLE hscm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hscm) return false;
    SC_HANDLE hsvc = OpenServiceW(hscm, SVC_NAMEW, SERVICE_QUERY_STATUS);
    bool r = false;
    if (hsvc) {
        SERVICE_STATUS ss = {};
        if (QueryServiceStatus(hsvc, &ss))
            r = (ss.dwCurrentState == SERVICE_RUNNING || ss.dwCurrentState == SERVICE_START_PENDING);
        CloseServiceHandle(hsvc);
    }
    CloseServiceHandle(hscm);
    return r;
}

static VOID WINAPI SvcCtrlHandler(DWORD ctrl) {
    if (ctrl == SERVICE_CONTROL_STOP || ctrl == SERVICE_CONTROL_SHUTDOWN) {
        g_svcStatus.dwCurrentState = SERVICE_STOP_PENDING;
        g_svcStatus.dwWaitHint     = 5000;
        SetServiceStatus(g_svcHandle, &g_svcStatus);
        if (g_svcStopEvt) SetEvent(g_svcStopEvt);
    }
}

static VOID WINAPI ServiceMain(DWORD /*argc*/, LPSTR* /*argv*/) {
    g_svcHandle = RegisterServiceCtrlHandlerA(SVC_NAME, SvcCtrlHandler);
    if (!g_svcHandle) return;

    g_svcStatus.dwServiceType      = SERVICE_WIN32_OWN_PROCESS;
    g_svcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_svcStatus.dwCurrentState     = SERVICE_START_PENDING;
    g_svcStatus.dwWaitHint         = 8000;
    SetServiceStatus(g_svcHandle, &g_svcStatus);

    // ── Connect SYSTEM process to the interactive window station ──────────
    HWINSTA hWinSta = OpenWindowStationW(L"WinSta0", FALSE, WINSTA_ALL_ACCESS);
    if (hWinSta) SetProcessWindowStation(hWinSta);

    // ── Init subsystems ──────────────────────────────────────────────────
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    Gdiplus::GdiplusStartup(&g_gdipToken, &g_gdip, nullptr);

    LoadConfig();
    GetLocalIP();
    g_sessionPass  = GeneratePassword();
    g_accessEvent  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_svcStopEvt   = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_runningAsService = true;

    g_svcStatus.dwCurrentState = SERVICE_RUNNING;
    g_svcStatus.dwWaitHint     = 0;
    SetServiceStatus(g_svcHandle, &g_svcStatus);

    StartHosting();

    WaitForSingleObject(g_svcStopEvt, INFINITE);

    StopHosting();
    WSACleanup();
    Gdiplus::GdiplusShutdown(g_gdipToken);
    if (g_accessEvent) { CloseHandle(g_accessEvent); g_accessEvent = nullptr; }
    if (g_svcStopEvt)  { CloseHandle(g_svcStopEvt);  g_svcStopEvt  = nullptr; }

    g_svcStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_svcHandle, &g_svcStatus);
}

// Elevated helper: actually create / delete the service entry
static void InstallServiceNow() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    SC_HANDLE hscm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hscm) {
        MessageBoxW(nullptr,
            L"Falha ao abrir o Gerenciador de Serviços.\n"
            L"São necessários privilégios de administrador.",
            L"Umbrela Viewer", MB_OK|MB_ICONERROR);
        return;
    }
    std::string cmd = std::string("\"") + exePath + "\" /service";

    SC_HANDLE hsvc = CreateServiceA(hscm, SVC_NAME,
        "Umbrela Viewer Remote Access",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        cmd.c_str(), nullptr, nullptr, nullptr,
        "LocalSystem", nullptr);

    if (hsvc) {
        SERVICE_DESCRIPTIONW sd = { (LPWSTR)SVC_DESCW };
        ChangeServiceConfig2W(hsvc, SERVICE_CONFIG_DESCRIPTION, &sd);
        StartService(hsvc, 0, nullptr);
        CloseServiceHandle(hsvc);
        MessageBoxW(nullptr,
            L"Serviço instalado e iniciado!\n\n"
            L"O Umbrela Viewer agora captura a tela em segundo plano,\n"
            L"incluindo prompts de UAC e a tela de logon.",
            L"Umbrela Viewer", MB_OK|MB_ICONINFORMATION);
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS)
            MessageBoxW(nullptr, L"O serviço já está instalado.", L"Umbrela Viewer", MB_OK|MB_ICONINFORMATION);
        else {
            wchar_t msg[256];
            swprintf_s(msg, L"Falha ao instalar o serviço.\nCódigo de erro: %lu", err);
            MessageBoxW(nullptr, msg, L"Umbrela Viewer", MB_OK|MB_ICONERROR);
        }
    }
    CloseServiceHandle(hscm);
}

static void UninstallServiceNow() {
    SC_HANDLE hscm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!hscm) {
        MessageBoxW(nullptr, L"Falha ao abrir o Gerenciador de Serviços.",
            L"Umbrela Viewer", MB_OK|MB_ICONERROR);
        return;
    }
    SC_HANDLE hsvc = OpenServiceW(hscm, SVC_NAMEW, SERVICE_STOP|DELETE|SERVICE_QUERY_STATUS);
    if (hsvc) {
        SERVICE_STATUS ss = {};
        ControlService(hsvc, SERVICE_CONTROL_STOP, &ss);
        // Give it up to 3s to stop
        for (int i = 0; i < 30; i++) {
            QueryServiceStatus(hsvc, &ss);
            if (ss.dwCurrentState == SERVICE_STOPPED) break;
            Sleep(100);
        }
        DeleteService(hsvc);
        CloseServiceHandle(hsvc);
        MessageBoxW(nullptr, L"Serviço removido com sucesso.",
            L"Umbrela Viewer", MB_OK|MB_ICONINFORMATION);
    } else {
        MessageBoxW(nullptr, L"Serviço não encontrado.", L"Umbrela Viewer", MB_OK|MB_ICONWARNING);
    }
    CloseServiceHandle(hscm);
}

// UI-side launchers — request UAC elevation for install/uninstall
static void DoInstallService() {
    char exePath[MAX_PATH]; GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    SHELLEXECUTEINFOA sei = {}; sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas"; sei.lpFile = exePath;
    sei.lpParameters = "/install-service"; sei.nShow = SW_SHOW;
    ShellExecuteExA(&sei);
    // Refresh the card after a short delay so the status reflects the change
    if (g_mainWnd) {
        std::thread([]{ Sleep(2500); if(g_mainWnd) InvalidateRect(g_mainWnd,nullptr,FALSE); }).detach();
    }
}
static void DoUninstallService() {
    char exePath[MAX_PATH]; GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    SHELLEXECUTEINFOA sei = {}; sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas"; sei.lpFile = exePath;
    sei.lpParameters = "/uninstall-service"; sei.nShow = SW_SHOW;
    ShellExecuteExA(&sei);
    if (g_mainWnd) {
        std::thread([]{ Sleep(2500); if(g_mainWnd) InvalidateRect(g_mainWnd,nullptr,FALSE); }).detach();
    }
}

// ─── App icon — UV monitor badge ─────────────────────────────────────────────
static HICON CreateAppIcon(int sz) {
    HDC dc = GetDC(nullptr), mdc = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, sz, sz);
    HBITMAP ob  = (HBITMAP)SelectObject(mdc, bmp);

    Gdiplus::Graphics g(mdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    // Black fill for the mask region outside the circle
    Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));
    g.FillRectangle(&black, 0.f, 0.f, (float)sz, (float)sz);

    DrawIconContent(g, 0.f, 0.f, (float)sz, true);

    SelectObject(mdc, ob); DeleteDC(mdc); ReleaseDC(nullptr, dc);
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    ICONINFO ii = {}; ii.fIcon = TRUE; ii.hbmColor = bmp; ii.hbmMask = mask;
    HICON ico = CreateIconIndirect(&ii);
    DeleteObject(bmp); DeleteObject(mask);
    return ico;
}

// ─── Main window paint ────────────────────────────────────────────────────────
static void PaintMain(HWND hw, HDC hdc){
    RECT wrc; GetClientRect(hw,&wrc);
    int W=wrc.right;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    VBClear();

    // ── Fonts ─────────────────────────────────────────────────────────────────
    Gdiplus::FontFamily ff    (L"Segoe UI");
    Gdiplus::FontFamily ffMono(L"Consolas");
    Gdiplus::Font fntBold(&ff,13,Gdiplus::FontStyleBold,   Gdiplus::UnitPixel);
    Gdiplus::Font fntNorm(&ff,13,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::Font fntSm  (&ff,11,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::Font fntTiny(&ff,10,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush wBrush (C::TEXT);
    Gdiplus::SolidBrush w2Brush(C::TEXT2);
    Gdiplus::SolidBrush bgBrush(C::BG);

    // ── Shared data strings ───────────────────────────────────────────────────
    wchar_t wipW[64]={};
    MultiByteToWideChar(CP_UTF8,0,g_localIP,-1,wipW,64);
    wchar_t wpass[16]={};
    MultiByteToWideChar(CP_UTF8,0,g_sessionPass.c_str(),-1,wpass,16);

    // ── Background + radial glows ─────────────────────────────────────────────
    g.FillRectangle(&bgBrush,0.f,0.f,(float)W,(float)WIN_H);
    // Top-right: blue glow (circle at 80%,-10%)
    {
        Gdiplus::GraphicsPath path;
        float r=W*0.38f, cx=W*0.80f, cy=-WIN_H*0.08f;
        path.AddEllipse(cx-r,cy-r,r*2,r*2);
        Gdiplus::PathGradientBrush pgb(&path);
        pgb.SetCenterColor(Gdiplus::Color(40,37,99,235));
        int n=1; Gdiplus::Color edge(0,37,99,235);
        pgb.SetSurroundColors(&edge,&n);
        g.FillPath(&pgb,&path);
    }
    // Top-left: cyan glow (circle at 20%,18%)
    {
        Gdiplus::GraphicsPath path;
        float r=W*0.32f, cx=W*0.18f, cy=WIN_H*0.18f;
        path.AddEllipse(cx-r,cy-r,r*2,r*2);
        Gdiplus::PathGradientBrush pgb(&path);
        pgb.SetCenterColor(Gdiplus::Color(20,34,211,238));
        int n=1; Gdiplus::Color edge(0,34,211,238);
        pgb.SetSurroundColors(&edge,&n);
        g.FillPath(&pgb,&path);
    }

    // ── [1] Navbar (glass dark, border-radius bottom) ─────────────────────
    {
        // Semi-transparent dark gradient
        Gdiplus::LinearGradientBrush navGr(
            Gdiplus::PointF(0.f,0.f), Gdiplus::PointF(0.f,(float)(TOOL_H+14)),
            Gdiplus::Color(210,10,16,32), Gdiplus::Color(210,15,23,42));
        // Extend 14px past TOOL_H so top corners are clipped → bottom-only-radius effect
        FillRR(g,0.f,-14.f,(float)W,(float)(TOOL_H+14),14.f,navGr);
        Gdiplus::Pen navBorder(C::BORDER,1.f);
        g.DrawLine(&navBorder,0.f,(float)TOOL_H,(float)W,(float)TOOL_H);

        DrawUmbrelaIcon(g,14.f,7.f,32.f);
        Gdiplus::Font fntBrand(&ff,15,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
        g.DrawString(L"UMBRELA",-1,&fntBrand,Gdiplus::PointF(55.f,9.f),&wBrush);
        Gdiplus::Font fntSub(&ff,12,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
        Gdiplus::SolidBrush cyanBr(C::PRIMARY2);
        g.DrawString(L"VIEWER",-1,&fntSub,Gdiplus::PointF(55.f,27.f),&cyanBr);

        bool hambHot=(g_hotBtn==VB_HAMBURGER);
        if(hambHot||g_dnBtn==VB_HAMBURGER){
            Gdiplus::SolidBrush hbg(hambHot?Gdiplus::Color(50,255,255,255):Gdiplus::Color(22,255,255,255));
            FillRR(g,(float)(W-48),4.f,38.f,38.f,9.f,hbg);
        }
        DrawHamburger(g,W-29,23,hambHot);
        VBAdd(VB_HAMBURGER,W-48,4,38,38);
    }

    // ── [2] Connect box (glass, inner glow, gradient → button) ───────────
    {
        float bx=12.f, by=(float)(ADDR_Y+8), bw=(float)(W-24), bh=34.f;
        bool addrFocus=(GetFocus()==g_editRemIP);

        Gdiplus::SolidBrush boxBg(C::ADDRBOX);
        FillRR(g,bx,by,bw,bh,12.f,boxBg);
        // Inner inset glow
        {
            Gdiplus::SolidBrush ig(addrFocus?Gdiplus::Color(22,37,99,235):Gdiplus::Color(12,37,99,235));
            FillRR(g,bx+1,by+1,bw-2,bh-2,11.f,ig);
        }
        Gdiplus::Pen boxBorder(addrFocus?C::BDRHI:C::BORDER,1.f);
        StrokeRR(g,bx,by,bw,bh,12.f,boxBorder);

        // → Connect button (gradient #2563EB → #22D3EE, 135deg)
        bool connHot=(g_hotBtn==VB_CONNECT), connDn=(g_dnBtn==VB_CONNECT);
        float cbx=(float)(W-64), cby=by+3.f, cbw=50.f, cbh=bh-6.f;
        {
            Gdiplus::Color c1=connDn?Gdiplus::Color(255,30,64,175):Gdiplus::Color(255,37,99,235);
            Gdiplus::Color c2=connDn?Gdiplus::Color(255,20,40,120):
                              connHot?Gdiplus::Color(255,34,211,238):Gdiplus::Color(255,14,165,233);
            Gdiplus::LinearGradientBrush btnGr(
                Gdiplus::PointF(cbx,cby),Gdiplus::PointF(cbx+cbw,cby+cbh),c1,c2);
            FillRR(g,cbx,cby,cbw,cbh,9.f,btnGr);
            if(!connDn){
                Gdiplus::Pen glowPen(Gdiplus::Color(90,34,211,238),1.4f);
                StrokeRR(g,cbx,cby,cbw,cbh,9.f,glowPen);
            }
        }
        Gdiplus::Font fntArrow(&ff,16,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
        Gdiplus::SolidBrush arrowBr(Gdiplus::Color(255,255,255,255));
        Gdiplus::StringFormat sfCtr;
        sfCtr.SetAlignment(Gdiplus::StringAlignmentCenter);
        sfCtr.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"→",-1,&fntArrow,Gdiplus::RectF(cbx,cby,cbw,cbh),&sfCtr,&arrowBr);
        VBAdd(VB_CONNECT,(int)cbx,(int)cby,(int)cbw,(int)cbh);
    }

    // ── [3] Hero card (2-col: IP | senha) ────────────────────────────────
    {
        float cX=12.f, cY=(float)(HERO_Y+8), cW=(float)(W-24), cH=(float)(HERO_H-14);
        // Card gradient
        Gdiplus::LinearGradientBrush cardGr(
            Gdiplus::PointF(cX,cY), Gdiplus::PointF(cX+cW,cY+cH),
            Gdiplus::Color(245,17,24,39), Gdiplus::Color(215,15,23,42));
        FillRR(g,cX,cY,cW,cH,14.f,cardGr);
        Gdiplus::Pen cardPen(C::BORDER,1.f);
        StrokeRR(g,cX,cY,cW,cH,14.f,cardPen);

        float halfW=cW*0.5f;
        { Gdiplus::Pen sp(C::BORDER,1.f); g.DrawLine(&sp,cX+halfW,cY+14.f,cX+halfW,cY+cH-14.f); }

        // LEFT — IP
        {
            Gdiplus::StringFormat sfCen; sfCen.SetAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(L"Este dispositivo",-1,&fntSm,
                Gdiplus::RectF(cX,cY+8.f,halfW,16.f),&sfCen,&w2Brush);

            // IP: 38px bold, cyan, glow layers
            Gdiplus::Font fntIP(&ff,38,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
            Gdiplus::RectF ipMsr;
            g.MeasureString(wipW,-1,&fntIP,Gdiplus::PointF(0,0),&ipMsr);
            float ipX=cX+(halfW-ipMsr.Width)*0.5f, ipY=cY+26.f;
            // Glow layers (3 passes with offset+alpha)
            for(int d=1;d<=3;d++){
                Gdiplus::SolidBrush gl(Gdiplus::Color((BYTE)(55/d),34,211,238));
                g.DrawString(wipW,-1,&fntIP,Gdiplus::PointF(ipX+(float)d,ipY+(float)d),&gl);
            }
            Gdiplus::SolidBrush ipBr(C::IP);
            g.DrawString(wipW,-1,&fntIP,Gdiplus::PointF(ipX,ipY),&ipBr);

            // Copy button
            bool cpHot=(g_hotBtn==VB_COPY_IP);
            float cpX=cX+(halfW-80.f)*0.5f, cpY=cY+78.f;
            Gdiplus::SolidBrush cpBg(cpHot?Gdiplus::Color(50,37,99,235):Gdiplus::Color(22,37,99,235));
            FillRR(g,cpX,cpY,80.f,24.f,8.f,cpBg);
            Gdiplus::Pen cpBorder(C::BORDER,1.f);
            StrokeRR(g,cpX,cpY,80.f,24.f,8.f,cpBorder);
            g.DrawString(L"⧉ Copiar",-1,&fntTiny,Gdiplus::PointF(cpX+11.f,cpY+5.f),&w2Brush);
            VBAdd(VB_COPY_IP,(int)cpX,(int)cpY,80,24);
        }

        // RIGHT — Senha
        {
            float rx=cX+halfW+2.f, rw=halfW-2.f;
            Gdiplus::StringFormat sfCen; sfCen.SetAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(L"Senha de sessão:",-1,&fntSm,
                Gdiplus::RectF(rx,cY+8.f,rw,16.f),&sfCen,&w2Brush);

            // Password badge: green border + bg + text
            Gdiplus::Font fntPass(&ffMono,24,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
            Gdiplus::RectF pMsr;
            g.MeasureString(wpass,-1,&fntPass,Gdiplus::PointF(0,0),&pMsr);
            float bW=pMsr.Width+44.f, bH=44.f;
            float bX=rx+(rw-bW)*0.5f, bY=cY+26.f;
            Gdiplus::SolidBrush passBg(Gdiplus::Color(30,16,185,129));
            FillRR(g,bX,bY,bW,bH,12.f,passBg);
            Gdiplus::Pen passBdr(Gdiplus::Color(180,16,185,129),1.f);
            StrokeRR(g,bX,bY,bW,bH,12.f,passBdr);
            Gdiplus::SolidBrush passTxt(C::PASS);
            Gdiplus::StringFormat sfPCen;
            sfPCen.SetAlignment(Gdiplus::StringAlignmentCenter);
            sfPCen.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(wpass,-1,&fntPass,Gdiplus::RectF(bX,bY,bW,bH),&sfPCen,&passTxt);

            // Buttons: Regen + Share (gradient)
            float btnY=cY+80.f, cx2=rx+rw*0.5f;
            {
                bool rgHot=(g_hotBtn==VB_REGEN_PASS);
                float rgX=cx2-88.f;
                Gdiplus::SolidBrush rgBg(rgHot?Gdiplus::Color(50,226,232,240):Gdiplus::Color(20,226,232,240));
                FillRR(g,rgX,btnY,78.f,24.f,8.f,rgBg);
                Gdiplus::Pen rgBdr(C::BORDER,1.f);
                StrokeRR(g,rgX,btnY,78.f,24.f,8.f,rgBdr);
                g.DrawString(L"↻ Nova",-1,&fntTiny,Gdiplus::PointF(rgX+12.f,btnY+5.f),&w2Brush);
                VBAdd(VB_REGEN_PASS,(int)rgX,(int)btnY,78,24);
            }
            {
                bool shHot=(g_hotBtn==VB_SHARE);
                float shX=cx2-2.f;
                Gdiplus::Color sh1=shHot?Gdiplus::Color(255,37,99,235):Gdiplus::Color(200,37,99,235);
                Gdiplus::Color sh2=shHot?Gdiplus::Color(255,34,211,238):Gdiplus::Color(180,14,116,144);
                Gdiplus::LinearGradientBrush shGr(
                    Gdiplus::PointF(shX,btnY),Gdiplus::PointF(shX+88.f,btnY+24.f),sh1,sh2);
                FillRR(g,shX,btnY,88.f,24.f,8.f,shGr);
                Gdiplus::SolidBrush shTxt(C::TEXT);
                g.DrawString(L"↗ Compartilhar",-1,&fntTiny,Gdiplus::PointF(shX+6.f,btnY+5.f),&shTxt);
                VBAdd(VB_SHARE,(int)shX,(int)btnY,88,24);
            }
        }

        // Status row
        {
            float stLineY=cY+cH-28.f;
            Gdiplus::Pen stSep(C::BORDER,1.f);
            g.DrawLine(&stSep,cX+10.f,stLineY,cX+cW-10.f,stLineY);
            float stY=stLineY+7.f;
            Gdiplus::Color dotCol=g_srvConnected?C::GREEN:(g_srvRunning?C::YELLOW:C::TEXT2);
            // 14px dot + double-layer glow
            float dcx=cX+24.f, dcy=stY+7.f, dr=7.f;
            if(g_srvConnected||g_srvRunning){
                Gdiplus::SolidBrush h1(Gdiplus::Color(70,dotCol.GetR(),dotCol.GetG(),dotCol.GetB()));
                Gdiplus::SolidBrush h2(Gdiplus::Color(35,dotCol.GetR(),dotCol.GetG(),dotCol.GetB()));
                g.FillEllipse(&h2,dcx-dr*2.4f,dcy-dr*2.4f,dr*4.8f,dr*4.8f);
                g.FillEllipse(&h1,dcx-dr*1.6f,dcy-dr*1.6f,dr*3.2f,dr*3.2f);
            }
            Gdiplus::SolidBrush dotBr(dotCol);
            g.FillEllipse(&dotBr,dcx-dr,dcy-dr,dr*2,dr*2);
            const wchar_t* stTxt=g_srvConnected?L"Sessão ativa — cliente conectado":
                                  g_srvRunning  ?L"Aguardando conexão...":L"Servidor parado";
            g.DrawString(stTxt,-1,&fntSm,Gdiplus::PointF(cX+40.f,stY+2.f),&w2Brush);
        }
    }

    // ── [4] Tab bar ───────────────────────────────────────────────────────
    {
        g.FillRectangle(&bgBrush,0.f,(float)TAB_Y,(float)W,(float)TAB_H);
        Gdiplus::Pen tabBotPen(C::BORDER,1.f);
        g.DrawLine(&tabBotPen,0.f,(float)(TAB_Y+TAB_H-1),(float)W,(float)(TAB_Y+TAB_H-1));

        const wchar_t* tabLabels[]={ L"Início", L"Sessões Recentes" };
        int tabIds[]={ VB_TAB_HOME, VB_TAB_RECENT };
        float tx=16.f;
        for(int i=0;i<2;i++){
            bool active=(g_activeTab==i);
            Gdiplus::Font& tfnt=active?fntBold:fntNorm;
            Gdiplus::RectF tMsr;
            g.MeasureString(tabLabels[i],-1,&tfnt,Gdiplus::PointF(0,0),&tMsr);
            float tw=tMsr.Width+24.f;
            if(active){
                // Glow text shadow for active tab
                Gdiplus::SolidBrush glTxt(Gdiplus::Color(55,34,211,238));
                g.DrawString(tabLabels[i],-1,&tfnt,
                    Gdiplus::PointF(tx+13.f,(float)(TAB_Y+11)),&glTxt);
                Gdiplus::SolidBrush underBr(C::ACCENT);
                g.FillRectangle(&underBr,tx,(float)(TAB_Y+TAB_H-3),tw,3.f);
            }
            Gdiplus::SolidBrush* tbr=active?&wBrush:&w2Brush;
            g.DrawString(tabLabels[i],-1,&tfnt,Gdiplus::PointF(tx+12.f,(float)(TAB_Y+10)),tbr);
            VBAdd(tabIds[i],(int)tx,TAB_Y,(int)tw,TAB_H);
            tx+=tw+4.f;
        }
    }

    // ── [5] Content ───────────────────────────────────────────────────────
    {
        g.FillRectangle(&bgBrush,0.f,(float)CONT_Y,(float)W,(float)CONT_H);
        bool notInstalled=!IsInstalledForAllUsers();

        if(g_activeTab==0){
            float cardH=88.f, cardW=(W-36.f)*0.5f;
            float cy=(float)(CONT_Y+16);

            // Card status
            {
                Gdiplus::SolidBrush c1Bg(Gdiplus::Color(242,17,24,39));
                FillRR(g,12.f,cy,cardW,cardH,12.f,c1Bg);
                Gdiplus::Pen c1Pen(C::BORDER,1.f);
                StrokeRR(g,12.f,cy,cardW,cardH,12.f,c1Pen);
                g.DrawString(L"Status do Servidor",-1,&fntBold,
                    Gdiplus::PointF(22.f,cy+12.f),&wBrush);
                Gdiplus::Color stCol=g_srvConnected?C::GREEN:(g_srvRunning?C::YELLOW:C::TEXT2);
                Gdiplus::Font fntVal(&ff,20,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
                Gdiplus::SolidBrush stBr(stCol);
                const wchar_t* stLbl=g_srvConnected?L"Conectado":g_srvRunning?L"Aguardando":L"Parado";
                g.DrawString(stLbl,-1,&fntVal,Gdiplus::PointF(22.f,cy+36.f),&stBr);
                g.DrawString(L"Porta 7890",-1,&fntTiny,Gdiplus::PointF(22.f,cy+66.f),&w2Brush);
            }
            // Card IP
            {
                float cx2=16.f+cardW;
                Gdiplus::SolidBrush c2Bg(Gdiplus::Color(242,17,24,39));
                FillRR(g,cx2,cy,cardW,cardH,12.f,c2Bg);
                Gdiplus::Pen c2Pen(C::BORDER,1.f);
                StrokeRR(g,cx2,cy,cardW,cardH,12.f,c2Pen);
                g.DrawString(L"Endereço IP Local",-1,&fntBold,
                    Gdiplus::PointF(cx2+12.f,cy+12.f),&wBrush);
                Gdiplus::Font fntIPSm(&ff,20,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
                Gdiplus::SolidBrush ipBr(C::IP);
                g.DrawString(wipW,-1,&fntIPSm,Gdiplus::PointF(cx2+12.f,cy+36.f),&ipBr);
                g.DrawString(L"Compartilhe com o técnico",-1,&fntTiny,
                    Gdiplus::PointF(cx2+12.f,cy+66.f),&w2Brush);
            }
            // Help card
            {
                float inY=cy+cardH+10.f, inW=(float)(W-24);
                Gdiplus::SolidBrush inBg(Gdiplus::Color(242,17,24,39));
                FillRR(g,12.f,inY,inW,76.f,12.f,inBg);
                Gdiplus::Pen inPen(C::BORDER,1.f);
                StrokeRR(g,12.f,inY,inW,76.f,12.f,inPen);
                g.DrawString(L"Como usar: compartilhe o IP e a senha acima com o técnico de suporte.",
                    -1,&fntSm,Gdiplus::PointF(22.f,inY+12.f),&w2Brush);
                g.DrawString(L"O técnico conectará usando o Umbrela Viewer no lado dele.",
                    -1,&fntSm,Gdiplus::PointF(22.f,inY+32.f),&w2Brush);
                g.DrawString(L"Você poderá encerrar a sessão a qualquer momento.",
                    -1,&fntSm,Gdiplus::PointF(22.f,inY+52.f),&w2Brush);
            }
            // Install card (shown only when not installed for all users)
            if(notInstalled){
                float inY=cy+cardH+10.f+76.f+10.f, inW=(float)(W-24);
                // Card with subtle blue tint
                Gdiplus::SolidBrush instBg(Gdiplus::Color(235,14,22,45));
                FillRR(g,12.f,inY,inW,64.f,12.f,instBg);
                Gdiplus::Pen instPen(Gdiplus::Color(120,37,99,235),1.f);
                StrokeRR(g,12.f,inY,inW,64.f,12.f,instPen);
                // Icon + text
                Gdiplus::SolidBrush iconBr(Gdiplus::Color(180,34,211,238));
                g.DrawString(L"⬇",-1,&fntBold,Gdiplus::PointF(22.f,inY+12.f),&iconBr);
                g.DrawString(L"Instalar para todos os usuários",-1,&fntBold,
                    Gdiplus::PointF(42.f,inY+12.f),&wBrush);
                g.DrawString(
                    L"Inicia automaticamente com o Windows. Requer senha de administrador.",
                    -1,&fntTiny,Gdiplus::PointF(42.f,inY+32.f),&w2Brush);
                // "Instalar agora →" button
                bool instHot=(g_hotBtn==VB_INSTALL_NOW);
                float ibX=inW-130.f, ibY=inY+18.f;
                Gdiplus::Color ib1=instHot?Gdiplus::Color(255,37,99,235):Gdiplus::Color(200,37,99,235);
                Gdiplus::Color ib2=instHot?Gdiplus::Color(255,34,211,238):Gdiplus::Color(180,14,116,144);
                Gdiplus::LinearGradientBrush ibGr(
                    Gdiplus::PointF(ibX,ibY),Gdiplus::PointF(ibX+118.f,ibY+26.f),ib1,ib2);
                FillRR(g,ibX,ibY,118.f,26.f,8.f,ibGr);
                Gdiplus::SolidBrush ibTxt(C::TEXT);
                Gdiplus::StringFormat sfIB;
                sfIB.SetAlignment(Gdiplus::StringAlignmentCenter);
                sfIB.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(L"Instalar agora →",-1,&fntSm,
                    Gdiplus::RectF(ibX,ibY,118.f,26.f),&sfIB,&ibTxt);
                VBAdd(VB_INSTALL_NOW,(int)ibX,(int)ibY,118,26);
            }
            // ── Serviço Windows card ──────────────────────────────────────
            {
                float svcOffY = notInstalled ? cy+cardH+10.f+76.f+10.f+64.f+10.f
                                             : cy+cardH+10.f+76.f+10.f;
                float svcW=(float)(W-24);
                bool svcInst = IsServiceInstalled();
                bool svcRun  = svcInst && IsServiceRunning();

                // Border colour: green if running, blue if installed+stopped, default otherwise
                Gdiplus::Color svcBdrCol = svcRun
                    ? Gdiplus::Color(140,16,185,129)
                    : svcInst ? Gdiplus::Color(100,37,99,235)
                              : Gdiplus::Color(80,37,99,235);

                Gdiplus::SolidBrush svcBg(Gdiplus::Color(235,12,20,38));
                FillRR(g,12.f,svcOffY,svcW,64.f,12.f,svcBg);
                Gdiplus::Pen svcPen(svcBdrCol,1.f);
                StrokeRR(g,12.f,svcOffY,svcW,64.f,12.f,svcPen);

                // Status dot
                Gdiplus::Color dotClr = svcRun ? C::GREEN : svcInst ? C::YELLOW : C::TEXT2;
                float dcx=28.f, dcy=svcOffY+20.f;
                if(svcRun||svcInst){
                    Gdiplus::SolidBrush h1(Gdiplus::Color(60,dotClr.GetR(),dotClr.GetG(),dotClr.GetB()));
                    g.FillEllipse(&h1,dcx-10.f,dcy-10.f,20.f,20.f);
                }
                Gdiplus::SolidBrush dotBr2(dotClr);
                g.FillEllipse(&dotBr2,dcx-6.f,dcy-6.f,12.f,12.f);

                // Labels
                g.DrawString(L"Serviço Windows",-1,&fntBold,Gdiplus::PointF(44.f,svcOffY+10.f),&wBrush);
                const wchar_t* svcLbl = svcRun  ? L"Em execução — captura UAC ativa"
                                       : svcInst ? L"Instalado mas parado"
                                                 : L"Não instalado";
                Gdiplus::SolidBrush svcLblBr(dotClr);
                g.DrawString(svcLbl,-1,&fntTiny,Gdiplus::PointF(44.f,svcOffY+32.f),&svcLblBr);

                // Action button
                bool svcBtnHot=(g_hotBtn==VB_SVC_TOGGLE);
                const wchar_t* svcBtnLbl = svcInst ? L"Remover Serviço" : L"Instalar Serviço";
                Gdiplus::Color sb1 = svcInst
                    ? (svcBtnHot ? Gdiplus::Color(255,185,28,28):Gdiplus::Color(200,140,20,20))
                    : (svcBtnHot ? Gdiplus::Color(255,37,99,235) :Gdiplus::Color(200,37,99,235));
                Gdiplus::Color sb2 = svcInst
                    ? (svcBtnHot ? Gdiplus::Color(255,220,38,38) :Gdiplus::Color(180,160,30,30))
                    : (svcBtnHot ? Gdiplus::Color(255,34,211,238):Gdiplus::Color(180,14,116,144));
                float sbX=svcW-136.f, sbY=svcOffY+18.f;
                Gdiplus::LinearGradientBrush sbGr(
                    Gdiplus::PointF(sbX,sbY),Gdiplus::PointF(sbX+124.f,sbY+26.f),sb1,sb2);
                FillRR(g,sbX,sbY,124.f,26.f,8.f,sbGr);
                Gdiplus::SolidBrush sbTxt(C::TEXT);
                Gdiplus::StringFormat sfSB;
                sfSB.SetAlignment(Gdiplus::StringAlignmentCenter);
                sfSB.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(svcBtnLbl,-1,&fntSm,
                    Gdiplus::RectF(sbX,sbY,124.f,26.f),&sfSB,&sbTxt);
                VBAdd(VB_SVC_TOGGLE,(int)sbX,(int)sbY,124,26);
            }
        } else {
            // Sessões Recentes
            if(g_recentSessions.empty()){
                Gdiplus::StringFormat sfCen; sfCen.SetAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(L"Nenhuma sessão recente.",-1,&fntNorm,
                    Gdiplus::RectF(0.f,(float)(CONT_Y+80),(float)W,40.f),&sfCen,&w2Brush);
            } else {
                float cw=215.f, ch=140.f;
                float startX=16.f, startY=(float)(CONT_Y+16);
                int cols=4;
                for(int i=0;i<(int)g_recentSessions.size()&&i<8;i++){
                    int col=i%cols, row=i/cols;
                    float cx=startX+(cw+10.f)*col, cy=(float)(startY+(ch+10.f)*row);
                    bool hot=(g_hotBtn==VB_SESSION_0+i);
                    Gdiplus::SolidBrush scBg(hot?Gdiplus::Color(255,22,35,57):Gdiplus::Color(242,17,24,39));
                    FillRR(g,cx,cy,cw,ch,12.f,scBg);
                    Gdiplus::Pen scPen(hot?C::BDRHI:C::BORDER,1.f);
                    StrokeRR(g,cx,cy,cw,ch,12.f,scPen);
                    DrawUmbrelaIcon(g,cx+8.f,cy+8.f,28.f);
                    wchar_t wipR[64]={}; MultiByteToWideChar(CP_UTF8,0,g_recentSessions[i].ip.c_str(),-1,wipR,64);
                    g.DrawString(wipR,-1,&fntBold,Gdiplus::PointF(cx+8.f,cy+46.f),&wBrush);
                    wchar_t wnR[64]={}; MultiByteToWideChar(CP_UTF8,0,g_recentSessions[i].name.c_str(),-1,wnR,64);
                    g.DrawString(wnR,-1,&fntTiny,Gdiplus::PointF(cx+8.f,cy+64.f),&w2Brush);
                    if(g_recentSessions[i].lastConn){
                        char tbuf[32]; tm tm2={};
                        localtime_s(&tm2,&g_recentSessions[i].lastConn);
                        strftime(tbuf,sizeof(tbuf),"%d/%m/%Y %H:%M",&tm2);
                        wchar_t wtbuf[32]={}; MultiByteToWideChar(CP_UTF8,0,tbuf,-1,wtbuf,32);
                        g.DrawString(wtbuf,-1,&fntTiny,Gdiplus::PointF(cx+8.f,cy+82.f),&w2Brush);
                    }
                    float cbX=cx+8.f, cbY=cy+106.f, cbW=cw-16.f, cbH=24.f;
                    Gdiplus::Color cc1=hot?Gdiplus::Color(255,37,99,235):Gdiplus::Color(200,37,99,235);
                    Gdiplus::Color cc2=hot?Gdiplus::Color(255,34,211,238):Gdiplus::Color(180,14,116,144);
                    Gdiplus::LinearGradientBrush cbGr(
                        Gdiplus::PointF(cbX,cbY),Gdiplus::PointF(cbX+cbW,cbY+cbH),cc1,cc2);
                    FillRR(g,cbX,cbY,cbW,cbH,8.f,cbGr);
                    Gdiplus::SolidBrush cbTxt(C::TEXT);
                    Gdiplus::StringFormat sfCb;
                    sfCb.SetAlignment(Gdiplus::StringAlignmentCenter);
                    sfCb.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    g.DrawString(L"Conectar",-1,&fntSm,
                        Gdiplus::RectF(cbX,cbY,cbW,cbH),&sfCb,&cbTxt);
                    VBAdd(VB_SESSION_0+i,(int)cx,(int)cy,(int)cw,(int)ch);
                }
            }
        }
    }

    // ── [6] Footer ────────────────────────────────────────────────────────
    {
        Gdiplus::SolidBrush footBg(Gdiplus::Color(255,17,24,39));
        g.FillRectangle(&footBg,0.f,(float)FOOT_Y,(float)W,(float)FOOT_H);
        Gdiplus::Pen footSep(C::BORDER,1.f);
        g.DrawLine(&footSep,0.f,(float)FOOT_Y,(float)W,(float)FOOT_Y);
        Gdiplus::Font fntFoot(&ff,10,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
        Gdiplus::SolidBrush footBr(C::TEXT2);
        Gdiplus::StringFormat sfFoot; sfFoot.SetAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"Umbrela Viewer v1.0  —  Solução de acesso remoto seguro",
            -1,&fntFoot,Gdiplus::RectF(0.f,(float)(FOOT_Y+9),(float)W,18.f),&sfFoot,&footBr);
    }
}

// ─── Mini-dialog WndProc ─────────────────────────────────────────────────────
static LRESULT CALLBACK UVDlgProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if(msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if(id == IDOK) {
            if(g_activeDlgEdit) GetWindowTextA(g_activeDlgEdit, g_dlgPassBuf, 64);
            g_dlgResultCode = IDOK; g_dlgDone = true;
            DestroyWindow(hw);
        } else if(id == IDCANCEL) {
            g_dlgResultCode = IDCANCEL; g_dlgDone = true;
            DestroyWindow(hw);
        } else if(id == 200) { // toggle show/hide password
            HWND edit = g_activeDlgEdit;
            HWND btn  = GetDlgItem(hw, 200);
            if(edit && btn) {
                LRESULT pc = SendMessage(edit, EM_GETPASSWORDCHAR, 0, 0);
                if(pc) { // hidden → show
                    SendMessage(edit, EM_SETPASSWORDCHAR, 0, 0);
                    SetWindowTextA(btn, "Ocultar");
                } else { // shown → hide
                    SendMessage(edit, EM_SETPASSWORDCHAR, (WPARAM)0x25CF, 0);
                    SetWindowTextA(btn, "Mostrar");
                }
                InvalidateRect(edit, nullptr, TRUE);
            }
        }
        return 0;
    }
    if(msg == WM_CLOSE) {
        g_dlgResultCode = IDCANCEL; g_dlgDone = true;
        DestroyWindow(hw); return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// Pumps messages until dialog closes; handles Enter/Escape from child controls.
static void RunDlgLoop(HWND dlg, HWND editCtrl, HWND disableParent) {
    if(disableParent) EnableWindow(disableParent, FALSE);
    MSG m; int r;
    while(!g_dlgDone && (r = GetMessage(&m, nullptr, 0, 0)) > 0) {
        if(m.message == WM_KEYDOWN && IsWindow(dlg)) {
            if(m.wParam == VK_RETURN) {
                if(editCtrl && g_activeDlgEdit) GetWindowTextA(editCtrl, g_dlgPassBuf, 64);
                g_dlgResultCode = IDOK; g_dlgDone = true;
                DestroyWindow(dlg); continue;
            }
            if(m.wParam == VK_ESCAPE) {
                g_dlgResultCode = IDCANCEL; g_dlgDone = true;
                DestroyWindow(dlg); continue;
            }
        }
        TranslateMessage(&m); DispatchMessage(&m);
    }
    if(r == 0) PostQuitMessage((int)m.wParam); // re-post WM_QUIT
    if(disableParent) { EnableWindow(disableParent, TRUE); SetForegroundWindow(disableParent); }
    g_activeDlgEdit = nullptr;
}

// ─── Settings menu ────────────────────────────────────────────────────────────
static void ShowSettingsMenu(HWND hw){
    HMENU m=CreatePopupMenu();
    AppendMenuW(m,MF_STRING,IDM_REGEN,       L"↻  Gerar nova senha");
    AppendMenuW(m,MF_STRING,IDM_SETDEFAULT,  L"✎  Definir senha padrão...");
    if(!g_defaultPass.empty())
        AppendMenuW(m,MF_STRING,IDM_CLEARDEFAULT,L"✕  Remover senha padrão");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING,IDM_ABOUT,       L"ℹ  Sobre o Umbrela Viewer");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING,IDM_EXIT,        L"✕  Finalizar");

    // Position below hamburger button
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hw);
    TrackPopupMenu(m,TPM_RIGHTBUTTON|TPM_RIGHTALIGN,pt.x,pt.y,0,hw,nullptr);
    DestroyMenu(m);
}

static void ShowSetDefaultPassDialog(HWND parent){
    strncpy_s(g_dlgPassBuf, g_defaultPass.c_str(), 64);
    g_dlgResultCode=IDCANCEL; g_dlgDone=false;
    HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,L"UVDlg",L"Umbrela Viewer — Definir Senha Padrão",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,220,180,360,178,nullptr,nullptr,g_hInst,nullptr);
    HFONT hf=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mkc=[&](const wchar_t*cls,const wchar_t*t,DWORD st,int x,int y,int w,int h,int id)->HWND{
        HWND c=CreateWindowW(cls,t,WS_CHILD|WS_VISIBLE|st,x,y,w,h,dlg,(HMENU)(intptr_t)id,g_hInst,nullptr);
        SendMessage(c,WM_SETFONT,(WPARAM)hf,TRUE);return c;
    };
    mkc(L"STATIC",L"Senha padrão (deixe vazio para gerar automaticamente a cada início):",SS_LEFT,10,10,330,30,0);
    wchar_t wPassBuf[65]={};
    MultiByteToWideChar(CP_ACP,0,g_dlgPassBuf,-1,wPassBuf,65);
    HWND editP=mkc(L"EDIT",wPassBuf,WS_BORDER|ES_AUTOHSCROLL,10,44,330,26,100);
    SendMessage(editP,EM_SETLIMITTEXT,63,0);
    mkc(L"BUTTON",L"Cancelar",BS_PUSHBUTTON,155,95,85,28,IDCANCEL);
    mkc(L"BUTTON",L"Salvar",BS_DEFPUSHBUTTON,250,95,85,28,IDOK);
    g_activeDlgEdit=editP; SetFocus(editP);
    RunDlgLoop(dlg,editP,parent);
    if(g_dlgResultCode==IDOK){
        g_defaultPass=g_dlgPassBuf;
        SaveConfig();
        InvalidateRect(parent,nullptr,FALSE);
    }
}

// ─── Tray ────────────────────────────────────────────────────────────────────
static void SetupTray(HWND hw,HICON ico){
    g_nid.cbSize=sizeof(g_nid);g_nid.hWnd=hw;g_nid.uID=ID_TRAY;
    g_nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;g_nid.uCallbackMessage=WM_TRAYICON;
    g_nid.hIcon=ico;strncpy_s(g_nid.szTip,"Umbrela Viewer",sizeof(g_nid.szTip));
    Shell_NotifyIconA(NIM_ADD,&g_nid);
}

static void DoUpdateDownload(); // defined below
// ─── Main window proc ────────────────────────────────────────────────────────
static HBRUSH g_bgBrush   = nullptr;
static HBRUSH g_addrBrush = nullptr;

static LRESULT CALLBACK MainWndProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_ERASEBKGND:
        if(!g_bgBrush) g_bgBrush=CreateSolidBrush(C::BG_CR);
        { RECT rc; GetClientRect(hw,&rc); FillRect((HDC)wp,&rc,g_bgBrush); }
        return 1;

    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hw,&ps);
        PaintMain(hw,hdc);
        EndPaint(hw,&ps);
        return 0;
    }

    // Dark colors for the edit control (address bar)
    case WM_CTLCOLOREDIT:{
        HDC hdc=(HDC)wp;
        SetTextColor(hdc,C::TEXT_CR);
        SetBkColor(hdc,C::ADDRB_CR);
        if(!g_addrBrush) g_addrBrush=CreateSolidBrush(C::ADDRB_CR);
        return (LRESULT)g_addrBrush;
    }
    case WM_CTLCOLORSTATIC:{
        HDC hdc=(HDC)wp;
        SetTextColor(hdc,C::TEXT2_CR);
        SetBkColor(hdc,C::BG_CR);
        if(!g_bgBrush) g_bgBrush=CreateSolidBrush(C::BG_CR);
        return (LRESULT)g_bgBrush;
    }

    case WM_MOUSEMOVE:{
        int mx=LOWORD(lp),my=HIWORD(lp);
        int newHot=VBHitTest(mx,my);
        if(newHot!=g_hotBtn){g_hotBtn=newHot;InvalidateRect(hw,nullptr,FALSE);}
        if(!g_tracking){
            TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hw,0};
            TrackMouseEvent(&tme);g_tracking=true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking=false;
        if(g_hotBtn){g_hotBtn=0;InvalidateRect(hw,nullptr,FALSE);}
        return 0;

    case WM_LBUTTONDOWN:{
        int mx=LOWORD(lp),my=HIWORD(lp);
        g_dnBtn=VBHitTest(mx,my);
        if(g_dnBtn) InvalidateRect(hw,nullptr,FALSE);
        SetCapture(hw);
        return 0;
    }
    case WM_LBUTTONUP:{
        ReleaseCapture();
        int mx=LOWORD(lp),my=HIWORD(lp);
        int hit=VBHitTest(mx,my);
        int dn=g_dnBtn; g_dnBtn=0;
        InvalidateRect(hw,nullptr,FALSE);
        if(hit&&hit==dn){
            switch(hit){
            case VB_HAMBURGER: ShowSettingsMenu(hw); break;
            case VB_CONNECT:   DoConnect(); break;
            case VB_COPY_IP:
                if(OpenClipboard(hw)){
                    EmptyClipboard();
                    size_t sz=strlen(g_localIP)+1;
                    HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,sz);
                    memcpy(GlobalLock(hg),g_localIP,sz);GlobalUnlock(hg);
                    SetClipboardData(CF_TEXT,hg);CloseClipboard();
                }
                break;
            case VB_REGEN_PASS:
                g_sessionPass=GeneratePassword();
                InvalidateRect(hw,nullptr,FALSE);
                break;
            case VB_SHARE:        DoShare(); break;
            case VB_INSTALL_NOW:  DoInstallAllUsers(); break;
            case VB_SVC_TOGGLE:
                if(IsServiceInstalled()) DoUninstallService();
                else DoInstallService();
                break;
            case VB_TAB_HOME:
                g_activeTab=0; InvalidateRect(hw,nullptr,FALSE); break;
            case VB_TAB_RECENT:
                g_activeTab=1; InvalidateRect(hw,nullptr,FALSE); break;
            default:
                if(hit>=VB_SESSION_0&&hit<VB_SESSION_0+8){
                    int idx=hit-VB_SESSION_0;
                    if(idx<(int)g_recentSessions.size()){
                        std::string ip=g_recentSessions[idx].ip;
                        SetWindowTextA(g_editRemIP,ip.c_str());
                        DoConnect();
                    }
                }
                break;
            }
        }
        return 0;
    }

    case WM_UPDATE_AVAILABLE:{
        int r=MessageBoxW(hw,
            L"Nova versão do Umbrela Viewer disponível!\n\n"
            L"Deseja atualizar agora? O programa será reiniciado.",
            L"Umbrela Viewer — Atualização",MB_YESNO|MB_ICONINFORMATION);
        if(r==IDYES) std::thread(DoUpdateDownload).detach();
        return 0;
    }

    case WM_HOST_ACCESS_REQUEST:{
        // Traz a janela para frente e pisca na barra de tarefas
        ShowWindow(hw, SW_RESTORE); SetForegroundWindow(hw);
        FLASHWINFO fi={sizeof(fi),hw,FLASHW_ALL|FLASHW_TIMERNOFG,6,0};
        FlashWindowEx(&fi);
        int r=MessageBoxW(hw,
            L"Um técnico solicita acesso remoto a este computador.\n\n"
            L"Deseja permitir a conexão?\n\n"
            L"Se não reconhece esta solicitação, clique em NÃO.",
            L"Umbrela Viewer — Solicitação de Acesso",
            MB_YESNO|MB_ICONQUESTION|MB_TOPMOST|MB_DEFBUTTON2);
        g_accessDecision = (r==IDYES);
        if(g_accessEvent) SetEvent(g_accessEvent);
        return 0;
    }

    case WM_CONNECT_OK:{
        if(!g_connectingIP.empty()){ AddRecentSession(g_connectingIP); g_connectingIP.clear(); }
        g_viewSock=(SOCKET)wp;
        g_viewRunning=g_viewConnected=true;
        CreateViewerWindow();
        std::thread(ViewRecvThread).detach();
        PostMessage(g_viewerWnd,WM_VIEW_CONNECTED,0,0);
        return 0;
    }
    case WM_CONNECT_FAIL:
        EnableWindow(g_btnConnect,TRUE);
        return 0;

    case WM_HOST_STATUS:{
        g_hostStatus=(int)wp;
        InvalidateRect(hw,nullptr,FALSE);
        // Update tray tip
        const char*tips[]={"Parado","Aguardando conexão","Conectado"};
        snprintf(g_nid.szTip,sizeof(g_nid.szTip),"Umbrela Viewer — %s",tips[wp<3?wp:0]);
        Shell_NotifyIconA(NIM_MODIFY,&g_nid);
        return 0;
    }

    case WM_COMMAND:
        switch(LOWORD(wp)){
        case IDM_REGEN:
            g_sessionPass=GeneratePassword();
            InvalidateRect(hw,nullptr,FALSE); break;
        case IDM_SETDEFAULT:  ShowSetDefaultPassDialog(hw); break;
        case IDM_CLEARDEFAULT:
            g_defaultPass.clear();SaveConfig();
            g_sessionPass=GeneratePassword();
            InvalidateRect(hw,nullptr,FALSE); break;
        case IDM_ABOUT:
            MessageBoxW(hw,L"Umbrela Viewer v1.0\n\nSolução de acesso remoto seguro\npara suporte técnico interno.\n\nDesenvolvido por Jean Silva",
                L"Sobre o Umbrela Viewer",MB_OK|MB_ICONINFORMATION); break;
        case IDM_SHOW: ShowWindow(hw,SW_RESTORE);SetForegroundWindow(hw); break;
        case IDM_EXIT:
            Shell_NotifyIconA(NIM_DELETE,&g_nid);PostQuitMessage(0); break;
        }
        return 0;

    case WM_TRAYICON:
        if(lp==WM_RBUTTONUP){
            HMENU m=CreatePopupMenu();
            AppendMenuW(m,MF_STRING,IDM_SHOW,L"Mostrar Umbrela Viewer");
            AppendMenuW(m,MF_SEPARATOR,0,nullptr);
            AppendMenuW(m,MF_STRING,IDM_REGEN,L"Gerar nova senha");
            AppendMenuW(m,MF_STRING,IDM_SETDEFAULT,L"Definir senha padrão...");
            AppendMenuW(m,MF_SEPARATOR,0,nullptr);
            AppendMenuW(m,MF_STRING,IDM_EXIT,L"Finalizar");
            POINT pt;GetCursorPos(&pt);SetForegroundWindow(hw);
            TrackPopupMenu(m,TPM_RIGHTBUTTON,pt.x,pt.y,0,hw,nullptr);
            DestroyMenu(m);
        } else if(lp==WM_LBUTTONDBLCLK){
            ShowWindow(hw,SW_RESTORE);SetForegroundWindow(hw);
        }
        return 0;

    case WM_SYSCOMMAND:
        if((wp&0xFFF0)==SC_MINIMIZE){ShowWindow(hw,SW_HIDE);return 0;}
        break;
    case WM_CLOSE: ShowWindow(hw,SW_HIDE); return 0;
    case WM_DESTROY:
        Shell_NotifyIconA(NIM_DELETE,&g_nid);
        StopHosting();
        if(g_bgBrush)  {DeleteObject(g_bgBrush);g_bgBrush=nullptr;}
        if(g_addrBrush){DeleteObject(g_addrBrush);g_addrBrush=nullptr;}
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hw,msg,wp,lp);
}

// ─── Auto-update ─────────────────────────────────────────────────────────────
static void DoUpdateDownload() {
    char tmp[MAX_PATH], tmpExe[MAX_PATH+32];
    GetTempPathA(MAX_PATH, tmp);
    strcpy_s(tmpExe, tmp); strcat_s(tmpExe, "UmbrelaViewer_update.exe");
    DeleteFileA(tmpExe);

    // Notifica no tray que está baixando
    snprintf(g_nid.szTip, sizeof(g_nid.szTip), "Umbrela Viewer — Baixando atualização...");
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);

    if(SUCCEEDED(URLDownloadToFileA(nullptr, UPDATE_DOWNLOAD_URL, tmpExe, 0, nullptr))) {
        SHELLEXECUTEINFOA sei={};
        sei.cbSize=sizeof(sei); sei.lpVerb="runas";
        sei.lpFile=tmpExe; sei.lpParameters="/install"; sei.nShow=SW_SHOW;
        if(ShellExecuteExA(&sei)) {
            // Fecha instância atual depois de lançar o instalador elevado
            PostMessage(g_mainWnd, WM_COMMAND, IDM_EXIT, 0);
        } else {
            DeleteFileA(tmpExe);
            MessageBoxW(nullptr,
                L"Atualização cancelada (UAC negado ou erro).",
                L"Umbrela Viewer",MB_OK|MB_ICONWARNING);
        }
    } else {
        MessageBoxW(nullptr,
            L"Não foi possível baixar a atualização.\nVerifique a conexão e tente mais tarde.",
            L"Umbrela Viewer",MB_OK|MB_ICONERROR);
    }
}

static void CheckUpdateThread() {
    Sleep(8000); // aguarda 8s após o início para não atrasar o startup
    if(!g_mainWnd) return;

    char tmp[MAX_PATH], verFile[MAX_PATH+32];
    GetTempPathA(MAX_PATH, tmp);
    strcpy_s(verFile, tmp); strcat_s(verFile, "uv_version.txt");
    DeleteFileA(verFile);

    // Baixa o arquivo de versão
    if(FAILED(URLDownloadToFileA(nullptr, UPDATE_VERSION_URL, verFile, 0, nullptr))) return;

    int remoteVer = 0;
    { std::ifstream f(verFile); f >> remoteVer; }
    DeleteFileA(verFile);

    if(remoteVer > APP_VERSION_INT && g_mainWnd)
        PostMessage(g_mainWnd, WM_UPDATE_AVAILABLE, 0, 0);
}

// ─── WinMain ─────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR lpCmdLine,int nCmd){
    g_hInst=hInst;

    // ── /service — run as a Windows service (launched by SCM) ─────────────────
    if(lpCmdLine && strstr(lpCmdLine,"/service")){
        static SERVICE_TABLE_ENTRYA tbl[] = {
            { (LPSTR)SVC_NAME, ServiceMain },
            { nullptr, nullptr }
        };
        StartServiceCtrlDispatcherA(tbl);
        return 0;
    }

    // ── /install-service — elevated helper (do the actual install) ─────────
    if(lpCmdLine && strstr(lpCmdLine,"/install-service")){
        InstallServiceNow();
        return 0;
    }

    // ── /uninstall-service — elevated helper (do the actual remove) ────────
    if(lpCmdLine && strstr(lpCmdLine,"/uninstall-service")){
        UninstallServiceNow();
        return 0;
    }

    // ── /install-all — elevated helper (install exe for all users) ─────────
    if(lpCmdLine && strstr(lpCmdLine,"/install-all")){
        DoInstallAllUsersNow();
        return 0;
    }

    // ── Normal GUI mode ────────────────────────────────────────────────────
    // Instância única
    HANDLE mutex=CreateMutexA(nullptr,TRUE,"UmbrelaViewerMutex_3f7b");
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        HWND ex=FindWindowA("ARMain",nullptr);
        if(ex){ShowWindow(ex,SW_RESTORE);SetForegroundWindow(ex);}
        return 0;
    }

    g_accessEvent = CreateEventW(nullptr,TRUE,FALSE,nullptr); // manual-reset, not signaled

    Gdiplus::GdiplusStartup(&g_gdipToken,&g_gdip,nullptr);
    WSADATA wsa;WSAStartup(MAKEWORD(2,2),&wsa);
    InitCommonControls();

    LoadConfig();
    GetLocalIP();
    g_sessionPass=GeneratePassword();

    // Register classes
    WNDCLASSA wc={};wc.hInstance=hInst;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);

    wc.lpfnWndProc=MainWndProc;wc.hbrBackground=nullptr;wc.lpszClassName="ARMain";RegisterClassA(&wc);
    wc.lpfnWndProc=ViewerWndProc;wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName="ARViewerWnd";RegisterClassA(&wc);
    wc.lpfnWndProc=ViewPanelProc;wc.hCursor=LoadCursor(nullptr,IDC_CROSS);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName="ARViewPanel";RegisterClassA(&wc);
    { WNDCLASSW wcw={};wcw.lpfnWndProc=UVDlgProc;wcw.hInstance=hInst;
      wcw.hCursor=LoadCursor(nullptr,IDC_ARROW);
      wcw.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wcw.lpszClassName=L"UVDlg";RegisterClassW(&wcw); }

    // Main window — no resize
    g_mainWnd=CreateWindowA("ARMain","Umbrela Viewer",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,WIN_W,WIN_H,
        nullptr,nullptr,hInst,nullptr);

    // Dark title bar
    BOOL dark=TRUE;
    DwmSetWindowAttribute(g_mainWnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));

    // Icons
    HICON ico32=CreateAppIcon(32),ico16=CreateAppIcon(16);
    SendMessage(g_mainWnd,WM_SETICON,ICON_BIG,(LPARAM)ico32);
    SendMessage(g_mainWnd,WM_SETICON,ICON_SMALL,(LPARAM)ico16);

    // Address bar edit control (floats on top of the drawn box)
    g_editRemIP=CreateWindowExA(0,"EDIT","",
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
        50,ADDR_Y+12,WIN_W-50-70,26,
        g_mainWnd,(HMENU)500,hInst,nullptr);
    SendMessageW(g_editRemIP,EM_SETCUEBANNER,TRUE,(LPARAM)L"Digite o IP remoto...");
    HFONT hfAddr=CreateFontA(15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,DEFAULT_QUALITY,DEFAULT_PITCH,"Segoe UI");
    SendMessage(g_editRemIP,WM_SETFONT,(WPARAM)hfAddr,TRUE);
    // Remove edit border so it blends into our custom drawn box
    SetWindowLongA(g_editRemIP,GWL_EXSTYLE,GetWindowLongA(g_editRemIP,GWL_EXSTYLE)&~WS_EX_CLIENTEDGE);
    SetWindowPos(g_editRemIP,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);

    // Connect button (real Win32 button, hidden — we handle via VB_CONNECT virtual button)
    g_btnConnect=CreateWindowA("BUTTON","→",WS_CHILD,0,0,1,1,g_mainWnd,(HMENU)501,hInst,nullptr);

    // Tray
    SetupTray(g_mainWnd,ico16);

    ShowWindow(g_mainWnd,nCmd);
    UpdateWindow(g_mainWnd);

    // Auto-start hosting — skip if the Windows service already owns the port
    if(!IsServiceRunning()){
        if(StartHosting()) PostMessage(g_mainWnd,WM_HOST_STATUS,1,0);
    } else {
        // Service is running; UI is read-only (viewer) — no hosting needed
        PostMessage(g_mainWnd,WM_HOST_STATUS,1,0);
    }

    // Verifica atualizações em background
    std::thread(CheckUpdateThread).detach();

    MSG m;
    while(GetMessage(&m,nullptr,0,0)){
        // Enter in address bar → connect
        if(m.message==WM_KEYDOWN&&m.wParam==VK_RETURN&&GetFocus()==g_editRemIP){
            DoConnect();continue;
        }
        // Enter in viewer chat
        if(m.message==WM_KEYDOWN&&m.wParam==VK_RETURN&&
           g_vChatIn&&GetFocus()==g_vChatIn&&!(GetAsyncKeyState(VK_SHIFT)&0x8000)){
            if(g_viewerWnd) PostMessage(g_viewerWnd,WM_COMMAND,403,0);continue;
        }
        TranslateMessage(&m);DispatchMessage(&m);
    }

    WSACleanup();
    Gdiplus::GdiplusShutdown(g_gdipToken);
    CloseHandle(mutex);
    return 0;
}
