#include "cme_virtual_midi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Function signatures for teVirtualMIDI */
typedef void* LPVM_MIDI_PORT;
typedef void (CALLBACK *LPVM_CALLBACK)(
    LPVM_MIDI_PORT midiPort,
    LPBYTE midiDataBytes,
    DWORD length,
    DWORD_PTR dwCallbackInstance
);

typedef LPVM_MIDI_PORT (WINAPI *pfnVirtualMIDICreatePortEx2_t)(
    LPCWSTR portName,
    LPVM_CALLBACK callback,
    DWORD_PTR dwCallbackInstance,
    DWORD maxSysexLength,
    DWORD flags
);
typedef void (WINAPI *pfnVirtualMIDIClosePort_t)(LPVM_MIDI_PORT midiPort);
typedef BOOL (WINAPI *pfnVirtualMIDISendData_t)(LPVM_MIDI_PORT midiPort, LPBYTE midiDataBytes, DWORD length);

static void CALLBACK TeCallback(
    LPVM_MIDI_PORT midiPort,
    LPBYTE midiDataBytes,
    DWORD length,
    DWORD_PTR dwCallbackInstance
) {
    (void)midiPort;
    CMEVirtualMIDIDevice *vm = (CMEVirtualMIDIDevice *)dwCallbackInstance;
    if (vm && vm->inCallback && midiDataBytes && length > 0) {
        vm->inCallback((const uint8_t *)midiDataBytes, (int)length, vm->callbackUserData);
    }
}

static void CALLBACK WinMMInCallback(
    HMIDIIN hMidiIn,
    UINT wMsg,
    DWORD_PTR dwInstance,
    DWORD_PTR dwParam1,
    DWORD_PTR dwParam2
) {
    (void)hMidiIn;
    (void)dwParam2;
    CMEVirtualMIDIDevice *vm = (CMEVirtualMIDIDevice *)dwInstance;
    if (!vm || !vm->inCallback) return;

    if (wMsg == MIM_DATA) {
        uint8_t bytes[3];
        bytes[0] = (uint8_t)(dwParam1 & 0xFF);
        bytes[1] = (uint8_t)((dwParam1 >> 8) & 0xFF);
        bytes[2] = (uint8_t)((dwParam1 >> 16) & 0xFF);
        int len = 3;
        uint8_t st = bytes[0] & 0xF0;
        if (st == 0xC0 || st == 0xD0) len = 2;
        vm->inCallback(bytes, len, vm->callbackUserData);
    }
}

