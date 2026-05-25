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
namespace C {
    using Gc = Gdiplus::Color;
    const Gc BG      {255,18,22,38};     // main background
    const Gc TOOLBAR {255,11,14,24};     // top toolbar
    const Gc CARD    {255,25,30,50};     // hero card
    const Gc ADDRBOX {255,13,16,28};     // address bar
    const Gc BORDER  {255,44,54,84};     // card/input border
    const Gc BDRHI   {255,60,80,140};    // highlighted border
    const Gc IP      {255,77,210,255};   // IP address (bright cyan-blue)
    const Gc PASS    {255,130,225,145};  // password (soft green)
    const Gc TEXT    {255,228,232,244};  // primary text
    const Gc TEXT2   {255,100,112,150};  // secondary/hint text
    const Gc BTNBG   {255,25,100,200};   // connect button bg
    const Gc BTNHOV  {255,35,130,240};   // connect button hover
    const Gc BTNDN   {255,15,70,155};    // connect button pressed
    const Gc MENUBTN {255,255,255,255};  // hamburger lines
    const Gc GREEN   {255,80,225,145};   // status online
    const Gc YELLOW  {255,255,200,60};   // status waiting
    const Gc SEP     {255,32,38,62};     // separator line
    const Gc ICONBG  {255,30,80,180};    // monitor icon bg
    // COLORREF equivalents
    constexpr COLORREF BG_CR    = RGB(18,22,38);
    constexpr COLORREF ADDRB_CR = RGB(13,16,28);
    constexpr COLORREF TEXT_CR  = RGB(228,232,244);
    constexpr COLORREF TEXT2_CR = RGB(100,112,150);
}

// ─── Layout constants ─────────────────────────────────────────────────────────
#define WIN_W     462
#define WIN_H     412
#define TOOL_H     50   // toolbar height
#define ADDR_Y     50   // address bar top
#define ADDR_H     56   // address bar height
#define HERO_Y    108   // hero card top
#define HERO_H    230   // hero card height
#define FOOT_Y    338   // footer top
#define FOOT_H     36   // footer height

// ─── Virtual button system ────────────────────────────────────────────────────
#define VB_HAMBURGER  1
#define VB_CONNECT    2
#define VB_COPY_IP    3
#define VB_REGEN_PASS 4
#define VB_SETTINGS   5

struct VBtn { RECT rc; int id; };
static VBtn  g_btns[8] = {};
static int   g_nbtns   = 0;
static int   g_hotBtn  = 0;
static int   g_dnBtn   = 0;
static bool  g_tracking = false;

