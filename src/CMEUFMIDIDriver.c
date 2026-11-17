/*
 * CMEUFMIDIDriver.c
 *
 * Native macOS CoreMIDI Driver Plug-in (CFPlugIn) for CME UF Series
 * USB MIDI Master Keyboards (UF5, UF6, UF7, UF8 / VID 0x7104, PID 0x2202).
 *
 * Technical background:
 * 1. The CME UF USB interface reports bInterfaceClass = 0x02 (CDC Control),
 *    bInterfaceSubClass = 0x01, bInterfaceProtocol = 0x01 instead of standard
 *    USB Audio/MIDI Streaming (Class 0x01, SubClass 0x03). Consequently,
 *    macOS's built-in AppleMIDIUSBDriver ignores the device.
 * 2. The CME UF firmware uses a non-standard USB-MIDI packet encoding
 *    (corresponding to QUIRK_MIDI_CME / snd_usbmidi_cme_input in Linux ALSA):
 *      - Standard channel voice messages use standard 4-byte USB-MIDI packets.
 *      - SysEx (0xF0..0xF7), Song Position Pointer (0xF2), System Real-Time
 *        (0xF8, 0xFA, 0xFB, 0xFC), and Active Sensing (0xFE) are preceded by
 *        a 0x0F Code Index Number (CIN) byte.
 *      - SysEx messages are NOT split into 4-byte chunks; the entire SysEx
 *        message is sent contiguously after the 0x0F byte.
 *      - Multiple MIDI messages can be concatenated in a single USB bulk packet.
 *      - When idle, the Philips PDIUSBD12 USB controller on Endpoint 2 IN (0x82)
 *        continuously responds to Bulk IN reads with 0-byte Zero-Length Packets
 *        (ZLPs) instead of NAKing, and periodically emits 0xF8 / 0xFE heartbeats.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <syslog.h>
#include <mach/mach_time.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreMIDI/MIDIDriver.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#define CME_USB_VID             0x7104
#define CME_USB_PID             0x2202
#define CME_USB_INTERFACE_NUM   0
#define CME_USB_CONFIG_VALUE    1

/*
 * Number of concurrent Bulk IN URBs queued on Endpoint 2 IN (0x82).
 * Keeping multiple transfers queued in the hardware xHCI ring ensures the
 * CME UF5's Philips PDIUSBD12 double-buffered FIFO is drained every 1ms USB
 * frame without gaps, preventing ZLP buffer-full drops during key presses.
 */
#define CME_NUM_INPUT_URBS      32
#define CME_BULK_PACKET_SIZE    64

#define CME_DRIVER_PREFS_ID     CFSTR("com.cme-pro.midi.driver.uf")
#define CME_CONFIG_NOTIFY_NAME  CFSTR("com.cme-pro.midi.driver.uf.configChanged")

/* Factory UUID: A8B5E412-9F3C-4D7A-8B2E-1C6D5E4F3A2B */
#define kCMEUFMIDIDriverFactoryID \
    CFUUIDGetConstantUUIDWithBytes(NULL, \
        0xA8, 0xB5, 0xE4, 0x12, 0x9F, 0x3C, 0x4D, 0x7A, \
        0x8B, 0x2E, 0x1C, 0x6D, 0x5E, 0x4F, 0x3A, 0x2B)

/* Standard USB-MIDI CIN payload byte lengths */
static const UInt8 kUSBMIDICINLength[16] = {
    0, 0, 2, 3, 3, 1, 2, 3,
    3, 3, 3, 3, 2, 2, 3, 1
};

struct CMEUFDriver;

typedef struct {
    struct CMEUFDriver *driver;
    UInt8 pipeRef;
    UInt16 maxPacketSize;
    UInt32 urbIndex;
    UInt8 buffer[CME_BULK_PACKET_SIZE];
} CMEUFPipeContext;

typedef struct CMEUFDriver {
    MIDIDriverInterface     *vtbl;
    CFUUIDRef               factoryID;
    UInt32                  refCount;
    int                     interfaceVersion;

    CFRunLoopRef            serverRunLoop;
    IONotificationPortRef   notifyPort;
    io_iterator_t           matchedIter;
    io_iterator_t           terminatedIter;

    MIDIDeviceRef           midiDevice;
    MIDIEndpointRef         midiSource;
    MIDIEndpointRef         midiDest;

    pthread_mutex_t         lock;
    volatile Boolean        running;
    volatile Boolean        usbConnected;

    io_service_t            usbService;
    IOUSBInterfaceInterface300 **usbInterface;

    UInt8                   inPipe1;    /* 0x81 (16B, unused per ESIMidiport.sys) */
    UInt8                   outPipe1;   /* 0x01 (16B, unused per ESIMidiport.sys) */
    UInt8                   inPipe2;    /* 0x82 (64B Main MIDI IN) */
    UInt8                   outPipe2;   /* 0x02 (64B Main MIDI OUT) */

    pthread_t               ioThread;
    Boolean                 ioThreadActive;
    CFRunLoopRef            ioRunLoop;
    CFRunLoopSourceRef      usbAsyncSource;

    CMEUFPipeContext        inUrbs[CME_NUM_INPUT_URBS];

    /* Real-time configurable driver parameters */
    volatile Boolean        filterActiveSensing;
    volatile Boolean        filterClock;
    volatile Boolean        filterAftertouch;
    volatile int            velocityCurve;      /* 0=Linear, 1=Soft, 2=Hard, 3=S-Curve, 4=Fixed */
    volatile int            fixedVelocity;      /* 1..127 */
    volatile int            octaveShift;        /* -3..+3 */
    volatile int            transposeSemitones; /* -24..+24 */
    volatile int            channelOverride;    /* 0=Pass, 1..16=Force Ch */
    volatile int            faderMode;          /* 0=Multi-Ch (Ch1..8 CC7), 1=Single-Ch (Ch1 CC20..27) */
    volatile int            transportMode;      /* 0=Logic Pro Surface CC, 1=Raw Passthrough */
    volatile int            pitchBendDeadzone;  /* 0..16 */

    UInt8                   velocityLUT[128];
    int                     lastSongPosition;
} CMEUFDriver;

