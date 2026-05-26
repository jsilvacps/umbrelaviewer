#include "screencap.h"
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")

// ─────────────────────────────────────────────────────────────────────────────
//  Normal (desktop app) mode
// ─────────────────────────────────────────────────────────────────────────────
bool ScreenCapture::Init() {
    m_w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (!m_serviceMode) {
        // Standard: grab the DC from the screen directly
        m_screenDC = GetDC(nullptr);
        m_memDC    = CreateCompatibleDC(m_screenDC);
        m_bmp      = CreateCompatibleBitmap(m_screenDC, m_w, m_h);
        m_oldBmp   = (HBITMAP)SelectObject(m_memDC, m_bmp);
    } else {
        // Service mode: WinSta0 must already be set on the process before Init()
        // We only create the off-screen bitmap here; the source DC is opened
        // per-frame in CaptureToJPEG so we can follow desktop switches.
        HDC refDC  = GetDC(nullptr);   // valid because process is on WinSta0
        m_memDC    = CreateCompatibleDC(refDC);
        m_bmp      = CreateCompatibleBitmap(refDC, m_w, m_h);
        m_oldBmp   = (HBITMAP)SelectObject(m_memDC, m_bmp);
        ReleaseDC(nullptr, refDC);
        // m_screenDC stays nullptr; we open per-frame in CaptureToJPEG
    }

    m_ready = FindJpegClsid();
    return m_ready;
}

void ScreenCapture::Shutdown() {
    if (m_oldBmp && m_memDC)   SelectObject(m_memDC, m_oldBmp);
    if (m_bmp)                  DeleteObject(m_bmp);
    if (m_memDC)                DeleteDC(m_memDC);
    if (m_screenDC)             ReleaseDC(nullptr, m_screenDC);
    m_bmp = nullptr; m_memDC = nullptr; m_screenDC = nullptr; m_ready = false;
}

bool ScreenCapture::FindJpegClsid() {
    UINT num = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&num, &sz);
    if (!sz) return false;
    auto* c = (Gdiplus::ImageCodecInfo*)malloc(sz);
    if (!c) return false;
    Gdiplus::GetImageEncoders(num, sz, c);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(c[i].MimeType, L"image/jpeg") == 0) {
            m_jpegClsid = c[i].Clsid; free(c); return true;
        }
    }
    free(c); return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Service-mode helper: open whichever desktop is currently receiving input.
//  Tries OpenInputDesktop first; if that fails (e.g. secure desktop / Winlogon)
//  falls back to opening "Winlogon" by name.
// ─────────────────────────────────────────────────────────────────────────────
HDESK ScreenCapture::OpenActiveDesktop() {
    DWORD access = DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS |
                   DESKTOP_ENUMERATE   | DESKTOP_SWITCHDESKTOP;
    HDESK hd = OpenInputDesktop(0, FALSE, access);
    if (!hd)
        hd = OpenDesktopW(L"Winlogon", 0, FALSE, access);
    return hd;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Capture
// ─────────────────────────────────────────────────────────────────────────────
bool ScreenCapture::CaptureToJPEG(std::vector<uint8_t>& out, int quality) {
    if (!m_ready) return false;

    if (!m_serviceMode) {
        // ── Normal mode ────────────────────────────────────────────────────
        int ox = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int oy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        BitBlt(m_memDC, 0, 0, m_w, m_h, m_screenDC, ox, oy, SRCCOPY | CAPTUREBLT);

        // Draw cursor overlay
        CURSORINFO ci = { sizeof(ci) };
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
            ICONINFO ii = {};
            if (GetIconInfo(ci.hCursor, &ii)) {
                DrawIconEx(m_memDC,
                    ci.ptScreenPos.x - ox - (int)ii.xHotspot,
                    ci.ptScreenPos.y - oy - (int)ii.yHotspot,
                    ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
                if (ii.hbmMask)  DeleteObject(ii.hbmMask);
            }
        }
    } else {
        // ── Service mode ───────────────────────────────────────────────────
        // 1. Find out which desktop is active right now.
        HDESK hDesk = OpenActiveDesktop();
        if (!hDesk) return false;

        // 2. Bind this thread to that desktop so GDI calls work on it.
        HDESK hPrev = GetThreadDesktop(GetCurrentThreadId());
        SetThreadDesktop(hDesk);

        // 3. Open a screen DC on the (now-current) desktop and blit.
        HDC srcDC = GetDC(nullptr);
        int ox = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int oy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        BitBlt(m_memDC, 0, 0, m_w, m_h, srcDC, ox, oy, SRCCOPY | CAPTUREBLT);

        // 4. Draw cursor overlay (best-effort; may not be visible on Winlogon).
        CURSORINFO ci = { sizeof(ci) };
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
            ICONINFO ii = {};
            if (GetIconInfo(ci.hCursor, &ii)) {
                DrawIconEx(m_memDC,
                    ci.ptScreenPos.x - ox - (int)ii.xHotspot,
                    ci.ptScreenPos.y - oy - (int)ii.yHotspot,
                    ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
                if (ii.hbmMask)  DeleteObject(ii.hbmMask);
            }
        }

        // 5. Restore thread desktop and release resources.
        ReleaseDC(nullptr, srcDC);
        if (hPrev) SetThreadDesktop(hPrev);
        CloseDesktop(hDesk);
    }

    // ── GDI+ JPEG encode (identical for both modes) ───────────────────────
    Gdiplus::Bitmap bmp(m_bmp, nullptr);
    Gdiplus::EncoderParameters ep;
    ep.Count = 1;
    ep.Parameter[0].Guid            = Gdiplus::EncoderQuality;
    ep.Parameter[0].Type            = Gdiplus::EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues  = 1;
    ULONG q = (ULONG)quality;
    ep.Parameter[0].Value = &q;

    IStream* s = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &s))) return false;

    if (bmp.Save(s, &m_jpegClsid, &ep) != Gdiplus::Ok) { s->Release(); return false; }

    STATSTG st = {};
    s->Stat(&st, STATFLAG_NONAME);
    LARGE_INTEGER z = {};
    s->Seek(z, STREAM_SEEK_SET, nullptr);
    out.resize(st.cbSize.LowPart);
    ULONG n = 0;
    s->Read(out.data(), st.cbSize.LowPart, &n);
    s->Release();
    out.resize(n);
    return n > 0;
}