static void VBClear() { g_nbtns = 0; }
static void VBAdd(int id, int x, int y, int w, int h) {
    if (g_nbtns < 8) g_btns[g_nbtns++] = {{x,y,x+w,y+h}, id};
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

// Draw Umbrela Viewer icon (umbrella + monitor + cursor)
static void DrawUmbrelaIcon(Gdiplus::Graphics& g, float x, float y, float sz) {
    float cx = x+sz*0.5f;
    // Dark circle background
    Gdiplus::SolidBrush bg(Gdiplus::Color(255,5,10,40));
    g.FillEllipse(&bg, x, y, sz, sz);
    // Blue glow ring
    Gdiplus::Pen glow(Gdiplus::Color(120,20,80,255), sz*0.1f);
    g.DrawEllipse(&glow, x+sz*0.04f, y+sz*0.04f, sz*0.92f, sz*0.92f);
    Gdiplus::Pen ring(Gdiplus::Color(220,35,115,255), sz*0.055f);
    g.DrawEllipse(&ring, x+sz*0.06f, y+sz*0.06f, sz*0.88f, sz*0.88f);
    // Umbrella dome arc
    Gdiplus::Pen umbra(Gdiplus::Color(240,140,195,255), sz*0.075f);
    umbra.SetLineCap(Gdiplus::LineCapRound,Gdiplus::LineCapRound,Gdiplus::DashCapRound);
    g.DrawArc(&umbra, x+sz*0.15f, y+sz*0.09f, sz*0.70f, sz*0.48f, 180.f, 180.f);
    // Pole
    Gdiplus::Pen pole(Gdiplus::Color(210,120,185,255), sz*0.055f);
    pole.SetLineCap(Gdiplus::LineCapRound,Gdiplus::LineCapRound,Gdiplus::DashCapRound);
    g.DrawLine(&pole, cx, y+sz*0.33f, cx, y+sz*0.57f);
    // Monitor body
    Gdiplus::Pen mon(Gdiplus::Color(220,75,150,245), sz*0.055f);
    float mw=sz*0.44f, mh=sz*0.27f, mx=cx-mw*0.5f, my=y+sz*0.57f;
    g.DrawRectangle(&mon, mx, my, mw, mh);
    // Monitor stand
    g.DrawLine(&mon, cx, my+mh, cx, my+mh+sz*0.07f);
    g.DrawLine(&mon, cx-sz*0.11f, my+mh+sz*0.07f, cx+sz*0.11f, my+mh+sz*0.07f);
    // Cursor (triangle arrow) inside monitor
    Gdiplus::SolidBrush cur(Gdiplus::Color(240,175,220,255));
    float arx=cx-sz*0.05f, ary=my+sz*0.03f, aw=sz*0.1f, ah=sz*0.15f;
    Gdiplus::PointF pts[3]={{arx,ary},{arx,ary+ah},{arx+aw,ary+ah*0.65f}};
    g.FillPolygon(&cur, pts, 3);
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
#define WM_UPDATE_AVAILABLE  (WM_USER+20)
#define WM_HOST_STATUS       (WM_USER+10)
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
    while (std::getline(f,ln))
        if (ln.rfind("default_password=",0)==0) g_defaultPass=ln.substr(17);
}
static void SaveConfig() {
    auto path = GetConfigPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path); if(f) f<<"default_password="<<g_defaultPass<<"\n";
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
    ScreenCapture cap; if(!cap.Init()) return;
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
    if(g_sessionPass!=crd.password){
        PacketHeader deny={PacketType::CONNECT_DENY,0};
        send(sock,(char*)&deny,sizeof(deny),0); closesocket(sock); return;
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
            PacketHeader deny={PacketType::CONNECT_DENY,0};
            send(c,(char*)&deny,sizeof(deny),0);closesocket(c);continue;
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
    g_viewerWnd=CreateWindowA("ARViewerWnd","Umbrela Viewer — Sessão Remota",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1250,750,nullptr,nullptr,g_hInst,nullptr);
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

// ─── Connect ──────────────────────────────────────────────────────────────────
static void RunDlgLoop(HWND dlg, HWND editCtrl, HWND disableParent); // defined below
static void DoConnect(){
    char ip[128]={},pass[65]={};
    GetWindowTextA(g_editRemIP,ip,sizeof(ip));
    // Password is taken from last field if any (pass field not shown on main UI)
    // For now password is entered in the address bar or separately — use edit only for IP here
    // Actually user enters IP in the address bar edit and password in a dialog if needed
    // Let's prompt for password separately if needed
    if(!ip[0]){MessageBoxA(g_mainWnd,"Digite o IP do host na barra de endereço.","Umbrela Viewer",MB_OK|MB_ICONWARNING);return;}

    // Ask for password
    g_dlgPassBuf[0]=0; g_dlgResultCode=IDCANCEL; g_dlgDone=false;
    HWND dlg=CreateWindowExA(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,"UVDlg","Umbrela Viewer — Senha",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,250,200,320,148,nullptr,nullptr,g_hInst,nullptr);
    HFONT hf=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mkc=[&](const char*cls,const char*t,DWORD st,int x,int y,int w,int h,int id)->HWND{
        HWND c=CreateWindowA(cls,t,WS_CHILD|WS_VISIBLE|st,x,y,w,h,dlg,(HMENU)(intptr_t)id,g_hInst,nullptr);
        SendMessage(c,WM_SETFONT,(WPARAM)hf,TRUE);return c;
    };
    mkc("STATIC","Senha do host:",SS_LEFT,10,10,290,18,0);
    HWND editP=mkc("EDIT","",WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL,10,32,290,24,100);
    mkc("BUTTON","Cancelar",BS_PUSHBUTTON,120,72,85,26,IDCANCEL);
    mkc("BUTTON","Conectar",BS_DEFPUSHBUTTON,215,72,85,26,IDOK);
    g_activeDlgEdit=editP; SetFocus(editP);
    RunDlgLoop(dlg,editP,g_mainWnd);
    if(g_dlgResultCode!=IDOK||!g_dlgPassBuf[0]) return;

    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    DWORD to=5000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&to,sizeof(to));
    setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&to,sizeof(to));
    sockaddr_in addr={};addr.sin_family=AF_INET;addr.sin_port=htons(DEFAULT_PORT);
    if(inet_pton(AF_INET,ip,&addr.sin_addr)!=1){
        MessageBoxA(g_mainWnd,"IP inválido.","Umbrela Viewer",MB_OK|MB_ICONWARNING);closesocket(s);return;
    }
    EnableWindow(g_btnConnect,FALSE);
    if(connect(s,(sockaddr*)&addr,sizeof(addr))!=0){
        MessageBoxA(g_mainWnd,"Não foi possível conectar.","Umbrela Viewer",MB_OK|MB_ICONERROR);
        EnableWindow(g_btnConnect,TRUE);closesocket(s);return;
    }
    ConnectRequestData crd={};strncpy_s(crd.password,g_dlgPassBuf,63);
    PacketHeader req={PacketType::CONNECT_REQUEST,sizeof(crd)};
    send(s,(char*)&req,sizeof(req),0);send(s,(char*)&crd,sizeof(crd),0);
    PacketHeader resp={};
    if(recv(s,(char*)&resp,sizeof(resp),MSG_WAITALL)!=sizeof(resp)||resp.type!=PacketType::CONNECT_ACCEPT){
        MessageBoxA(g_mainWnd,resp.type==PacketType::CONNECT_DENY?"Senha incorreta.":"Sem resposta.","Umbrela Viewer",MB_OK|MB_ICONERROR);
        EnableWindow(g_btnConnect,TRUE);closesocket(s);return;
    }
    DWORD noT=0;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&noT,sizeof(noT));
    setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&noT,sizeof(noT));
    g_viewSock=s;g_viewRunning=g_viewConnected=true;
    CreateViewerWindow();
    std::thread(ViewRecvThread).detach();
    PostMessage(g_viewerWnd,WM_VIEW_CONNECTED,0,0);
}

