#pragma once
#include <cstdint>

#pragma pack(push, 1)

enum class PacketType : uint32_t {
    CONNECT_REQUEST  = 1,
    CONNECT_ACCEPT   = 2,
    CONNECT_DENY     = 3,
    DISCONNECT       = 4,
    SCREEN_INFO      = 5,
    SCREEN_FRAME     = 6,
    MOUSE_MOVE       = 7,
    MOUSE_BUTTON     = 8,
    MOUSE_WHEEL      = 9,
    KEYBOARD_EVENT   = 10,
    CHAT_MESSAGE     = 11,
    FILE_SEND_START  = 13,
    FILE_SEND_DATA   = 14,
    FILE_SEND_END    = 15,
    CONNECT_BUSY     = 16,
    PING             = 17,
    PONG             = 18,
};

struct PacketHeader {
    PacketType type;
    uint32_t   dataSize;
};

struct ConnectRequestData {
    char password[64];
};

struct ScreenInfoData {
    uint32_t width;
    uint32_t height;
};

struct MouseMoveData   { int32_t  x, y; };          // normalized 0..10000
struct MouseButtonData { uint32_t button, pressed; };// button: 0=L 1=R 2=M
struct MouseWheelData  { int32_t  delta; };
struct KeyEventData    { uint32_t vkCode, pressed; };

struct FileSendStartData {
    uint64_t fileSize;
    char     filename[256];
};

#pragma pack(pop)

constexpr int    DEFAULT_PORT    = 7890;
constexpr size_t MAX_PACKET_SIZE = 4 * 1024 * 1024;
