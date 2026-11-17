#ifndef CME_WINUSB_H
#define CME_WINUSB_H

#include <windows.h>
#include <winusb.h>
#include <setupapi.h>
#include <stdbool.h>
#include "cme_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CME_NUM_OVERLAPPED_URBS 16

typedef struct {
    OVERLAPPED  overlapped;
    uint8_t     buffer[CME_BULK_PACKET_SIZE];
    bool        pending;
} CMEOverlappedContext;

typedef struct {
    HANDLE                  hDeviceFile;
    WINUSB_INTERFACE_HANDLE hWinUsb;
    wchar_t                 devicePath[512];
    bool                    connected;

    uint8_t                 inPipe;     /* 0x82 */
    uint8_t                 outPipe;    /* 0x02 */
    uint16_t                maxPacketSize;

    HANDLE                  hStopEvent;
    HANDLE                  hReadThread;
    bool                    threadRunning;

    CMEOverlappedContext    contexts[CME_NUM_OVERLAPPED_URBS];
    HANDLE                  events[CME_NUM_OVERLAPPED_URBS + 1]; /* +1 for hStopEvent */

    CMEProtocolState        *protoState;
    CMEMIDICallback         midiCallback;
    void                    *callbackUserData;
} CMEWinUSBDevice;

bool CME_WinUSB_FindDevice(wchar_t *outDevicePath, size_t maxChars);
bool CME_WinUSB_Open(CMEWinUSBDevice *dev, const wchar_t *devicePath);
bool CME_WinUSB_Start(
    CMEWinUSBDevice *dev,
    CMEProtocolState *protoState,
    CMEMIDICallback midiCallback,
    void *userData
);
void CME_WinUSB_Stop(CMEWinUSBDevice *dev);
void CME_WinUSB_Close(CMEWinUSBDevice *dev);
bool CME_WinUSB_Write(CMEWinUSBDevice *dev, const uint8_t *data, int length);

#ifdef __cplusplus
}
#endif

#endif /* CME_WINUSB_H */

