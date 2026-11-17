#ifndef CME_SHARED_STATE_H
#define CME_SHARED_STATE_H

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include "cme_protocol.h"

#define CME_REGISTRY_KEY    L"Software\\CME-Pro\\UF5"
#define CME_SHARED_MEM_NAME L"Local\\CMEUF5_SharedMemory"
#define CME_EVENT_CONFIG    L"Local\\CMEUF5_ConfigChanged"
#define CME_LOG_RING_SIZE   64

#pragma pack(push, 1)

typedef struct {
    uint32_t timestampMs;
    uint8_t  type;          /* 0=In, 1=Out */
    uint8_t  status;
    uint8_t  data1;
    uint8_t  data2;
    uint8_t  len;
    uint8_t  raw[4];
    char     desc[32];
} CMELogEntry;

typedef struct {
    uint32_t magic;         /* 'CUF5' = 0x35465543 */
    uint32_t version;       /* 2 */

    /* Connection & Ports */
    bool     usbConnected;
    bool     midiPortActive;
    wchar_t  portName[64];
    wchar_t  usbDevicePath[256];

    /* Configuration */
    CMEConfig config;

    /* Live Hardware State */
    uint8_t  keys[128];     /* Note velocity (0 = off) */
    uint8_t  lastNote;
    uint8_t  lastVelocity;

    uint8_t  faders[9];     /* [0]=Master, [1..8]=Ch 1..8 */
    uint8_t  knobs[8];      /* CC 74, 71, 73, 72, 10, 91, 93, 94 */
    int16_t  pitchBend;     /* -8192 .. +8191 */
    uint8_t  modWheel;      /* 0 .. 127 */
    uint8_t  aftertouch;    /* 0 .. 127 */
    bool     sustain;       /* true/false */
    uint8_t  expression;    /* 0 .. 127 */
    uint8_t  activeTransport; /* CC 114..119 or 0 */

    char     lcdText[32];

    /* Real-Time MIDI Monitor Log Ring Buffer */
    uint32_t logHead;
    uint32_t logCount;
    CMELogEntry logRing[CME_LOG_RING_SIZE];
} CMEUFSharedState;

#pragma pack(pop)

#define CME_MAGIC 0x35465543

/* Load configuration from Windows Registry */
static inline void CME_LoadRegistryConfig(CMEConfig *cfg) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CME_REGISTRY_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVal = 0, dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"VelocityCurve", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->velocityCurve = (int)dwVal;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"FixedVelocity", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->fixedVelocity = (int)dwVal;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"OctaveShift", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->octaveShift = (int)(int32_t)dwVal;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"TransposeSemitones", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->transposeSemitones = (int)(int32_t)dwVal;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"ChannelOverride", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->channelOverride = (int)dwVal;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"FaderMode", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->faderMode = (int)dwVal;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"TransportMode", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->transportMode = (int)dwVal;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"PitchBendDeadzone", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->pitchBendDeadzone = (int)dwVal;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"FilterActiveSensing", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->filterActiveSensing = (dwVal != 0);
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"FilterClock", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->filterClock = (dwVal != 0);
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"FilterAftertouch", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS) cfg->filterAftertouch = (dwVal != 0);
        RegCloseKey(hKey);
    } else {
        /* Defaults */
        cfg->velocityCurve = CME_VELOCITY_LINEAR;
        cfg->fixedVelocity = 100;
        cfg->octaveShift = 0;
        cfg->transposeSemitones = 0;
        cfg->channelOverride = 0;
        cfg->faderMode = 0;
        cfg->transportMode = 0;
        cfg->pitchBendDeadzone = 0;
        cfg->filterActiveSensing = true;
        cfg->filterClock = true;
        cfg->filterAftertouch = false;
    }
}

/* Save configuration to Windows Registry */
static inline void CME_SaveRegistryConfig(const CMEConfig *cfg) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CME_REGISTRY_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD dwVal;
        dwVal = (DWORD)cfg->velocityCurve;      RegSetValueExW(hKey, L"VelocityCurve", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = (DWORD)cfg->fixedVelocity;      RegSetValueExW(hKey, L"FixedVelocity", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = (DWORD)(int32_t)cfg->octaveShift; RegSetValueExW(hKey, L"OctaveShift", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = (DWORD)(int32_t)cfg->transposeSemitones; RegSetValueExW(hKey, L"TransposeSemitones", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = (DWORD)cfg->channelOverride;    RegSetValueExW(hKey, L"ChannelOverride", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = (DWORD)cfg->faderMode;          RegSetValueExW(hKey, L"FaderMode", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = (DWORD)cfg->transportMode;      RegSetValueExW(hKey, L"TransportMode", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = (DWORD)cfg->pitchBendDeadzone;  RegSetValueExW(hKey, L"PitchBendDeadzone", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = cfg->filterActiveSensing ? 1 : 0; RegSetValueExW(hKey, L"FilterActiveSensing", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = cfg->filterClock ? 1 : 0;         RegSetValueExW(hKey, L"FilterClock", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        dwVal = cfg->filterAftertouch ? 1 : 0;    RegSetValueExW(hKey, L"FilterAftertouch", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(DWORD));
        RegCloseKey(hKey);

        /* Signal notification event to all listening processes */
        HANDLE hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, CME_EVENT_CONFIG);
        if (hEvent) {
            SetEvent(hEvent);
            CloseHandle(hEvent);
        }
    }
}

#endif /* CME_SHARED_STATE_H */