/* Forward declarations */
static void CloseUSBDevice(CMEUFDriver *driver);
static void DevicesAdded(void *refCon, io_iterator_t iterator);
static void DevicesRemoved(void *refCon, io_iterator_t iterator);

static Boolean ReadBoolPreference(CFStringRef key, Boolean defaultValue) {
    Boolean exists = false;
    Boolean val = CFPreferencesGetAppBooleanValue(key, CME_DRIVER_PREFS_ID, &exists);
    return exists ? val : defaultValue;
}

static int ReadIntPreference(CFStringRef key, int defaultValue, int minVal, int maxVal) {
    Boolean exists = false;
    CFIndex val = CFPreferencesGetAppIntegerValue(key, CME_DRIVER_PREFS_ID, &exists);
    if (!exists) return defaultValue;
    int v = (int)val;
    if (v < minVal) v = minVal;
    if (v > maxVal) v = maxVal;
    return v;
}

static void RebuildVelocityLUT(CMEUFDriver *driver) {
    int curve = driver->velocityCurve;
    int fixed = driver->fixedVelocity;
    driver->velocityLUT[0] = 0;
    for (int v = 1; v <= 127; v++) {
        double norm = (double)v / 127.0;
        double out = norm;
        switch (curve) {
            case 1: /* Soft (easier high velocity) */
                out = pow(norm, 0.55);
                break;
            case 2: /* Hard (firmer touch required) */
                out = pow(norm, 1.75);
                break;
            case 3: /* S-Curve */
                out = norm * norm * (3.0 - 2.0 * norm);
                break;
            case 4: /* Fixed velocity */
                driver->velocityLUT[v] = (UInt8)fixed;
                continue;
            case 0:
            default:
                out = norm;
                break;
        }
        int mapped = (int)llround(out * 127.0);
        if (mapped < 1) mapped = 1;
        if (mapped > 127) mapped = 127;
        driver->velocityLUT[v] = (UInt8)mapped;
    }
}

static void LoadDriverPreferences(CMEUFDriver *driver) {
    CFPreferencesAppSynchronize(CME_DRIVER_PREFS_ID);
    driver->filterActiveSensing = ReadBoolPreference(CFSTR("FilterActiveSensing"), true);
    driver->filterClock         = ReadBoolPreference(CFSTR("FilterClock"), true);
    driver->filterAftertouch    = ReadBoolPreference(CFSTR("FilterAftertouch"), false);
    driver->velocityCurve       = ReadIntPreference(CFSTR("VelocityCurve"), 0, 0, 4);
    driver->fixedVelocity       = ReadIntPreference(CFSTR("FixedVelocity"), 100, 1, 127);
    driver->octaveShift         = ReadIntPreference(CFSTR("OctaveShift"), 0, -3, 3);
    driver->transposeSemitones  = ReadIntPreference(CFSTR("TransposeSemitones"), 0, -24, 24);
    driver->channelOverride     = ReadIntPreference(CFSTR("ChannelOverride"), 0, 0, 16);
    driver->faderMode           = ReadIntPreference(CFSTR("FaderMode"), 0, 0, 1);
    driver->transportMode       = ReadIntPreference(CFSTR("TransportMode"), 0, 0, 1);
    driver->pitchBendDeadzone   = ReadIntPreference(CFSTR("PitchBendDeadzone"), 0, 0, 16);
    RebuildVelocityLUT(driver);
}

static void OnConfigChangedNotification(
    CFNotificationCenterRef center,
    void *observer,
    CFStringRef name,
    const void *object,
    CFDictionaryRef userInfo
) {
    (void)center;
    (void)name;
    (void)object;
    (void)userInfo;
    CMEUFDriver *driver = (CMEUFDriver *)observer;
    if (driver) {
        LoadDriverPreferences(driver);
        syslog(LOG_NOTICE, "CMEUFMIDIDriver: Reloaded live configuration (curve=%d, oct=%d, semi=%d, ch=%d, faderMode=%d, transportMode=%d)",
               driver->velocityCurve, driver->octaveShift, driver->transposeSemitones,
               driver->channelOverride, driver->faderMode, driver->transportMode);
    }
}

/* ==========================================================================
 * CME USB-MIDI Input Protocol Parser & Real-Time DSP Processor
 * ========================================================================== */

static void AppendMIDIMessage(
    MIDIPacketList *pktList,
    size_t pktBufSize,
    MIDIPacket **pkt,
    MIDITimeStamp *timestamp,
    Boolean *hasPackets,
    const UInt8 *data,
    int dataLength
) {
    if (dataLength <= 0 || !pkt || !*pkt) return;
    MIDIPacket *nextPkt = MIDIPacketListAdd(
        pktList, pktBufSize, *pkt, (*timestamp)++, (ByteCount)dataLength, data
    );
    if (nextPkt) {
        *pkt = nextPkt;
        *hasPackets = true;
    }
}

/*
 * Emit a Logic Pro Control Surface Transport CC message on Channel 1:
 *   CC 0x72 (114) = Record
 *   CC 0x73 (115) = Top / Return to Zero
 *   CC 0x74 (116) = Rewind
 *   CC 0x75 (117) = Forward
 *   CC 0x76 (118) = Stop
 *   CC 0x77 (119) = Play
 */
static void EmitTransportCC(
    MIDIPacketList *pktList,
    size_t pktBufSize,
    MIDIPacket **pkt,
    MIDITimeStamp *timestamp,
    Boolean *hasPackets,
    UInt8 ccNumber
) {
    UInt8 msg[3] = { 0xB0, ccNumber, 0x7F };
    AppendMIDIMessage(pktList, pktBufSize, pkt, timestamp, hasPackets, msg, 3);
}