bool CME_VirtualMIDI_Init(
    CMEVirtualMIDIDevice *vm,
    const wchar_t *preferredPortName,
    CMEVMInputCallback inCallback,
    void *userData
) {
    if (!vm) return false;
    memset(vm, 0, sizeof(*vm));

    const wchar_t *name = preferredPortName ? preferredPortName : L"CME UF5";
    wcsncpy(vm->portName, name, 63);
    vm->inCallback = inCallback;
    vm->callbackUserData = userData;

    /* 1. Try to load teVirtualMIDI DLL (64-bit or generic) */
    HMODULE hDll = LoadLibraryW(L"teVirtualMIDI64.dll");
    if (!hDll) hDll = LoadLibraryW(L"teVirtualMIDI.dll");
    if (!hDll) hDll = LoadLibraryW(L"teVirtualMIDI32.dll");

    if (hDll) {
        pfnVirtualMIDICreatePortEx2_t pCreate =
            (pfnVirtualMIDICreatePortEx2_t)(void*)GetProcAddress(hDll, "virtualMIDICreatePortEx2");
        pfnVirtualMIDIClosePort_t pClose =
            (pfnVirtualMIDIClosePort_t)(void*)GetProcAddress(hDll, "virtualMIDIClosePort");
        pfnVirtualMIDISendData_t pSend =
            (pfnVirtualMIDISendData_t)(void*)GetProcAddress(hDll, "virtualMIDISendData");

        if (pCreate && pClose && pSend) {
            LPVM_MIDI_PORT port = pCreate(name, TeCallback, (DWORD_PTR)vm, 65535, 0);
            if (port) {
                vm->hTeDll = hDll;
                vm->tePortHandle = port;
                vm->pfnCreatePortEx2 = (void*)pCreate;
                vm->pfnClosePort = (void*)pClose;
                vm->pfnSendData = (void*)pSend;
                vm->type = CME_VM_TE_VIRTUAL_MIDI;
                vm->active = true;
                return true;
            }
        }
        FreeLibrary(hDll);
    }

    /* 2. Fall back to existing loopMIDI or virtual port via WinMM */
    UINT numOut = midiOutGetNumDevs();
    for (UINT i = 0; i < numOut; i++) {
        MIDIOUTCAPSW caps;
        if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            if (wcsstr(caps.szPname, L"CME") || wcsstr(caps.szPname, L"loopMIDI") || wcsstr(caps.szPname, name)) {
                if (midiOutOpen(&vm->hMidiOut, i, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
                    vm->outDeviceID = i;
                    vm->type = CME_VM_WINMM_LOOPMIDI;
                    wcsncpy(vm->portName, caps.szPname, 63);
                    vm->active = true;

                    /* Also check for corresponding MIDI IN */
                    UINT numIn = midiInGetNumDevs();
                    for (UINT j = 0; j < numIn; j++) {
                        MIDIINCAPSW inCaps;
                        if (midiInGetDevCapsW(j, &inCaps, sizeof(inCaps)) == MMSYSERR_NOERROR) {
                            if (wcsstr(inCaps.szPname, caps.szPname)) {
                                if (midiInOpen(&vm->hMidiIn, j, (DWORD_PTR)WinMMInCallback, (DWORD_PTR)vm, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
                                    vm->inDeviceID = j;
                                    midiInStart(vm->hMidiIn);
                                    break;
                                }
                            }
                        }
                    }
                    return true;
                }
            }
        }
    }

    /* 3. WinMM default output device fallback */
    if (numOut > 0) {
        MIDIOUTCAPSW caps;
        if (midiOutGetDevCapsW(0, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            if (midiOutOpen(&vm->hMidiOut, 0, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
                vm->outDeviceID = 0;
                vm->type = CME_VM_WINMM_OUTPUT;
                wcsncpy(vm->portName, caps.szPname, 63);
                vm->active = true;
                return true;
            }
        }
    }

    return false;
}

bool CME_VirtualMIDI_Send(CMEVirtualMIDIDevice *vm, const uint8_t *data, int length) {
    if (!vm || !vm->active || !data || length <= 0) return false;

    if (vm->type == CME_VM_TE_VIRTUAL_MIDI && vm->pfnSendData) {
        pfnVirtualMIDISendData_t pSend = (pfnVirtualMIDISendData_t)vm->pfnSendData;
        return pSend((LPVM_MIDI_PORT)vm->tePortHandle, (LPBYTE)data, (DWORD)length) == TRUE;
    }

    if (vm->hMidiOut) {
        if (length <= 3) {
            DWORD msg = data[0] | (data[1] << 8) | ((length >= 3 ? data[2] : 0) << 16);
            return midiOutShortMsg(vm->hMidiOut, msg) == MMSYSERR_NOERROR;
        } else {
            /* SysEx */
            MIDIHDR hdr;
            memset(&hdr, 0, sizeof(hdr));
            hdr.lpData = (LPSTR)data;
            hdr.dwBufferLength = length;
            hdr.dwBytesRecorded = length;

            if (midiOutPrepareHeader(vm->hMidiOut, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
                MMRESULT res = midiOutLongMsg(vm->hMidiOut, &hdr, sizeof(hdr));
                while (!(hdr.dwFlags & MHDR_DONE)) {
                    Sleep(1);
                }
                midiOutUnprepareHeader(vm->hMidiOut, &hdr, sizeof(hdr));
                return res == MMSYSERR_NOERROR;
            }
        }
    }

    return false;
}

void CME_VirtualMIDI_Panic(CMEVirtualMIDIDevice *vm) {
    if (!vm || !vm->active) return;

    for (uint8_t ch = 0; ch < 16; ch++) {
        uint8_t msgAllNotesOff[3] = { (uint8_t)(0xB0 | ch), 123, 0 };
        uint8_t msgAllSoundOff[3] = { (uint8_t)(0xB0 | ch), 120, 0 };
        uint8_t msgResetCtrl[3]   = { (uint8_t)(0xB0 | ch), 121, 0 };
        CME_VirtualMIDI_Send(vm, msgAllNotesOff, 3);
        CME_VirtualMIDI_Send(vm, msgAllSoundOff, 3);
        CME_VirtualMIDI_Send(vm, msgResetCtrl, 3);
    }
}

void CME_VirtualMIDI_Close(CMEVirtualMIDIDevice *vm) {
    if (!vm) return;

    if (vm->type == CME_VM_TE_VIRTUAL_MIDI && vm->pfnClosePort && vm->tePortHandle) {
        pfnVirtualMIDIClosePort_t pClose = (pfnVirtualMIDIClosePort_t)vm->pfnClosePort;
        pClose((LPVM_MIDI_PORT)vm->tePortHandle);
        vm->tePortHandle = NULL;
    }

    if (vm->hTeDll) {
        FreeLibrary(vm->hTeDll);
        vm->hTeDll = NULL;
    }

    if (vm->hMidiIn) {
        midiInStop(vm->hMidiIn);
        midiInClose(vm->hMidiIn);
        vm->hMidiIn = NULL;
    }

    if (vm->hMidiOut) {
        midiOutReset(vm->hMidiOut);
        midiOutClose(vm->hMidiOut);
        vm->hMidiOut = NULL;
    }

    vm->active = false;
}

