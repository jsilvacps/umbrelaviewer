#include "screencap.h"
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")

bool ScreenCapture::Init() {
    m_w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    m_screenDC = GetDC(nullptr);
    m_memDC    = CreateCompatibleDC(m_screenDC);
    m_bmp      = CreateCompatibleBitmap(m_screenDC, m_w, m_h);
    m_oldBmp   = (HBITMAP)SelectObject(m_memDC, m_bmp);

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

bool ScreenCapture::CaptureToJPEG(std::vector<uint8_t>& out, int quality) {
    if (!m_ready) return false;

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