static void ProcessAndAppendChannelMessage(
    CMEUFDriver *driver,
    MIDIPacketList *pktList,
    size_t pktBufSize,
    MIDIPacket **pkt,
    MIDITimeStamp *timestamp,
    Boolean *hasPackets,
    const UInt8 *rawMsg,
    int dataLength
) {
    if (dataLength < 2) return;
    UInt8 msg[3];
    msg[0] = rawMsg[0];
    msg[1] = rawMsg[1];
    msg[2] = (dataLength >= 3) ? rawMsg[2] : 0;

    UInt8 statusHigh = msg[0] & 0xF0;
    UInt8 ch = msg[0] & 0x0F;

    /* 1. Filter Aftertouch if requested */
    if (driver && driver->filterAftertouch) {
        if (statusHigh == 0xD0 || statusHigh == 0xA0) {
            return;
        }
    }

    /* 2. Fader Mode & Channel Override */
    Boolean isMultiChannelFader = (statusHigh == 0xB0 && msg[1] == 0x07 && ch <= 7);
    if (driver) {
        if (isMultiChannelFader && driver->faderMode == 1) {
            /* Remap Faders 1..8 (Ch 1..8 CC 7) -> Ch 1 CC 20..27 */
            msg[1] = (UInt8)(20 + ch);
            ch = (driver->channelOverride > 0) ? (UInt8)(driver->channelOverride - 1) : 0;
            msg[0] = (UInt8)(statusHigh | (ch & 0x0F));
        } else if (driver->channelOverride > 0 && !isMultiChannelFader) {
            /* Force MIDI channel (preserving multi-channel faders in Logic Mixer mode) */
            ch = (UInt8)(driver->channelOverride - 1);
            msg[0] = (UInt8)(statusHigh | (ch & 0x0F));
        }
    }

    /* 3. Note On / Note Off / Poly Aftertouch: Octave, Transpose, Velocity Curve */
    if (driver && (statusHigh == 0x80 || statusHigh == 0x90 || statusHigh == 0xA0)) {
        int note = (int)msg[1] + (driver->octaveShift * 12) + driver->transposeSemitones;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        msg[1] = (UInt8)note;

        if (statusHigh == 0x90 && msg[2] > 0) {
            msg[2] = driver->velocityLUT[msg[2] & 0x7F];
        }
    }

    /* 4. Pitch Bend Deadzone */
    if (driver && statusHigh == 0xE0 && driver->pitchBendDeadzone > 0 && dataLength >= 3) {
        int bend = ((int)msg[2] << 7) | (int)msg[1];
        int diff = bend - 8192;
        int threshold = driver->pitchBendDeadzone * 64;
        if (diff > -threshold && diff < threshold) {
            msg[1] = 0x00;
            msg[2] = 0x40; /* 8192 center */
        }
    }

    AppendMIDIMessage(pktList, pktBufSize, pkt, timestamp, hasPackets, msg, dataLength);
}

static void CMEParseUSBMIDIInputWithDriver(
    CMEUFDriver *driver,
    const UInt8 *buffer,
    int bufferLength,
    Boolean filterActiveSensing,
    Boolean filterClock,
    MIDIEndpointRef midiSource
) {
    if (!buffer || bufferLength < 4 || !midiSource) {
        return;
    }

    Byte pktBuf[1024];
    MIDIPacketList *pktList = (MIDIPacketList *)pktBuf;
    MIDIPacket *pkt = MIDIPacketListInit(pktList);
    MIDITimeStamp timestamp = mach_absolute_time();
    Boolean hasPackets = false;
    Boolean translateTransport = (driver == NULL || driver->transportMode == 0);

    int remaining = bufferLength;
    const UInt8 *ptr = buffer;

    while (remaining >= 4) {
        int sourceLength = 4;
        UInt8 cin = ptr[0] & 0x0F;

        if (cin == 0x00) {
            /* Empty/padding 4-byte USB-MIDI slot */
            ptr += 4;
            remaining -= 4;
            continue;
        }

        if (cin == 0x0F) {
            int dataLength = 1;
            UInt8 status = ptr[1];

            if (status == 0xF0) {
                /*
                 * CME SysEx (snd_usbmidi_cme_input / ESIMidiport.sys):
                 * Sent as a single contiguous message starting at ptr[1] (0xF0)
                 * and ending at 0xF7, preceded by ptr[0] = 0x0F.
                 */
                const UInt8 *tmp = ptr + 2;
                int tmpLen = remaining - 2;
                while (tmpLen > 1 && *tmp != 0xF7) {
                    tmp++;
                    tmpLen--;
                }
                dataLength = (int)(tmp - ptr);
                sourceLength = dataLength + 1;
            } else if (status == 0xF2) {
                /* 3-byte Song Position Pointer */
                dataLength = 3;
            } else if (status == 0xF1 || status == 0xF3) {
                /* 2-byte MTC Quarter Frame / Song Select */
                dataLength = 2;
            } else if (status >= 0x80 && status <= 0xEF) {
                /* Channel voice message wrapped in CIN 0x0F */
                UInt8 msgCin = (status >> 4) & 0x0F;
                dataLength = (msgCin == 0x0C || msgCin == 0x0D) ? 2 : 3;
            }

            if (dataLength > 0 && (1 + dataLength) <= remaining) {
                const UInt8 *midiBytes = &ptr[1];
                Boolean skip = false;
                if (dataLength == 1) {
                    if (status == 0xFE && filterActiveSensing) skip = true;
                    if (status == 0xF8 && filterClock) skip = true;
                }

                if (!skip) {
                    if (status >= 0x80 && status <= 0xEF) {
                        ProcessAndAppendChannelMessage(
                            driver, pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets,
                            midiBytes, dataLength
                        );
                    } else {
                        /* Pass original System Common / Real-Time / SysEx message */
                        AppendMIDIMessage(
                            pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets,
                            midiBytes, dataLength
                        );

                        /* Also emit Logic Pro Control Surface Transport CCs if enabled */
                        if (translateTransport) {
                            if (status == 0xFA || status == 0xFB) {
                                /* Start / Continue -> Play (CC 119 / 0x77) */
                                EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x77);
                            } else if (status == 0xFC) {
                                /* Stop -> Stop (CC 118 / 0x76) */
                                EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x76);
                            } else if (status == 0xF2 && dataLength >= 3) {
                                /* Song Position Pointer -> Top (0x73), Rewind (0x74), Forward (0x75) */
                                int pos = ((int)midiBytes[2] << 7) | (int)midiBytes[1];
                                int prev = driver ? driver->lastSongPosition : 0;
                                if (driver) driver->lastSongPosition = pos;
                                if (pos == 0) {
                                    EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x73);
                                } else if (pos > prev) {
                                    EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x75);
                                } else {
                                    EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x74);
                                }
                            } else if (status == 0xF0 && dataLength >= 6) {
                                /* Check for MMC SysEx: F0 7F <dev> 06 <cmd> F7 */
                                if (midiBytes[1] == 0x7F && midiBytes[3] == 0x06) {
                                    UInt8 cmd = midiBytes[4];
                                    if (cmd == 0x01) EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x76); /* Stop */
                                    else if (cmd == 0x02 || cmd == 0x03) EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x77); /* Play */
                                    else if (cmd == 0x04) EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x75); /* Fast Forward */
                                    else if (cmd == 0x05) EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x74); /* Rewind */
                                    else if (cmd == 0x06 || cmd == 0x07) EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x72); /* Record */
                                    else if (cmd == 0x44) EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x73); /* Locate/Top */
                                } else if (midiBytes[1] == 0x00 && midiBytes[2] == 0x00) {
                                    /* CME proprietary Record/Transport SysEx */
                                    EmitTransportCC(pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets, 0x72);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            int dataLength = kUSBMIDICINLength[cin];
            if (dataLength > 0 && (1 + dataLength) <= remaining) {
                const UInt8 *midiBytes = &ptr[1];
                if (midiBytes[0] >= 0x80 && midiBytes[0] <= 0xEF) {
                    ProcessAndAppendChannelMessage(
                        driver, pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets,
                        midiBytes, dataLength
                    );
                } else {
                    AppendMIDIMessage(
                        pktList, sizeof(pktBuf), &pkt, &timestamp, &hasPackets,
                        midiBytes, dataLength
                    );
                }
            }
        }

        if (sourceLength <= 0) {
            sourceLength = 4;
        }
        ptr += sourceLength;
        remaining -= sourceLength;
    }

    if (hasPackets) {
        MIDIReceived(midiSource, pktList);
    }
}