// ─── Self-install (C:\Program Files, todos os usuários, UAC) ─────────────────
static std::string GetInstallDir() {
    char buf[MAX_PATH];
    SHGetFolderPathA(nullptr,CSIDL_PROGRAM_FILES,nullptr,0,buf);
    return std::string(buf)+"\\UmbrelaViewer";
}
static std::string GetInstallExe() { return GetInstallDir()+"\\UmbrelaViewer.exe"; }

static bool IsRunningFromInstallDir() {
    char my[MAX_PATH]; GetModuleFileNameA(nullptr,my,MAX_PATH);
    return _stricmp(my,GetInstallExe().c_str())==0;
}

static bool IsRunningAsAdmin() {
    BOOL isAdmin=FALSE;
    PSID adminGroup=nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth=SECURITY_NT_AUTHORITY;
    if(AllocateAndInitializeSid(&ntAuth,2,SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&adminGroup)){
        CheckTokenMembership(nullptr,adminGroup,&isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin==TRUE;
}

static bool DoInstall() {
    // Create install dir and copy exe
    std::error_code ec;
    std::filesystem::create_directories(GetInstallDir(),ec);
    if(ec) return false;
    char src[MAX_PATH]; GetModuleFileNameA(nullptr,src,MAX_PATH);
    if(!CopyFileA(src,GetInstallExe().c_str(),FALSE)) return false;

    // Autorun — HKLM para todos os usuários
    HKEY key;
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){
        std::string val="\""+GetInstallExe()+"\"";
        RegSetValueExA(key,"UmbrelaViewer",0,REG_SZ,(BYTE*)val.c_str(),(DWORD)val.size()+1);
        RegCloseKey(key);
    }

    // Atalho na Área de Trabalho pública (todos os usuários)
    CoInitialize(nullptr);
    IShellLinkA*psl=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkA,(void**)&psl))){
        psl->SetPath(GetInstallExe().c_str());
        psl->SetDescription("Umbrela Viewer");
        psl->SetIconLocation(GetInstallExe().c_str(),0);
        IPersistFile*ppf=nullptr;
        if(SUCCEEDED(psl->QueryInterface(IID_IPersistFile,(void**)&ppf))){
            char desk[MAX_PATH];
            SHGetFolderPathA(nullptr,CSIDL_COMMON_DESKTOPDIRECTORY,nullptr,0,desk);
            std::wstring lnk=std::filesystem::path(desk).append("UmbrelaViewer.lnk").wstring();
            ppf->Save(lnk.c_str(),TRUE); ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
    return true;
}

// ─── App icon — umbrella + monitor + cursor ──────────────────────────────────
static HICON CreateAppIcon(int sz){
    HDC dc=GetDC(nullptr),mdc=CreateCompatibleDC(dc);
    HBITMAP bmp=CreateCompatibleBitmap(dc,sz,sz);
    HBITMAP ob=(HBITMAP)SelectObject(mdc,bmp);
    Gdiplus::Graphics g(mdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float s=(float)sz, cx=s*0.5f;

    // Black fill
    Gdiplus::SolidBrush bkg(Gdiplus::Color(255,0,0,0));
    g.FillRectangle(&bkg,0.f,0.f,s,s);

    // Dark circle background
    Gdiplus::SolidBrush bgc(Gdiplus::Color(255,5,10,40));
    g.FillEllipse(&bgc,s*0.03f,s*0.03f,s*0.94f,s*0.94f);

    // Outer blue glow
    Gdiplus::Pen glow(Gdiplus::Color(90,20,70,255),s*0.10f);
    g.DrawEllipse(&glow,s*0.04f,s*0.04f,s*0.92f,s*0.92f);

    // Main ring
    Gdiplus::Pen ring(Gdiplus::Color(230,30,110,255),s*0.048f);
    g.DrawEllipse(&ring,s*0.06f,s*0.06f,s*0.88f,s*0.88f);

    // Inner ring highlight
    Gdiplus::Pen rhi(Gdiplus::Color(140,100,170,255),s*0.022f);
    g.DrawEllipse(&rhi,s*0.09f,s*0.09f,s*0.82f,s*0.82f);

    // Umbrella dome arc
    Gdiplus::Pen udome(Gdiplus::Color(245,150,205,255),s*0.072f);
    udome.SetLineCap(Gdiplus::LineCapRound,Gdiplus::LineCapRound,Gdiplus::DashCapRound);
    g.DrawArc(&udome,s*0.16f,s*0.10f,s*0.68f,s*0.46f,180.f,180.f);

    // Umbrella spokes (subtle, lighter)
    Gdiplus::Pen spoke(Gdiplus::Color(120,110,170,255),s*0.028f);
    spoke.SetLineCap(Gdiplus::LineCapRound,Gdiplus::LineCapRound,Gdiplus::DashCapRound);
    g.DrawLine(&spoke,cx,s*0.33f,cx-s*0.21f,s*0.53f);
    g.DrawLine(&spoke,cx,s*0.33f,cx+s*0.21f,s*0.53f);

    // Pole
    Gdiplus::Pen pole(Gdiplus::Color(225,125,188,255),s*0.052f);
    pole.SetLineCap(Gdiplus::LineCapRound,Gdiplus::LineCapRound,Gdiplus::DashCapRound);
    g.DrawLine(&pole,cx,s*0.33f,cx,s*0.57f);

    // Monitor body
    Gdiplus::Pen monp(Gdiplus::Color(230,75,150,245),s*0.05f);
    float mw=s*0.42f,mh=s*0.26f,mx=cx-mw*0.5f,my=s*0.57f;
    g.DrawRectangle(&monp,mx,my,mw,mh);

    // Screen fill
    Gdiplus::SolidBrush scr(Gdiplus::Color(70,20,55,150));
    g.FillRectangle(&scr,mx+s*0.02f,my+s*0.02f,mw-s*0.04f,mh-s*0.04f);

    // Monitor stand
    g.DrawLine(&monp,cx,my+mh,cx,my+mh+s*0.065f);
    g.DrawLine(&monp,cx-s*0.10f,my+mh+s*0.065f,cx+s*0.10f,my+mh+s*0.065f);

    // Cursor arrow (triangle) in monitor
    Gdiplus::SolidBrush cur(Gdiplus::Color(245,180,225,255));
    float arx=cx-s*0.055f,ary=my+s*0.03f,aw=s*0.10f,ah=s*0.15f;
    Gdiplus::PointF pts[3]={{arx,ary},{arx,ary+ah},{arx+aw,ary+ah*0.64f}};
    g.FillPolygon(&cur,pts,3);

    SelectObject(mdc,ob);DeleteDC(mdc);ReleaseDC(nullptr,dc);
    HBITMAP mask=CreateBitmap(sz,sz,1,1,nullptr);
    ICONINFO ii={};ii.fIcon=TRUE;ii.hbmColor=bmp;ii.hbmMask=mask;
    HICON ico=CreateIconIndirect(&ii);DeleteObject(bmp);DeleteObject(mask);
    return ico;
}

// ─── Main window paint ────────────────────────────────────────────────────────
static void PaintMain(HWND hw, HDC hdc){
    RECT wrc; GetClientRect(hw,&wrc);
    int W=wrc.right;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // ── Reset virtual buttons ─────────────────────────────────────────────
    VBClear();

    // ── [1] Toolbar (0..TOOL_H) ───────────────────────────────────────────
    Gdiplus::SolidBrush tbBrush(C::TOOLBAR);
    g.FillRectangle(&tbBrush,0,0,W,TOOL_H);

    // Umbrela Viewer logo in toolbar
    DrawUmbrelaIcon(g,12.f,9.f,30.f);

    // "UMBRELA" + "VIEWER" title
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font fntBold(&ff,15,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::Font fntNorm(&ff,13,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush wBrush(C::TEXT);
    Gdiplus::SolidBrush w2Brush(C::TEXT2);
    g.DrawString(L"UMBRELA",-1,&fntBold,Gdiplus::PointF(50.f,11.f),&wBrush);
    g.DrawString(L"VIEWER",-1,&fntNorm,Gdiplus::PointF(50.f,30.f),&w2Brush);

    // Hamburger button
    bool hambHot = (g_hotBtn==VB_HAMBURGER);
    if(hambHot||g_dnBtn==VB_HAMBURGER){
        Gdiplus::SolidBrush hbg(hambHot?Gdiplus::Color(40,255,255,255):Gdiplus::Color(20,255,255,255));
        FillRR(g,(float)(W-44),(float)7,36.f,36.f,6.f,hbg);
    }
    DrawHamburger(g,W-26,25,hambHot);
    VBAdd(VB_HAMBURGER,W-44,7,36,36);

    // ── [2] Address bar (ADDR_Y..ADDR_Y+ADDR_H) ──────────────────────────
    Gdiplus::SolidBrush bgBrush(C::BG);
    g.FillRectangle(&bgBrush,0.f,(float)ADDR_Y,(float)W,(float)ADDR_H);

    // Address box (drawn, edit control floats inside)
    bool addrFocus = (GetFocus()==g_editRemIP);
    Gdiplus::SolidBrush addrBg(C::ADDRBOX);
    FillRR(g,12.f,ADDR_Y+10.f,(float)(W-24),36.f,6.f,addrBg);
    Gdiplus::Pen borderPen(addrFocus?C::BDRHI:C::BORDER,1.2f);
    StrokeRR(g,12.f,ADDR_Y+10.f,(float)(W-24),36.f,6.f,borderPen);

    // Monitor/screen icon inside address box
    Gdiplus::SolidBrush icoBrush(C::TEXT2);
    g.DrawString(L"🖥",-1,&fntNorm,Gdiplus::PointF(20.f,ADDR_Y+16.f),&icoBrush);

    // Connect arrow button (right side of address box)
    bool connHot=(g_hotBtn==VB_CONNECT),connDn=(g_dnBtn==VB_CONNECT);
    Gdiplus::Color btnCol=connDn?C::BTNDN:(connHot?C::BTNHOV:C::BTNBG);
    Gdiplus::SolidBrush btnBrush(btnCol);
    FillRR(g,(float)(W-58),ADDR_Y+11.f,44.f,34.f,5.f,btnBrush);

    Gdiplus::Font fntBtn(&ff,15,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush arrowBrush(Gdiplus::Color(255,255,255,255));
    Gdiplus::StringFormat sfC;sfC.SetAlignment(Gdiplus::StringAlignmentCenter);sfC.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF btnRect((float)(W-58),ADDR_Y+11.f,44.f,34.f);
    g.DrawString(L"→",-1,&fntBtn,btnRect,&sfC,&arrowBrush);
    VBAdd(VB_CONNECT,W-58,ADDR_Y+11,44,34);

    // ── [3] Thin separator ───────────────────────────────────────────────
    Gdiplus::Pen sepPen(C::SEP,1.0f);
    g.DrawLine(&sepPen,0.f,(float)HERO_Y,(float)W,(float)HERO_Y);

    // ── [4] Hero card ────────────────────────────────────────────────────
    g.FillRectangle(&bgBrush,0.f,(float)HERO_Y,(float)W,(float)HERO_H);

    // Card inset
    Gdiplus::SolidBrush cardBrush(C::CARD);
    FillRR(g,14.f,(float)(HERO_Y+12),(float)(W-28),HERO_H-22.f,8.f,cardBrush);
    Gdiplus::Pen cardPen(C::BORDER,1.0f);
    StrokeRR(g,14.f,(float)(HERO_Y+12),(float)(W-28),HERO_H-22.f,8.f,cardPen);

    // "Este dispositivo"
    Gdiplus::Font fntSm(&ff,11,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    g.DrawString(L"Este dispositivo",-1,&fntSm,Gdiplus::PointF((float)(W/2-56),(float)(HERO_Y+22)),&w2Brush);

    // ── IP address (large, colored) ───────────────────────────────────────
    wchar_t wipW[64]; MultiByteToWideChar(CP_ACP,0,g_localIP,-1,wipW,64);
    Gdiplus::Font fntIP(&ff,32,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush ipBrush(C::IP);
    // Measure and center
    Gdiplus::RectF ipMeasure;
    g.MeasureString(wipW,-1,&fntIP,Gdiplus::PointF(0,0),&ipMeasure);
    float ipX = (W-ipMeasure.Width)/2.f;
    g.DrawString(wipW,-1,&fntIP,Gdiplus::PointF(ipX,(float)(HERO_Y+40)),&ipBrush);

    // Copy IP button (small, subtle)
    bool cpHot=(g_hotBtn==VB_COPY_IP);
    Gdiplus::Color cpCol=cpHot?Gdiplus::Color(50,255,255,255):Gdiplus::Color(20,255,255,255);
    Gdiplus::SolidBrush cpBrush(cpCol);
    float cpX=ipX+ipMeasure.Width+6.f;
    float cpY=(float)(HERO_Y+45);
    FillRR(g,cpX,cpY,28.f,22.f,4.f,cpBrush);
    Gdiplus::Font fntTiny(&ff,12,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    g.DrawString(L"⧉",-1,&fntTiny,Gdiplus::PointF(cpX+5.f,cpY+3.f),&w2Brush);
    VBAdd(VB_COPY_IP,(int)cpX,(int)cpY,28,22);

    // ── Separator line ───────────────────────────────────────────────────
    Gdiplus::Pen divPen(C::SEP,1.0f);
    g.DrawLine(&divPen,28.f,(float)(HERO_Y+100),(float)(W-28),(float)(HERO_Y+100));

    // ── Password row ─────────────────────────────────────────────────────
    float passRowY = (float)(HERO_Y+114);

    // Lock icon
    DrawLock(g,28.f,passRowY+1,16.f,C::TEXT2);

    // "Senha:" label
    g.DrawString(L"Senha:",-1,&fntSm,Gdiplus::PointF(50.f,passRowY+2),&w2Brush);

    // Password value
    wchar_t wpass[16]={};
    MultiByteToWideChar(CP_ACP,0,g_sessionPass.c_str(),-1,wpass,16);
    Gdiplus::Font fntPass(&ff,22,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush passBrush(C::PASS);
    g.DrawString(wpass,-1,&fntPass,Gdiplus::PointF(100.f,passRowY-2),&passBrush);

    // Regen button
    bool rgHot=(g_hotBtn==VB_REGEN_PASS);
    Gdiplus::Color rgCol=rgHot?Gdiplus::Color(50,255,255,255):Gdiplus::Color(25,255,255,255);
    Gdiplus::SolidBrush rgBrush(rgCol);
    float rgX=(float)(W-110), rgY=passRowY;
    FillRR(g,rgX,rgY,44.f,24.f,5.f,rgBrush);
    g.DrawString(L"↻ Nova",-1,&fntTiny,Gdiplus::PointF(rgX+4.f,rgY+5.f),&w2Brush);
    VBAdd(VB_REGEN_PASS,(int)rgX,(int)rgY,44,24);

    // Settings button
    bool stHot=(g_hotBtn==VB_SETTINGS);
    Gdiplus::Color stCol=stHot?Gdiplus::Color(50,255,255,255):Gdiplus::Color(25,255,255,255);
    Gdiplus::SolidBrush stBrush(stCol);
    float stX=(float)(W-62), stY=passRowY;
    FillRR(g,stX,stY,44.f,24.f,5.f,stBrush);
    g.DrawString(L"⚙ Menu",-1,&fntTiny,Gdiplus::PointF(stX+3.f,stY+5.f),&w2Brush);
    VBAdd(VB_SETTINGS,(int)stX,(int)stY,44,24);

    // ── Status row ───────────────────────────────────────────────────────
    float statusY=(float)(HERO_Y+155);
    Gdiplus::Color dotColor = g_srvConnected?C::GREEN:(g_srvRunning?C::YELLOW:C::TEXT2);
    DrawCircle(g,30.f,statusY+7,5.f,dotColor);

    const wchar_t* statusText = g_srvConnected?L"Conectado — sessão ativa":
                                 g_srvRunning  ?L"Aguardando conexão...":L"Servidor parado";
    g.DrawString(statusText,-1,&fntSm,Gdiplus::PointF(44.f,statusY+1),&w2Brush);

    // ── [5] Footer ───────────────────────────────────────────────────────
    g.FillRectangle(&bgBrush,0.f,(float)FOOT_Y,(float)W,(float)FOOT_H);
    Gdiplus::Pen footPen(C::SEP,1.0f);
    g.DrawLine(&footPen,0.f,(float)FOOT_Y,(float)W,(float)FOOT_Y);
    Gdiplus::Font fntFoot(&ff,10,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush footBrush(C::TEXT2);
    g.DrawString(L"Compartilhe seu IP e senha com o técnico para iniciar a sessão.",
                 -1,&fntFoot,Gdiplus::PointF(14.f,(float)(FOOT_Y+11)),&footBrush);
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
        }
        return 0;
    }
    if(msg == WM_CLOSE) {
        g_dlgResultCode = IDCANCEL; g_dlgDone = true;
        DestroyWindow(hw); return 0;
    }
    return DefWindowProcA(hw, msg, wp, lp);
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
    AppendMenuA(m,MF_STRING,IDM_REGEN,       "↻  Gerar nova senha");
    AppendMenuA(m,MF_STRING,IDM_SETDEFAULT,  "✎  Definir senha padrão...");
    if(!g_defaultPass.empty())
        AppendMenuA(m,MF_STRING,IDM_CLEARDEFAULT,"✕  Remover senha padrão");
    AppendMenuA(m,MF_SEPARATOR,0,nullptr);
    AppendMenuA(m,MF_STRING,IDM_ABOUT,       "ℹ  Sobre o Umbrela Viewer");
    AppendMenuA(m,MF_SEPARATOR,0,nullptr);
    AppendMenuA(m,MF_STRING,IDM_EXIT,        "✕  Finalizar");

    // Position below hamburger button
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hw);
    TrackPopupMenu(m,TPM_RIGHTBUTTON|TPM_RIGHTALIGN,pt.x,pt.y,0,hw,nullptr);
    DestroyMenu(m);
}

static void ShowSetDefaultPassDialog(HWND parent){
    strncpy_s(g_dlgPassBuf, g_defaultPass.c_str(), 64);
    g_dlgResultCode=IDCANCEL; g_dlgDone=false;
    HWND dlg=CreateWindowExA(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,"UVDlg","Umbrela Viewer — Definir Senha Padrão",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,220,180,360,178,nullptr,nullptr,g_hInst,nullptr);
    HFONT hf=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mkc=[&](const char*cls,const char*t,DWORD st,int x,int y,int w,int h,int id)->HWND{
        HWND c=CreateWindowA(cls,t,WS_CHILD|WS_VISIBLE|st,x,y,w,h,dlg,(HMENU)(intptr_t)id,g_hInst,nullptr);
        SendMessage(c,WM_SETFONT,(WPARAM)hf,TRUE);return c;
    };
    mkc("STATIC","Senha padrão (deixe vazio para gerar automaticamente a cada início):",SS_LEFT,10,10,330,30,0);
    HWND editP=mkc("EDIT",g_dlgPassBuf,WS_BORDER|ES_AUTOHSCROLL,10,44,330,26,100);
    SendMessage(editP,EM_SETLIMITTEXT,63,0);
    mkc("BUTTON","Cancelar",BS_PUSHBUTTON,155,95,85,28,IDCANCEL);
    mkc("BUTTON","Salvar",BS_DEFPUSHBUTTON,250,95,85,28,IDOK);
    g_activeDlgEdit=editP; SetFocus(editP);
    RunDlgLoop(dlg,editP,parent);
    if(g_dlgResultCode==IDOK){
        g_defaultPass=g_dlgPassBuf;
        SaveConfig();
        g_sessionPass=g_defaultPass.empty()?GeneratePassword():g_defaultPass;
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
                g_sessionPass=g_defaultPass.empty()?GeneratePassword():g_defaultPass;
                InvalidateRect(hw,nullptr,FALSE);
                break;
            case VB_SETTINGS: ShowSettingsMenu(hw); break;
            }
        }
        return 0;
    }

    case WM_UPDATE_AVAILABLE:{
        int r=MessageBoxA(hw,
            "Nova versão do Umbrela Viewer disponível!\n\n"
            "Deseja atualizar agora? O programa será reiniciado.",
            "Umbrela Viewer — Atualização",MB_YESNO|MB_ICONINFORMATION);
        if(r==IDYES) std::thread(DoUpdateDownload).detach();
        return 0;
    }

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
            g_sessionPass=g_defaultPass.empty()?GeneratePassword():g_defaultPass;
            InvalidateRect(hw,nullptr,FALSE); break;
        case IDM_SETDEFAULT:  ShowSetDefaultPassDialog(hw); break;
        case IDM_CLEARDEFAULT:
            g_defaultPass.clear();SaveConfig();
            g_sessionPass=GeneratePassword();
            InvalidateRect(hw,nullptr,FALSE); break;
        case IDM_ABOUT:
            MessageBoxA(hw,"Umbrela Viewer v1.0\n\nSolução de acesso remoto seguro\npara suporte técnico interno.\n\nDesenvolvido por Jean Silva",
                "Sobre o Umbrela Viewer",MB_OK|MB_ICONINFORMATION); break;
        case IDM_SHOW: ShowWindow(hw,SW_RESTORE);SetForegroundWindow(hw); break;
        case IDM_EXIT:
            Shell_NotifyIconA(NIM_DELETE,&g_nid);PostQuitMessage(0); break;
        }
        return 0;

    case WM_TRAYICON:
        if(lp==WM_RBUTTONUP){
            HMENU m=CreatePopupMenu();
            AppendMenuA(m,MF_STRING,IDM_SHOW,"Mostrar Umbrela Viewer");
            AppendMenuA(m,MF_SEPARATOR,0,nullptr);
            AppendMenuA(m,MF_STRING,IDM_REGEN,"Gerar nova senha");
            AppendMenuA(m,MF_STRING,IDM_SETDEFAULT,"Definir senha padrão...");
            AppendMenuA(m,MF_SEPARATOR,0,nullptr);
            AppendMenuA(m,MF_STRING,IDM_EXIT,"Finalizar");
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
            MessageBoxA(nullptr,
                "Atualização cancelada (UAC negado ou erro).",
                "Umbrela Viewer",MB_OK|MB_ICONWARNING);
        }
    } else {
        MessageBoxA(nullptr,
            "Não foi possível baixar a atualização.\nVerifique a conexão e tente mais tarde.",
            "Umbrela Viewer",MB_OK|MB_ICONERROR);
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

    // Instância elevada responsável pela instalação (lançada pelo processo normal)
    if(lpCmdLine && strstr(lpCmdLine,"/install")){
        if(DoInstall()){
            ShellExecuteA(nullptr,"open",GetInstallExe().c_str(),nullptr,nullptr,SW_SHOW);
        } else {
            MessageBoxA(nullptr,
                "Falha ao instalar em C:\\Program Files\\UmbrelaViewer.\n"
                "Verifique as permissões e tente novamente.",
                "Umbrela Viewer",MB_OK|MB_ICONERROR);
        }
        return 0;
    }

    // Instância única
    HANDLE mutex=CreateMutexA(nullptr,TRUE,"UmbrelaViewerMutex_3f7b");
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        HWND ex=FindWindowA("ARMain",nullptr);
        if(ex){ShowWindow(ex,SW_RESTORE);SetForegroundWindow(ex);}
        return 0;
    }

    Gdiplus::GdiplusStartup(&g_gdipToken,&g_gdip,nullptr);
    WSADATA wsa;WSAStartup(MAKEWORD(2,2),&wsa);
    InitCommonControls();

    if(!IsRunningFromInstallDir()){
        int r=MessageBoxA(nullptr,
            "Umbrela Viewer não está instalado.\n\nDeseja instalar agora?\n\n"
            "  • Instalado em C:\\Program Files\\UmbrelaViewer\n"
            "  • Inicia automaticamente com o Windows (todos os usuários)\n"
            "  • Atalho na Área de Trabalho (todos os usuários)",
            "Umbrela Viewer — Instalação",MB_YESNO|MB_ICONQUESTION);
        if(r==IDYES){
            if(IsRunningAsAdmin()){
                // Já é admin — instala diretamente
                if(DoInstall()){
                    ShellExecuteA(nullptr,"open",GetInstallExe().c_str(),nullptr,nullptr,SW_SHOW);
                    return 0;
                }
            } else {
                // Pede elevação (UAC) — relança com /install
                char exePath[MAX_PATH];
                GetModuleFileNameA(nullptr,exePath,MAX_PATH);
                SHELLEXECUTEINFOA sei={};
                sei.cbSize=sizeof(sei);
                sei.lpVerb="runas";
                sei.lpFile=exePath;
                sei.lpParameters="/install";
                sei.nShow=SW_SHOW;
                if(ShellExecuteExA(&sei)){
                    return 0; // instância elevada cuida do resto
                }
                // Usuário negou UAC — continua rodando sem instalar
            }
        }
    }

    LoadConfig();
    GetLocalIP();
    g_sessionPass=g_defaultPass.empty()?GeneratePassword():g_defaultPass;

    // Register classes
    WNDCLASSA wc={};wc.hInstance=hInst;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);

    wc.lpfnWndProc=MainWndProc;wc.hbrBackground=nullptr;wc.lpszClassName="ARMain";RegisterClassA(&wc);
    wc.lpfnWndProc=ViewerWndProc;wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName="ARViewerWnd";RegisterClassA(&wc);
    wc.lpfnWndProc=ViewPanelProc;wc.hCursor=LoadCursor(nullptr,IDC_CROSS);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName="ARViewPanel";RegisterClassA(&wc);
    wc.lpfnWndProc=UVDlgProc;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName="UVDlg";RegisterClassA(&wc);

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
        44,ADDR_Y+14,(WIN_W-16)-44-52,28,  // inside the address box, after icon, before connect btn
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

    // Auto-start hosting
    if(StartHosting()) PostMessage(g_mainWnd,WM_HOST_STATUS,1,0);

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
