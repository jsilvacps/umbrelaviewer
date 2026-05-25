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

    bool Init();
    void Shutdown();
    bool CaptureToJPEG(std::vector<uint8_t>& out, int quality = 65);

    int GetWidth()  const { return m_w; }
    int GetHeight() const { return m_h; }

private:
    int     m_w = 0, m_h = 0;
    HDC     m_screenDC  = nullptr;
    HDC     m_memDC     = nullptr;
    HBITMAP m_bmp       = nullptr;
    HBITMAP m_oldBmp    = nullptr;
    CLSID   m_jpegClsid = {};
    bool    m_ready     = false;

    bool FindJpegClsid();
};