void CMEParseUSBMIDIInput(
    const UInt8 *buffer,
    int bufferLength,
    Boolean filterActiveSensing,
    Boolean filterClock,
    MIDIEndpointRef midiSource
) {
    CMEParseUSBMIDIInputWithDriver(NULL, buffer, bufferLength, filterActiveSensing, filterClock, midiSource);
}

/* ==========================================================================
 * Standard USB-MIDI Output Encoder
 * ========================================================================== */

static int EncodeMIDIToUSBPackets(
    const UInt8 *midiData,
    int midiLen,
    UInt8 *outBuf,
    int maxOutLen
) {
    int outIdx = 0;
    int i = 0;

    while (i < midiLen && (outIdx + 4) <= maxOutLen) {
        UInt8 status = midiData[i];

        if (status == 0xF0) {
            /* SysEx message */
            int sysexEnd = i + 1;
            while (sysexEnd < midiLen && midiData[sysexEnd] != 0xF7) {
                sysexEnd++;
            }
            if (sysexEnd < midiLen && midiData[sysexEnd] == 0xF7) {
                sysexEnd++; /* include 0xF7 */
            }
            int sysexLen = sysexEnd - i;
            int pos = i;
            while (sysexLen > 0 && (outIdx + 4) <= maxOutLen) {
                if (sysexLen > 3) {
                    outBuf[outIdx + 0] = 0x04;
                    outBuf[outIdx + 1] = midiData[pos + 0];
                    outBuf[outIdx + 2] = midiData[pos + 1];
                    outBuf[outIdx + 3] = midiData[pos + 2];
                    pos += 3;
                    sysexLen -= 3;
                } else if (sysexLen == 3) {
                    outBuf[outIdx + 0] = 0x07;
                    outBuf[outIdx + 1] = midiData[pos + 0];
                    outBuf[outIdx + 2] = midiData[pos + 1];
                    outBuf[outIdx + 3] = midiData[pos + 2];
                    pos += 3;
                    sysexLen = 0;
                } else if (sysexLen == 2) {
                    outBuf[outIdx + 0] = 0x06;
                    outBuf[outIdx + 1] = midiData[pos + 0];
                    outBuf[outIdx + 2] = midiData[pos + 1];
                    outBuf[outIdx + 3] = 0x00;
                    pos += 2;
                    sysexLen = 0;
                } else {
                    outBuf[outIdx + 0] = 0x05;
                    outBuf[outIdx + 1] = midiData[pos + 0];
                    outBuf[outIdx + 2] = 0x00;
                    outBuf[outIdx + 3] = 0x00;
                    pos += 1;
                    sysexLen = 0;
                }
                outIdx += 4;
            }
            i = sysexEnd;
        } else if (status >= 0xF8) {
            /* 1-byte System Real-Time */
            outBuf[outIdx + 0] = 0x0F;
            outBuf[outIdx + 1] = status;
            outBuf[outIdx + 2] = 0x00;
            outBuf[outIdx + 3] = 0x00;
            outIdx += 4;
            i += 1;
        } else if (status == 0xF2 && (i + 2) < midiLen) {
            /* 3-byte Song Position Pointer */
            outBuf[outIdx + 0] = 0x03;
            outBuf[outIdx + 1] = status;
            outBuf[outIdx + 2] = midiData[i + 1];
            outBuf[outIdx + 3] = midiData[i + 2];
            outIdx += 4;
            i += 3;
        } else if ((status == 0xF1 || status == 0xF3) && (i + 1) < midiLen) {
            /* 2-byte System Common */
            outBuf[outIdx + 0] = 0x02;
            outBuf[outIdx + 1] = status;
            outBuf[outIdx + 2] = midiData[i + 1];
            outBuf[outIdx + 3] = 0x00;
            outIdx += 4;
            i += 2;
        } else if (status >= 0x80 && status <= 0xEF) {
            UInt8 cin = (status >> 4) & 0x0F;
            int msgLen = (cin == 0x0C || cin == 0x0D) ? 2 : 3;
            if ((i + msgLen) <= midiLen) {
                outBuf[outIdx + 0] = cin;
                outBuf[outIdx + 1] = midiData[i + 0];
                outBuf[outIdx + 2] = (msgLen >= 2) ? midiData[i + 1] : 0x00;
                outBuf[outIdx + 3] = (msgLen >= 3) ? midiData[i + 2] : 0x00;
                outIdx += 4;
                i += msgLen;
            } else {
                break;
            }
        } else {
            /* Unknown or bare data byte */
            i++;
        }
    }

    return outIdx;
}

