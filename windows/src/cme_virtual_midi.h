#ifndef CME_VIRTUAL_MIDI_H
#define CME_VIRTUAL_MIDI_H

#include <windows.h>
#include <mmsystem.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CME_VM_NONE = 0,
    CME_VM_TE_VIRTUAL_MIDI,     /* Native Tobias Erichsen teVirtualMIDI kernel port */
    CME_VM_WINMM_LOOPMIDI,      /* loopMIDI port opened via WinMM */
    CME_VM_WINMM_OUTPUT         /* Standard WinMM MIDI Output */
} CMEVMType;

/* Callback when DAW sends MIDI to the UF5 virtual port */
typedef void (*CMEVMInputCallback)(const uint8_t *data, int length, void *userData);

typedef struct {
    CMEVMType           type;
    wchar_t             portName[64];
    bool                active;

    /* teVirtualMIDI handles & functions */
    HMODULE             hTeDll;
    void*               tePortHandle;
    void*               pfnCreatePortEx2;
    void*               pfnClosePort;
    void*               pfnSendData;

    /* WinMM fallback handles */
    HMIDIOUT            hMidiOut;
    HMIDIIN             hMidiIn;
    UINT                outDeviceID;
    UINT                inDeviceID;

    CMEVMInputCallback  inCallback;
    void                *callbackUserData;
} CMEVirtualMIDIDevice;

bool CME_VirtualMIDI_Init(
    CMEVirtualMIDIDevice *vm,
    const wchar_t *preferredPortName,
    CMEVMInputCallback inCallback,
    void *userData
);

bool CME_VirtualMIDI_Send(CMEVirtualMIDIDevice *vm, const uint8_t *data, int length);
void CME_VirtualMIDI_Panic(CMEVirtualMIDIDevice *vm);
void CME_VirtualMIDI_Close(CMEVirtualMIDIDevice *vm);

#ifdef __cplusplus
}
#endif

#endif /* CME_VIRTUAL_MIDI_H */

