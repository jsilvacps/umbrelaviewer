/*
 * gen_icon.cpp  —  Build-time tool: renders the Umbrela Viewer logo in
 * 16×16, 32×32, 48×48 and 256×256 and packs them into a PNG-in-ICO file.
 *
 * Usage: gen_icon.exe <output.ico>
 *
 * Must be compiled with /subsystem:console (CMake default for non-WIN32 targets).
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;

// ─── GDI+ helpers (mirror of main.cpp) ───────────────────────────────────────

static void FillRR(Graphics& g, float x, float y, float w, float h,
                   float r, const Brush& b) {
    GraphicsPath p;
    p.AddArc(x,       y,       r*2,r*2, 180, 90);
    p.AddArc(x+w-r*2, y,       r*2,r*2, 270, 90);
    p.AddArc(x+w-r*2, y+h-r*2, r*2,r*2,   0, 90);
    p.AddArc(x,       y+h-r*2, r*2,r*2,  90, 90);
    p.CloseFigure();
    g.FillPath(&b, &p);
}

static void StrokeRR(Graphics& g, float x, float y, float w, float h,
                     float r, const Pen& pen) {
    GraphicsPath p;
    p.AddArc(x,       y,       r*2,r*2, 180, 90);
    p.AddArc(x+w-r*2, y,       r*2,r*2, 270, 90);
    p.AddArc(x+w-r*2, y+h-r*2, r*2,r*2,   0, 90);
    p.AddArc(x,       y+h-r*2, r*2,r*2,  90, 90);
    p.CloseFigure();
    g.DrawPath(&pen, &p);
}

// Exact copy of DrawIconContent(g, x, y, sz, withCircle=true) from main.cpp
static void DrawLogo(Graphics& g, float x, float y, float sz) {
    float cx = x + sz * 0.5f, cy = y + sz * 0.5f;

    // ── Circular badge ────────────────────────────────────────────────────
    SolidBrush bgBr(Color(255, 5, 8, 22));
    g.FillEllipse(&bgBr, x, y, sz, sz);

    Pen glow(Color(48, 0, 100, 255), sz * 0.13f);
    g.DrawEllipse(&glow, x + sz*0.03f, y + sz*0.03f, sz*0.94f, sz*0.94f);

    Pen ring(Color(185, 0, 207, 255), sz * 0.044f);
    g.DrawEllipse(&ring, x + sz*0.075f, y + sz*0.075f, sz*0.85f, sz*0.85f);

    // ── Monitor ───────────────────────────────────────────────────────────
    float mw = sz * 0.58f, mh = mw * 0.67f;
    float mx = cx - mw * 0.5f + sz * 0.04f;
    float my = cy - mh * 0.5f + sz * (-0.04f);

    Pen monGlow(Color(50, 0, 207, 255), sz * 0.060f);
    StrokeRR(g, mx - sz*0.024f, my - sz*0.024f,
               mw + sz*0.048f, mh + sz*0.048f, sz*0.07f, monGlow);

    SolidBrush monFill(Color(255, 7, 12, 42));
    FillRR(g, mx, my, mw, mh, sz * 0.052f, monFill);

    {
        LinearGradientBrush borderBr(
            PointF(mx, my), PointF(mx, my + mh),
            Color(215, 0, 207, 255), Color(175, 0, 123, 255));
        Pen borderPen(&borderBr, sz * 0.030f);
        StrokeRR(g, mx, my, mw, mh, sz * 0.052f, borderPen);
    }

    // ── Stand ─────────────────────────────────────────────────────────────
    if (sz >= 20) {
        float nkW = mw * 0.22f, nkH = sz * 0.058f;
        float bsW = mw * 0.46f, bsH = sz * 0.036f;
        SolidBrush nkBr(Color(145, 0, 85, 205));
        SolidBrush bsBr(Color(125, 0, 65, 175));
        FillRR(g, cx - nkW*0.5f, my+mh,     nkW, nkH, sz*0.010f, nkBr);
        FillRR(g, cx - bsW*0.5f, my+mh+nkH, bsW, bsH, sz*0.010f, bsBr);
    }

    // ── Three dots ────────────────────────────────────────────────────────
    if (sz >= 22) {
        float dr = sz * 0.024f;
        float dy = my + sz * 0.060f;
        float dx = mx + mw - sz * 0.068f;
        SolidBrush dotBr(Color(205, 0, 229, 255));
        for (int i = 0; i < 3; i++)
            g.FillEllipse(&dotBr, dx - i*dr*2.55f - dr, dy - dr, dr*2.f, dr*2.f);
    }

    // ── Speed lines ───────────────────────────────────────────────────────
    if (sz >= 24) {
        float lx2 = mx - sz * 0.055f;
        struct { float len, yOff; BYTE alpha; } lines[] = {
            { sz*0.130f, mh*0.26f, 140 },
            { sz*0.200f, mh*0.50f, 200 },
            { sz*0.130f, mh*0.74f, 110 },
        };
        for (auto& l : lines) {
            Pen lp(Color(l.alpha, 0, 207, 255), sz * 0.022f);
            lp.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            float ly = my + l.yOff;
            g.DrawLine(&lp, lx2 - l.len, ly, lx2, ly);
            SolidBrush dp(Color((BYTE)std::min(255, (int)l.alpha+55), 0, 229, 255));
            float dr2 = sz * 0.019f;
            g.FillEllipse(&dp, lx2 - l.len - dr2, ly - dr2, dr2*2.f, dr2*2.f);
        }
    }

    // ── "UV" monogram ─────────────────────────────────────────────────────
    if (sz >= 30) {
        FontFamily ff(L"Segoe UI");
        float uSz = mh * 0.56f;
        Font fntU(&ff, uSz,         FontStyleBold, UnitPixel);
        Font fntV(&ff, uSz * 0.86f, FontStyleBold, UnitPixel);
        RectF uMsr, vMsr;
        g.MeasureString(L"U", -1, &fntU, PointF(0,0), &uMsr);
        g.MeasureString(L"V", -1, &fntV, PointF(0,0), &vMsr);
        float totalW = uMsr.Width * 0.82f + vMsr.Width * 0.80f;
        float tx = (mx + mw * 0.5f) - totalW * 0.5f + sz * 0.02f;
        float ty = my + (mh - uMsr.Height) * 0.46f;
        SolidBrush uBr(Color(255, 0, 229, 255));
        g.DrawString(L"U", -1, &fntU, PointF(tx, ty), &uBr);
        SolidBrush vBr(Color(240, 242, 245, 247));
        g.DrawString(L"V", -1, &fntV,
            PointF(tx + uMsr.Width * 0.74f, ty + sz * 0.014f), &vBr);
    }
}

// ─── PNG encoder lookup ───────────────────────────────────────────────────────

static bool GetPngClsid(CLSID& clsid) {
    UINT num = 0, sz = 0;
    GetImageEncodersSize(&num, &sz);
    if (!sz) return false;
    auto* c = (ImageCodecInfo*)malloc(sz);
    if (!c) return false;
    GetImageEncoders(num, sz, c);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(c[i].MimeType, L"image/png") == 0) {
            clsid = c[i].Clsid; free(c); return true;
        }
    }
    free(c); return false;
}

// ─── Render one size → PNG bytes ─────────────────────────────────────────────

static std::vector<uint8_t> RenderToPNG(int sz, const CLSID& pngClsid) {
    Bitmap bmp(sz, sz, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.Clear(Color(0, 0, 0, 0));   // fully transparent background
    DrawLogo(g, 0.f, 0.f, (float)sz);

    IStream* s = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &s))) return {};
    if (bmp.Save(s, &pngClsid) != Ok) { s->Release(); return {}; }

    STATSTG st = {}; s->Stat(&st, STATFLAG_NONAME);
    LARGE_INTEGER z = {}; s->Seek(z, STREAM_SEEK_SET, nullptr);
    std::vector<uint8_t> out(st.cbSize.LowPart);
    ULONG n = 0; s->Read(out.data(), st.cbSize.LowPart, &n);
    s->Release(); out.resize(n);
    return out;
}

// ─── ICO packing — PNG-in-ICO (Vista+) ───────────────────────────────────────

#pragma pack(push, 1)
struct IcoHeader   { WORD res, type, count; };
struct IcoDirEntry { BYTE w, h, colors, res; WORD planes, bitCount; DWORD size, offset; };
#pragma pack(pop)

static bool WriteIco(const char* path,
                     const std::vector<std::pair<int, std::vector<uint8_t>>>& imgs) {
    DWORD hdrSz = (DWORD)(sizeof(IcoHeader) + imgs.size() * sizeof(IcoDirEntry));
    std::vector<uint8_t> ico(hdrSz);

    auto* hdr = (IcoHeader*)ico.data();
    hdr->res = 0; hdr->type = 1; hdr->count = (WORD)imgs.size();

    DWORD off = hdrSz;
    for (size_t i = 0; i < imgs.size(); ++i) {
        auto& [sz, png] = imgs[i];
        auto* e = (IcoDirEntry*)(ico.data() + sizeof(IcoHeader) + i * sizeof(IcoDirEntry));
        e->w = e->h = (BYTE)(sz >= 256 ? 0 : sz);
        e->colors = 0; e->res = 0; e->planes = 1; e->bitCount = 32;
        e->size = (DWORD)png.size(); e->offset = off;
        ico.insert(ico.end(), png.begin(), png.end());
        off += (DWORD)png.size();
    }

    FILE* f = nullptr; fopen_s(&f, path, "wb");
    if (!f) return false;
    fwrite(ico.data(), 1, ico.size(), f);
    fclose(f);
    return true;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { printf("Usage: gen_icon <output.ico>\n"); return 1; }

    GdiplusStartupInput gi; ULONG_PTR tok;
    GdiplusStartup(&tok, &gi, nullptr);

    CLSID pngClsid;
    if (!GetPngClsid(pngClsid)) { printf("ERROR: PNG encoder not found\n"); return 1; }

    const int sizes[] = { 16, 32, 48, 256 };
    std::vector<std::pair<int, std::vector<uint8_t>>> imgs;
    for (int sz : sizes) {
        auto png = RenderToPNG(sz, pngClsid);
        if (png.empty()) { printf("ERROR: render failed for %d\n", sz); return 1; }
        printf("  %3dx%-3d  %zu bytes\n", sz, sz, png.size());
        imgs.push_back({ sz, std::move(png) });
    }

    if (!WriteIco(argv[1], imgs)) { printf("ERROR: write failed: %s\n", argv[1]); return 1; }
    printf("ICO written: %s\n", argv[1]);

    GdiplusShutdown(tok);
    return 0;
}