/* ==========================================================================
 * Asynchronous USB Bulk IN Read Loop
 * ========================================================================== */

static void USBReadCompletion(void *refCon, IOReturn result, void *arg0) {
    CMEUFPipeContext *ctx = (CMEUFPipeContext *)refCon;
    if (!ctx || !ctx->driver) return;

    CMEUFDriver *driver = ctx->driver;
    if (!driver->running || !driver->usbConnected || !driver->usbInterface) {
        return;
    }

    if (result == kIOReturnAborted || result == kIOReturnNoDevice || result == kIOReturnNotOpen) {
        return;
    }

    UInt32 bytesRead = (UInt32)(uintptr_t)arg0;

    if (result != kIOReturnSuccess) {
        if (result == kIOUSBPipeStalled) {
            (*driver->usbInterface)->ClearPipeStallBothEnds(driver->usbInterface, ctx->pipeRef);
        }
    } else if (bytesRead >= 4) {
        CMEParseUSBMIDIInputWithDriver(
            driver,
            ctx->buffer,
            (int)bytesRead,
            driver->filterActiveSensing,
            driver->filterClock,
            driver->midiSource
        );
    }
    /*
     * Note on bytesRead == 0 (Zero-Length Packet):
     * Matching ESIMidiport.sys (sub_140012080) and Linux snd-usb-audio:
     * DO NOT sleep here! The CME UF5's Philips PDIUSBD12 Endpoint 2 IN (0x82)
     * is double-buffered (only 2 banks deep) and its 8051 MCU validates a ZLP
     * every ~1.5 ms when idle. Once both banks are drained, the PDIUSBD12
     * hardware automatically NAKs until the next ~1.5 ms tick. Sleeping even
     * 2 ms allows both hardware banks to fill with ZLPs, causing the 8051 to
     * drop Note On/Off events during key presses. Immediately re-queuing the
     * URB keeps the xHCI ring populated with zero drops and < 0.3% CPU usage.
     */

    if (driver->running && driver->usbConnected && driver->usbInterface) {
        (*driver->usbInterface)->ReadPipeAsync(
            driver->usbInterface,
            ctx->pipeRef,
            ctx->buffer,
            ctx->maxPacketSize,
            USBReadCompletion,
            ctx
        );
    }
}

static void *USBIOThreadMain(void *arg) {
    CMEUFDriver *driver = (CMEUFDriver *)arg;

    /* Elevate USB I/O RunLoop thread to interactive real-time priority */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    driver->ioRunLoop = CFRunLoopGetCurrent();
    CFRetain(driver->ioRunLoop);

    if (driver->usbInterface) {
        IOReturn kr = (*driver->usbInterface)->CreateInterfaceAsyncEventSource(
            driver->usbInterface, &driver->usbAsyncSource
        );
        if (kr == kIOReturnSuccess && driver->usbAsyncSource) {
            CFRunLoopAddSource(driver->ioRunLoop, driver->usbAsyncSource, kCFRunLoopDefaultMode);

            /*
             * Per ESIMidiport.sys (sub_140012930 / sub_14001A380):
             * Only poll Endpoint 2 IN (0x82, 64B) — Endpoint 1 (0x81, 16B) is
             * unused on CME UF keyboards. Queue CME_NUM_INPUT_URBS concurrent
             * Bulk IN transfers so the xHCI hardware ring never runs dry.
             */
            if (driver->inPipe2 != 0) {
                for (UInt32 i = 0; i < CME_NUM_INPUT_URBS; i++) {
                    CMEUFPipeContext *ctx = &driver->inUrbs[i];
                    ctx->driver = driver;
                    ctx->pipeRef = driver->inPipe2;
                    ctx->maxPacketSize = CME_BULK_PACKET_SIZE;
                    ctx->urbIndex = i;
                    (*driver->usbInterface)->ReadPipeAsync(
                        driver->usbInterface,
                        driver->inPipe2,
                        ctx->buffer,
                        ctx->maxPacketSize,
                        USBReadCompletion,
                        ctx
                    );
                }
            }

            CFRunLoopRun();

            if (driver->usbAsyncSource) {
                CFRunLoopRemoveSource(driver->ioRunLoop, driver->usbAsyncSource, kCFRunLoopDefaultMode);
                CFRelease(driver->usbAsyncSource);
                driver->usbAsyncSource = NULL;
            }
        }
    }

    CFRelease(driver->ioRunLoop);
    driver->ioRunLoop = NULL;
    return NULL;
}

/* ==========================================================================
 * USB Device Connection & Disconnection
 * ========================================================================== */

