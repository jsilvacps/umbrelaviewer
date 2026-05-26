#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#include <cstdint>

class ScreenCapture {
public:
    ScreenCapture()  = default;
    ~ScreenCapture() { Shutdown(); }

    // Call before Init() when running as a Windows service (SYSTEM context).
    // The service must have already called SetProcessWindowStation(WinSta0).
    void SetServiceMode(bool svc) { m_serviceMode = svc; }

    bool Init();
    void Shutdown();
    bool CaptureToJPEG(std::vector<uint8_t>& out, int quality = 65);

    int GetWidth()  const { return m_w; }
    int GetHeight() const { return m_h; }

private:
    int     m_w = 0, m_h = 0;
    HDC     m_screenDC  = nullptr;  // normal mode only
    HDC     m_memDC     = nullptr;
    HBITMAP m_bmp       = nullptr;
    HBITMAP m_oldBmp    = nullptr;
    CLSID   m_jpegClsid = {};
    bool    m_ready     = false;
    bool    m_serviceMode = false;

    bool FindJpegClsid();
    // Service-mode helpers
    HDESK OpenActiveDesktop();
};
