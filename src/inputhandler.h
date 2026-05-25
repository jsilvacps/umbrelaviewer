#pragma once
#include <windows.h>
#include <cstdint>

void InputMouseMove  (int32_t normX, int32_t normY);
void InputMouseButton(uint32_t button, bool pressed);
void InputMouseWheel (int32_t delta);
void InputKey        (uint32_t vkCode, bool pressed);