static Boolean OpenUSBDevice(CMEUFDriver *driver, io_service_t service) {
    LoadDriverPreferences(driver);

    IOCFPlugInInterface **plugInInterface = NULL;
    SInt32 score = 0;

    kern_return_t kr = IOCreatePlugInInterfaceForService(
        service,
        kIOUSBInterfaceUserClientTypeID,
        kIOCFPlugInInterfaceID,
        &plugInInterface,
        &score
    );
    if (kr != kIOReturnSuccess || !plugInInterface) {
        syslog(LOG_ERR, "CMEUFMIDIDriver: IOCreatePlugInInterfaceForService failed: 0x%x", kr);
        return false;
    }

    IOUSBInterfaceInterface300 **intf = NULL;
    HRESULT res = (*plugInInterface)->QueryInterface(
        plugInInterface,
        CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
        (LPVOID *)&intf
    );
    (*plugInInterface)->Release(plugInInterface);

    if (res != S_OK || !intf) {
        syslog(LOG_ERR, "CMEUFMIDIDriver: QueryInterface(IOUSBInterfaceInterface300) failed: 0x%x", (int)res);
        return false;
    }

    /* Retry opening interface briefly in case of transient contention */
    for (int attempt = 0; attempt < 5; attempt++) {
        kr = (*intf)->USBInterfaceOpen(intf);
        if (kr == kIOReturnSuccess) break;
        usleep(50000);
    }

    if (kr != kIOReturnSuccess) {
        syslog(LOG_ERR, "CMEUFMIDIDriver: USBInterfaceOpen failed: 0x%x", kr);
        (*intf)->Release(intf);
        return false;
    }

    UInt8 numEndpoints = 0;
    (*intf)->GetNumEndpoints(intf, &numEndpoints);

    driver->inPipe1 = 0;
    driver->outPipe1 = 0;
    driver->inPipe2 = 0;
    driver->outPipe2 = 0;

    for (UInt8 pipeRef = 1; pipeRef <= numEndpoints; pipeRef++) {
        UInt8 direction = 0, number = 0, transferType = 0, interval = 0;
        UInt16 maxPacketSize = 0;
        if ((*intf)->GetPipeProperties(intf, pipeRef, &direction, &number, &transferType, &maxPacketSize, &interval) == kIOReturnSuccess) {
            if (transferType == kUSBBulk || transferType == kUSBInterrupt) {
                if (direction == kUSBIn) {
                    if (number == 1) driver->inPipe1 = pipeRef;
                    else if (number == 2 || driver->inPipe2 == 0) driver->inPipe2 = pipeRef;
                } else if (direction == kUSBOut) {
                    if (number == 1) driver->outPipe1 = pipeRef;
                    else if (number == 2 || driver->outPipe2 == 0) driver->outPipe2 = pipeRef;
                }
            }
        }
    }

    /*
     * Reset DATA0/DATA1 toggle and clear any stale stall state on both ends of
     * the main MIDI IN (0x82) and OUT (0x02) pipes, matching ESIMidiport.sys
     * (sub_14001A270: URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL).
     */
    if (driver->inPipe2 != 0) {
        (*intf)->ClearPipeStallBothEnds(intf, driver->inPipe2);
    }
    if (driver->outPipe2 != 0) {
        (*intf)->ClearPipeStallBothEnds(intf, driver->outPipe2);
    }

    driver->usbInterface = intf;
    driver->usbService = service;
    IOObjectRetain(service);
    driver->usbConnected = true;

    /* Ensure MIDIDeviceRef exists in MIDISetup */
    if (driver->midiDevice == 0) {
        MIDIDeviceListRef devList = MIDIGetDriverDeviceList(&driver->vtbl);
        if (devList) {
            if (MIDIDeviceListGetNumberOfDevices(devList) > 0) {
                driver->midiDevice = MIDIDeviceListGetDevice(devList, 0);
            }
            MIDIDeviceListDispose(devList);
        }
    }

    if (driver->midiDevice == 0) {
        OSStatus err = MIDIDeviceCreate(
            &driver->vtbl,
            CFSTR("CME UF5"),
            CFSTR("CME"),
            CFSTR("UF5"),
            &driver->midiDevice
        );
        if (err == noErr && driver->midiDevice != 0) {
            MIDIEntityRef entity = 0;
            MIDIDeviceAddEntity(driver->midiDevice, CFSTR("MIDI"), false, 1, 1, &entity);
            MIDISetupAddDevice(driver->midiDevice);
        }
    }

    if (driver->midiDevice != 0) {
        if (MIDIDeviceGetNumberOfEntities(driver->midiDevice) == 0) {
            MIDIEntityRef entity = 0;
            MIDIDeviceAddEntity(driver->midiDevice, CFSTR("MIDI"), false, 1, 1, &entity);
        }
        MIDIEntityRef entity = MIDIDeviceGetEntity(driver->midiDevice, 0);
        if (entity != 0) {
            driver->midiSource = MIDIEntityGetSource(entity, 0);
            driver->midiDest = MIDIEntityGetDestination(entity, 0);
            if (driver->midiDest != 0) {
                MIDIEndpointSetRefCons(driver->midiDest, driver, (void *)(uintptr_t)driver->outPipe2);
            }
        }
        MIDIObjectSetIntegerProperty(driver->midiDevice, kMIDIPropertyOffline, 0);
    }

    if (pthread_create(&driver->ioThread, NULL, USBIOThreadMain, driver) == 0) {
        driver->ioThreadActive = true;
    }

    syslog(LOG_NOTICE, "CMEUFMIDIDriver: Connected CME UF5 (inPipe1=%u, outPipe1=%u, inPipe2=%u, outPipe2=%u)",
           driver->inPipe1, driver->outPipe1, driver->inPipe2, driver->outPipe2);
    return true;
}

