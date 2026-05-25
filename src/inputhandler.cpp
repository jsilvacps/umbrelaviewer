#include "inputhandler.h"
#pragma comment(lib, "user32.lib")

static void EnsureVirtScreen(int& w, int& h) {
    w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

void InputMouseMove(int32_t normX, int32_t normY) {
    int sw, sh; EnsureVirtScreen(sw, sh);
    int ox = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int oy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int ax = ox + (int)((normX / 10000.0) * sw);
    int ay = oy + (int)((normY / 10000.0) * sh);

    INPUT inp = {};
    inp.type       = INPUT_MOUSE;
    inp.mi.dx      = (LONG)((ax * 65535L) / sw);
    inp.mi.dy      = (LONG)((ay * 65535L) / sh);
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &inp, sizeof(INPUT));
}

void InputMouseButton(uint32_t button, bool pressed) {
    INPUT inp = {};
    inp.type = INPUT_MOUSE;
    switch (button) {
    case 0: inp.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;   break;
    case 1: inp.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;  break;
    case 2: inp.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    default: return;
    }
    SendInput(1, &inp, sizeof(INPUT));
}

void InputMouseWheel(int32_t delta) {
    INPUT inp = {};
    inp.type         = INPUT_MOUSE;
    inp.mi.mouseData = (DWORD)delta;
    inp.mi.dwFlags   = MOUSEEVENTF_WHEEL;
    SendInput(1, &inp, sizeof(INPUT));
}

void InputKey(uint32_t vkCode, bool pressed) {
    if (!vkCode) return;
    INPUT inp = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = (WORD)vkCode;
    inp.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;
    if (vkCode == VK_RCONTROL || vkCode == VK_RMENU  || vkCode == VK_INSERT  ||
        vkCode == VK_DELETE   || vkCode == VK_HOME   || vkCode == VK_END     ||
        vkCode == VK_PRIOR    || vkCode == VK_NEXT   || vkCode == VK_UP      ||
        vkCode == VK_DOWN     || vkCode == VK_LEFT   || vkCode == VK_RIGHT   ||
        vkCode == VK_NUMLOCK  || vkCode == VK_DIVIDE || vkCode == VK_LWIN    ||
        vkCode == VK_RWIN     || vkCode == VK_APPS)
        inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &inp, sizeof(INPUT));
}