static void CloseUSBDevice(CMEUFDriver *driver) {
    pthread_mutex_lock(&driver->lock);
    if (!driver->usbConnected) {
        pthread_mutex_unlock(&driver->lock);
        return;
    }
    driver->usbConnected = false;

    if (driver->usbInterface) {
        if (driver->inPipe1) (*driver->usbInterface)->AbortPipe(driver->usbInterface, driver->inPipe1);
        if (driver->inPipe2) (*driver->usbInterface)->AbortPipe(driver->usbInterface, driver->inPipe2);
        if (driver->outPipe1) (*driver->usbInterface)->AbortPipe(driver->usbInterface, driver->outPipe1);
        if (driver->outPipe2) (*driver->usbInterface)->AbortPipe(driver->usbInterface, driver->outPipe2);
    }

    if (driver->ioRunLoop) {
        CFRunLoopStop(driver->ioRunLoop);
    }
    pthread_mutex_unlock(&driver->lock);

    if (driver->ioThreadActive) {
        pthread_join(driver->ioThread, NULL);
        driver->ioThreadActive = false;
    }

    pthread_mutex_lock(&driver->lock);
    if (driver->usbInterface) {
        (*driver->usbInterface)->USBInterfaceClose(driver->usbInterface);
        (*driver->usbInterface)->Release(driver->usbInterface);
        driver->usbInterface = NULL;
    }
    if (driver->usbService) {
        IOObjectRelease(driver->usbService);
        driver->usbService = 0;
    }
    if (driver->midiDevice != 0) {
        MIDIObjectSetIntegerProperty(driver->midiDevice, kMIDIPropertyOffline, 1);
    }
    pthread_mutex_unlock(&driver->lock);

    syslog(LOG_NOTICE, "CMEUFMIDIDriver: Disconnected CME UF5");
}

static void DevicesAdded(void *refCon, io_iterator_t iterator) {
    CMEUFDriver *driver = (CMEUFDriver *)refCon;
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != 0) {
        if (!driver->usbConnected && driver->running) {
            OpenUSBDevice(driver, service);
        }
        IOObjectRelease(service);
    }
}

static void DevicesRemoved(void *refCon, io_iterator_t iterator) {
    CMEUFDriver *driver = (CMEUFDriver *)refCon;
    io_service_t service;
    Boolean removed = false;
    while ((service = IOIteratorNext(iterator)) != 0) {
        removed = true;
        IOObjectRelease(service);
    }
    if (removed && driver->usbConnected) {
        CloseUSBDevice(driver);
    }
}

static CFMutableDictionaryRef CreateCMEInterfaceMatchingDict(void) {
    CFMutableDictionaryRef matchingDict = IOServiceMatching("IOUSBHostInterface");
    if (!matchingDict) return NULL;

    int vid = CME_USB_VID;
    int pid = CME_USB_PID;
    int ifnum = CME_USB_INTERFACE_NUM;
    int cfg = CME_USB_CONFIG_VALUE;

    CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    CFNumberRef ifRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &ifnum);
    CFNumberRef cfgRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &cfg);

    CFDictionarySetValue(matchingDict, CFSTR(kUSBVendorID), vidRef);
    CFDictionarySetValue(matchingDict, CFSTR(kUSBProductID), pidRef);
    CFDictionarySetValue(matchingDict, CFSTR(kUSBInterfaceNumber), ifRef);
    CFDictionarySetValue(matchingDict, CFSTR(kUSBConfigurationValue), cfgRef);

    CFRelease(vidRef);
    CFRelease(pidRef);
    CFRelease(ifRef);
    CFRelease(cfgRef);

    return matchingDict;
}

/* ==========================================================================
 * MIDIDriverInterface Methods
 * ========================================================================== */

static HRESULT DriverQueryInterface(void *thisPointer, REFIID iid, LPVOID *ppv) {
    CMEUFDriver *driver = (CMEUFDriver *)thisPointer;
    if (!ppv) return E_POINTER;
    *ppv = NULL;

    CFUUIDRef interfaceID = CFUUIDCreateFromUUIDBytes(NULL, iid);
    if (!interfaceID) return E_NOINTERFACE;

    if (CFEqual(interfaceID, IUnknownUUID) || CFEqual(interfaceID, kMIDIDriverInterfaceID)) {
        driver->vtbl->AddRef(thisPointer);
        driver->interfaceVersion = 1;
        *ppv = thisPointer;
        CFRelease(interfaceID);
        return S_OK;
    }

    CFRelease(interfaceID);
    return E_NOINTERFACE;
}

static ULONG DriverAddRef(void *thisPointer) {
    CMEUFDriver *driver = (CMEUFDriver *)thisPointer;
    return ++driver->refCount;
}

static ULONG DriverRelease(void *thisPointer) {
    CMEUFDriver *driver = (CMEUFDriver *)thisPointer;
    UInt32 newCount = --driver->refCount;
    if (newCount == 0) {
        if (driver->factoryID) {
            CFPlugInRemoveInstanceForFactory(driver->factoryID);
            CFRelease(driver->factoryID);
        }
        pthread_mutex_destroy(&driver->lock);
        free(driver);
    }
    return newCount;
}

static OSStatus DriverFindDevices(MIDIDriverRef self, MIDIDeviceListRef devList) {
    return noErr;
}

static OSStatus DriverStart(MIDIDriverRef self, MIDIDeviceListRef devList) {
    CMEUFDriver *driver = (CMEUFDriver *)self;
    syslog(LOG_NOTICE, "CMEUFMIDIDriver: Start called");

    LoadDriverPreferences(driver);
    driver->running = true;
    driver->serverRunLoop = CFRunLoopGetCurrent();

    /* Subscribe to live configuration changes from CME UF5 Control.app */
    CFNotificationCenterRef darwinCenter = CFNotificationCenterGetDarwinNotifyCenter();
    if (darwinCenter) {
        CFNotificationCenterAddObserver(
            darwinCenter,
            driver,
            OnConfigChangedNotification,
            CME_CONFIG_NOTIFY_NAME,
            NULL,
            CFNotificationSuspensionBehaviorDeliverImmediately
        );
    }

    /* Mark any previously saved devices offline until matched */
    ItemCount numDevs = MIDIDeviceListGetNumberOfDevices(devList);
    for (ItemCount i = 0; i < numDevs; i++) {
        MIDIDeviceRef dev = MIDIDeviceListGetDevice(devList, i);
        if (dev != 0) {
            MIDIObjectSetIntegerProperty(dev, kMIDIPropertyOffline, 1);
            if (i == 0) {
                driver->midiDevice = dev;
            }
        }
    }

    driver->notifyPort = IONotificationPortCreate(kIOMainPortDefault);
    if (driver->notifyPort) {
        CFRunLoopSourceRef notifySource = IONotificationPortGetRunLoopSource(driver->notifyPort);
        if (notifySource && driver->serverRunLoop) {
            CFRunLoopAddSource(driver->serverRunLoop, notifySource, kCFRunLoopDefaultMode);
        }

        CFMutableDictionaryRef matchDict1 = CreateCMEInterfaceMatchingDict();
        if (matchDict1) {
            IOServiceAddMatchingNotification(
                driver->notifyPort,
                kIOMatchedNotification,
                matchDict1,
                DevicesAdded,
                driver,
                &driver->matchedIter
            );
            DevicesAdded(driver, driver->matchedIter);
        }

        CFMutableDictionaryRef matchDict2 = CreateCMEInterfaceMatchingDict();
        if (matchDict2) {
            IOServiceAddMatchingNotification(
                driver->notifyPort,
                kIOTerminatedNotification,
                matchDict2,
                DevicesRemoved,
                driver,
                &driver->terminatedIter
            );
            DevicesRemoved(driver, driver->terminatedIter);
        }
    }

    return noErr;
}

static OSStatus DriverStop(MIDIDriverRef self) {
    CMEUFDriver *driver = (CMEUFDriver *)self;
    syslog(LOG_NOTICE, "CMEUFMIDIDriver: Stop called");

    CFNotificationCenterRef darwinCenter = CFNotificationCenterGetDarwinNotifyCenter();
    if (darwinCenter) {
        CFNotificationCenterRemoveObserver(darwinCenter, driver, CME_CONFIG_NOTIFY_NAME, NULL);
    }

    driver->running = false;
    CloseUSBDevice(driver);

    if (driver->matchedIter) {
        IOObjectRelease(driver->matchedIter);
        driver->matchedIter = 0;
    }
    if (driver->terminatedIter) {
        IOObjectRelease(driver->terminatedIter);
        driver->terminatedIter = 0;
    }
    if (driver->notifyPort) {
        IONotificationPortDestroy(driver->notifyPort);
        driver->notifyPort = NULL;
    }

    return noErr;
}

static OSStatus DriverConfigure(MIDIDriverRef self, MIDIDeviceRef device) {
    return noErr;
}

static OSStatus DriverSend(
    MIDIDriverRef self,
    const MIDIPacketList *pktlist,
    void *destRefCon1,
    void *destRefCon2
) {
    CMEUFDriver *driver = (CMEUFDriver *)self;
    if (!driver->running || !driver->usbConnected || !driver->usbInterface || !pktlist) {
        return kMIDIUnknownEndpoint;
    }

    UInt8 outPipe = driver->outPipe2 ? driver->outPipe2 : driver->outPipe1;
    if (outPipe == 0) {
        return kMIDIUnknownEndpoint;
    }

    UInt8 usbBuf[256];
    const MIDIPacket *pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++) {
        int usbLen = EncodeMIDIToUSBPackets(pkt->data, pkt->length, usbBuf, sizeof(usbBuf));
        if (usbLen > 0 && driver->usbConnected && driver->usbInterface) {
            (*driver->usbInterface)->WritePipe(driver->usbInterface, outPipe, usbBuf, usbLen);
        }
        pkt = MIDIPacketNext(pkt);
    }

    return noErr;
}

static OSStatus DriverEnableSource(MIDIDriverRef self, MIDIEndpointRef src, Boolean enabled) {
    return noErr;
}

static OSStatus DriverFlush(
    MIDIDriverRef self,
    MIDIEndpointRef dest,
    void *destRefCon1,
    void *destRefCon2
) {
    return noErr;
}

static OSStatus DriverMonitor(
    MIDIDriverRef self,
    MIDIEndpointRef dest,
    const MIDIPacketList *pktlist
) {
    return noErr;
}

static OSStatus DriverSendPackets(
    MIDIDriverRef self,
    const MIDIEventList *pktlist,
    void *destRefCon1,
    void *destRefCon2
) {
    return noErr;
}

static OSStatus DriverMonitorEvents(
    MIDIDriverRef self,
    MIDIEndpointRef dest,
    const MIDIEventList *pktlist
) {
    return noErr;
}

static MIDIDriverInterface gCMEUFMIDIDriverVtbl = {
    NULL,
    DriverQueryInterface,
    DriverAddRef,
    DriverRelease,
    DriverFindDevices,
    DriverStart,
    DriverStop,
    DriverConfigure,
    DriverSend,
    DriverEnableSource,
    DriverFlush,
    DriverMonitor,
    DriverSendPackets,
    DriverMonitorEvents
};

/* ==========================================================================
 * CFPlugIn Factory Entry Point
 * ========================================================================== */

__attribute__((visibility("default")))
void *NewCMEUFMIDIDriver(CFAllocatorRef allocator, CFUUIDRef typeID) {
    if (!typeID || !CFEqual(typeID, kMIDIDriverTypeID)) {
        return NULL;
    }

    CMEUFDriver *driver = (CMEUFDriver *)calloc(1, sizeof(CMEUFDriver));
    if (!driver) {
        return NULL;
    }

    driver->vtbl = &gCMEUFMIDIDriverVtbl;
    driver->factoryID = CFRetain(kCMEUFMIDIDriverFactoryID);
    driver->refCount = 1;
    driver->interfaceVersion = 2;
    LoadDriverPreferences(driver);

    pthread_mutex_init(&driver->lock, NULL);
    CFPlugInAddInstanceForFactory(driver->factoryID);

    openlog("CMEUFMIDIDriver", LOG_PID, LOG_USER);
    syslog(LOG_NOTICE, "CMEUFMIDIDriver: Instance created");

    return &driver->vtbl;
}
