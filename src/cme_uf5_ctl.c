/*
 * cme_uf5_ctl.c
 *
 * Companion CLI utility for the CME UF5 macOS CoreMIDI Driver:
 *   - cme-uf5-ctl status       : Inspect USB, CoreMIDI, App & Logic Pro status
 *   - cme-uf5-ctl monitor      : Real-time MIDI monitor for CME UF5
 *   - cme-uf5-ctl clock on|off : Enable/disable 0xF8 MIDI Clock passthrough
 *   - cme-uf5-ctl reload       : Restart MIDIServer to reload the driver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#define CME_USB_VID             0x7104
#define CME_USB_PID             0x2202
#define CME_DRIVER_PREFS_ID     CFSTR("com.cme-pro.midi.driver.uf")
#define CME_CONFIG_NOTIFY_NAME  CFSTR("com.cme-pro.midi.driver.uf.configChanged")

static volatile sig_atomic_t gStopMonitor = 0;

static void HandleSigInt(int sig) {
    (void)sig;
    gStopMonitor = 1;
}

static void CFStringToUTF8(CFStringRef str, char *buf, size_t bufLen) {
    if (!buf || bufLen == 0) return;
    buf[0] = '\0';
    if (!str) return;
    if (!CFStringGetCString(str, buf, (CFIndex)bufLen, kCFStringEncodingUTF8)) {
        buf[0] = '\0';
    }
}

static const char *NoteName(UInt8 note, char *buf, size_t bufLen) {
    static const char *names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int octave = (note / 12) - 1;
    snprintf(buf, bufLen, "%s%d", names[note % 12], octave);
    return buf;
}

static void PrintDecodedMIDIPacket(const MIDIPacket *pkt) {
    const UInt8 *d = pkt->data;
    UInt16 len = pkt->length;
    if (len == 0) return;

    UInt8 status = d[0];
    if (status >= 0x80 && status <= 0xEF) {
        UInt8 type = status & 0xF0;
        UInt8 ch = (status & 0x0F) + 1;
        char noteBuf[16];

        switch (type) {
            case 0x80:
                if (len >= 3) {
                    printf("Note Off        ch=%2u  note=%-4s (%3u)  vel=%3u",
                           ch, NoteName(d[1], noteBuf, sizeof(noteBuf)), d[1], d[2]);
                }
                break;
            case 0x90:
                if (len >= 3) {
                    if (d[2] == 0) {
                        printf("Note Off        ch=%2u  note=%-4s (%3u)  vel=  0",
                               ch, NoteName(d[1], noteBuf, sizeof(noteBuf)), d[1]);
                    } else {
                        printf("Note On         ch=%2u  note=%-4s (%3u)  vel=%3u",
                               ch, NoteName(d[1], noteBuf, sizeof(noteBuf)), d[1], d[2]);
                    }
                }
                break;
            case 0xA0:
                if (len >= 3) {
                    printf("Poly Pressure   ch=%2u  note=%-4s (%3u)  val=%3u",
                           ch, NoteName(d[1], noteBuf, sizeof(noteBuf)), d[1], d[2]);
                }
                break;
            case 0xB0:
                if (len >= 3) {
                    printf("Control Change  ch=%2u  cc=%3u           val=%3u",
                           ch, d[1], d[2]);
                }
                break;
            case 0xC0:
                if (len >= 2) {
                    printf("Program Change  ch=%2u  pgm=%3u", ch, d[1]);
                }
                break;
            case 0xD0:
                if (len >= 2) {
                    printf("Aftertouch      ch=%2u  val=%3u", ch, d[1]);
                }
                break;
            case 0xE0:
                if (len >= 3) {
                    int bend = ((int)d[2] << 7 | (int)d[1]) - 8192;
                    printf("Pitch Bend      ch=%2u  val=%+5d", ch, bend);
                }
                break;
        }
    } else if (status == 0xF0) {
        printf("SysEx           (%u bytes):", len);
        for (UInt16 i = 0; i < len; i++) {
            printf(" %02X", d[i]);
        }
    } else if (status == 0xF2 && len >= 3) {
        int pos = ((int)d[2] << 7) | (int)d[1];
        printf("Song Position   pos=%d", pos);
    } else if (status == 0xF8) {
        printf("Timing Clock    (0xF8)");
    } else if (status == 0xFA) {
        printf("Start           (0xFA)");
    } else if (status == 0xFB) {
        printf("Continue        (0xFB)");
    } else if (status == 0xFC) {
        printf("Stop            (0xFC)");
    } else if (status == 0xFE) {
        printf("Active Sensing  (0xFE)");
    } else {
        printf("MIDI            (%u bytes):", len);
        for (UInt16 i = 0; i < len; i++) {
            printf(" %02X", d[i]);
        }
    }

    printf("   [");
    for (UInt16 i = 0; i < len; i++) {
        printf("%s%02X", (i > 0 ? " " : ""), d[i]);
    }
    printf("]\n");
    fflush(stdout);
}

static void MonitorReadProc(const MIDIPacketList *pktlist, void *readProcRefCon, void *srcConnRefCon) {
    (void)readProcRefCon;
    (void)srcConnRefCon;
    const MIDIPacket *pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++) {
        PrintDecodedMIDIPacket(pkt);
        pkt = MIDIPacketNext(pkt);
    }
}

static int CmdStatus(void) {
    printf("=== CME UF5 macOS Suite Status ===\n\n");

    const char *home = getenv("HOME");
    struct stat st;

    /* 1. Check installed components */
    char userPluginPath[512], logicScriptPath[512], userAppPath[512];
    snprintf(userPluginPath, sizeof(userPluginPath),
             "%s/Library/Audio/MIDI Drivers/CMEUFMIDIDriver.plugin", home ? home : "");
    snprintf(logicScriptPath, sizeof(logicScriptPath),
             "%s/Music/Audio Music Apps/MIDI Device Scripts/CME/UF5.device/config.lua", home ? home : "");
    snprintf(userAppPath, sizeof(userAppPath),
             "%s/Applications/CME UF5 Control.app", home ? home : "");

    Boolean userInstalled = (stat(userPluginPath, &st) == 0);
    Boolean sysInstalled = (stat("/Library/Audio/MIDI Drivers/CMEUFMIDIDriver.plugin", &st) == 0);
    Boolean logicInstalled = (stat(logicScriptPath, &st) == 0) ||
                             (stat("/Library/Audio/MIDI Device Scripts/CME/UF5.device/config.lua", &st) == 0);
    Boolean appInstalled = (stat("/Applications/CME UF5 Control.app", &st) == 0) ||
                           (stat(userAppPath, &st) == 0);

    printf("1. Installed Components:\n");
    printf("   CoreMIDI Driver (~/Library/Audio/MIDI Drivers) : %s\n",
           userInstalled ? "INSTALLED" : (sysInstalled ? "INSTALLED (System)" : "not installed"));
    printf("   Control App     (/Applications/CME UF5 Control): %s\n",
           appInstalled ? "INSTALLED" : "not installed");
    printf("   Logic Surface   (MIDI Device Scripts/CME/UF5)  : %s\n",
           logicInstalled ? "INSTALLED" : "not installed");

    CFPreferencesAppSynchronize(CME_DRIVER_PREFS_ID);
    Boolean exists = false;
    Boolean filterClock = CFPreferencesGetAppBooleanValue(CFSTR("FilterClock"), CME_DRIVER_PREFS_ID, &exists);
    if (!exists) filterClock = true;
    Boolean filterAS = CFPreferencesGetAppBooleanValue(CFSTR("FilterActiveSensing"), CME_DRIVER_PREFS_ID, &exists);
    if (!exists) filterAS = true;
    CFIndex velCurve = CFPreferencesGetAppIntegerValue(CFSTR("VelocityCurve"), CME_DRIVER_PREFS_ID, &exists);
    if (!exists) velCurve = 0;
    CFIndex octShift = CFPreferencesGetAppIntegerValue(CFSTR("OctaveShift"), CME_DRIVER_PREFS_ID, &exists);
    if (!exists) octShift = 0;
    CFIndex semiShift = CFPreferencesGetAppIntegerValue(CFSTR("TransposeSemitones"), CME_DRIVER_PREFS_ID, &exists);
    if (!exists) semiShift = 0;

    static const char *kCurveNames[5] = { "Linear", "Soft", "Hard", "S-Curve", "Fixed" };
    const char *curveName = (velCurve >= 0 && velCurve <= 4) ? kCurveNames[velCurve] : "Linear";

    printf("   Velocity Curve / Pitch Shift                   : %s (Octave %+ld, Transpose %+ld)\n",
           curveName, (long)octShift, (long)semiShift);
    printf("   Filter 0xF8 Clock / 0xFE Active Sensing        : Clock=%s, ActiveSensing=%s\n\n",
           filterClock ? "ON" : "OFF", filterAS ? "ON" : "OFF");

    /* 2. Check USB hardware */
    printf("2. USB Hardware (VID 0x%04X / PID 0x%04X):\n", CME_USB_VID, CME_USB_PID);
    CFMutableDictionaryRef matchDict = IOServiceMatching("IOUSBHostDevice");
    int vid = CME_USB_VID, pid = CME_USB_PID;
    CFNumberRef vidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef pidRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    CFDictionarySetValue(matchDict, CFSTR(kUSBVendorID), vidRef);
    CFDictionarySetValue(matchDict, CFSTR(kUSBProductID), pidRef);
    CFRelease(vidRef);
    CFRelease(pidRef);

    io_iterator_t iter = 0;
    Boolean usbFound = false;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matchDict, &iter) == KERN_SUCCESS) {
        io_service_t dev;
        while ((dev = IOIteratorNext(iter)) != 0) {
            usbFound = true;
            CFNumberRef locRef = IORegistryEntryCreateCFProperty(dev, CFSTR("locationID"), kCFAllocatorDefault, 0);
            CFNumberRef addrRef = IORegistryEntryCreateCFProperty(dev, CFSTR("USB Address"), kCFAllocatorDefault, 0);
            int loc = 0, addr = 0;
            if (locRef) { CFNumberGetValue(locRef, kCFNumberIntType, &loc); CFRelease(locRef); }
            if (addrRef) { CFNumberGetValue(addrRef, kCFNumberIntType, &addr); CFRelease(addrRef); }
            printf("   Status         : CONNECTED (USB Address %d, Location 0x%08x)\n", addr, loc);
            IOObjectRelease(dev);
        }
        IOObjectRelease(iter);
    }
    if (!usbFound) {
        printf("   Status         : NOT CONNECTED\n");
    }
    printf("\n");

    /* 3. Check CoreMIDI devices & endpoints */
    printf("3. CoreMIDI Subsystem:\n");
    ItemCount numDevs = MIDIGetNumberOfDevices();
    Boolean cmeDevFound = false;
    for (ItemCount i = 0; i < numDevs; i++) {
        MIDIDeviceRef dev = MIDIGetDevice(i);
        CFStringRef nameRef = NULL, manufRef = NULL, modelRef = NULL;
        SInt32 offline = 0;
        MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &nameRef);
        MIDIObjectGetStringProperty(dev, kMIDIPropertyManufacturer, &manufRef);
        MIDIObjectGetStringProperty(dev, kMIDIPropertyModel, &modelRef);
        MIDIObjectGetIntegerProperty(dev, kMIDIPropertyOffline, &offline);

        char name[128], manuf[128], model[128];
        CFStringToUTF8(nameRef, name, sizeof(name));
        CFStringToUTF8(manufRef, manuf, sizeof(manuf));
        CFStringToUTF8(modelRef, model, sizeof(model));
        if (nameRef) CFRelease(nameRef);
        if (manufRef) CFRelease(manufRef);
        if (modelRef) CFRelease(modelRef);

        if (strstr(name, "CME") || strstr(manuf, "CME")) {
            cmeDevFound = true;
            printf("   MIDIDevice     : '%s' (%s %s) -> %s\n",
                   name, manuf, model, offline ? "OFFLINE" : "ONLINE");
        }
    }
    if (!cmeDevFound) {
        printf("   MIDIDevice     : Not registered in MIDISetup\n");
    }

    ItemCount numSrc = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < numSrc; i++) {
        MIDIEndpointRef ep = MIDIGetSource(i);
        CFStringRef nameRef = NULL;
        MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &nameRef);
        if (!nameRef) MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &nameRef);
        char name[128];
        CFStringToUTF8(nameRef, name, sizeof(name));
        if (nameRef) CFRelease(nameRef);
        printf("   MIDI Source    : '%s'\n", name);
    }

    ItemCount numDst = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < numDst; i++) {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        CFStringRef nameRef = NULL;
        MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &nameRef);
        if (!nameRef) MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &nameRef);
        char name[128];
        CFStringToUTF8(nameRef, name, sizeof(name));
        if (nameRef) CFRelease(nameRef);
        printf("   MIDI Dest      : '%s'\n", name);
    }

    return 0;
}

static int CmdMonitor(void) {
    MIDIClientRef client = 0;
    MIDIPortRef inPort = 0;
    OSStatus err = MIDIClientCreate(CFSTR("cme-uf5-ctl"), NULL, NULL, &client);
    if (err != noErr) {
        fprintf(stderr, "Failed to create CoreMIDI client: %d\n", (int)err);
        return 1;
    }
    err = MIDIInputPortCreate(client, CFSTR("Input"), MonitorReadProc, NULL, &inPort);
    if (err != noErr) {
        fprintf(stderr, "Failed to create CoreMIDI input port: %d\n", (int)err);
        return 1;
    }

    ItemCount numSrc = MIDIGetNumberOfSources();
    ItemCount connected = 0;
    for (ItemCount i = 0; i < numSrc; i++) {
        MIDIEndpointRef src = MIDIGetSource(i);
        CFStringRef nameRef = NULL;
        MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &nameRef);
        if (!nameRef) MIDIObjectGetStringProperty(src, kMIDIPropertyName, &nameRef);
        char name[128];
        CFStringToUTF8(nameRef, name, sizeof(name));
        if (nameRef) CFRelease(nameRef);

        if (MIDIPortConnectSource(inPort, src, NULL) == noErr) {
            printf("Connected to MIDI Source: '%s'\n", name);
            connected++;
        }
    }

    if (connected == 0) {
        fprintf(stderr, "No active MIDI sources found. Is the CME UF5 connected and powered on?\n");
        return 1;
    }

    printf("Listening for MIDI events (press Ctrl+C to stop)...\n");
    printf("------------------------------------------------------------------------\n");
    signal(SIGINT, HandleSigInt);

    while (!gStopMonitor) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
    }

    printf("\nStopped.\n");
    MIDIPortDispose(inPort);
    MIDIClientDispose(client);
    return 0;
}

static int CmdReload(void) {
    printf("Restarting CoreMIDI MIDIServer...\n");
    system("killall MIDIServer 2>/dev/null || true");
    usleep(600000);
    return CmdStatus();
}

static int CmdClock(const char *mode) {
    if (!mode || (strcmp(mode, "on") != 0 && strcmp(mode, "off") != 0)) {
        fprintf(stderr, "Usage: cme-uf5-ctl clock [on|off]\n");
        return 1;
    }
    Boolean filterClock = (strcmp(mode, "off") == 0);
    CFPreferencesSetAppValue(
        CFSTR("FilterClock"),
        filterClock ? kCFBooleanTrue : kCFBooleanFalse,
        CME_DRIVER_PREFS_ID
    );
    CFPreferencesAppSynchronize(CME_DRIVER_PREFS_ID);
    CFNotificationCenterRef darwinCenter = CFNotificationCenterGetDarwinNotifyCenter();
    if (darwinCenter) {
        CFNotificationCenterPostNotification(darwinCenter, CME_CONFIG_NOTIFY_NAME, NULL, NULL, true);
    }
    printf("0xF8 MIDI Clock passthrough set to: %s\n\n", filterClock ? "OFF (filtered)" : "ON (passthrough)");
    return CmdStatus();
}

static void Usage(const char *prog) {
    printf("Usage: %s <command>\n\n", prog);
    printf("Commands:\n");
    printf("  status          Show USB, CoreMIDI, App & Logic Pro status of CME UF5\n");
    printf("  monitor         Monitor incoming MIDI messages from CME UF5 in real time\n");
    printf("  clock on|off    Enable or disable 0xF8 MIDI Clock heartbeat passthrough\n");
    printf("  reload          Restart MIDIServer and reload the CME UF5 driver\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return CmdStatus();
    }
    if (strcmp(argv[1], "status") == 0) {
        return CmdStatus();
    } else if (strcmp(argv[1], "monitor") == 0) {
        return CmdMonitor();
    } else if (strcmp(argv[1], "reload") == 0) {
        return CmdReload();
    } else if (strcmp(argv[1], "clock") == 0) {
        return CmdClock(argc >= 3 ? argv[2] : NULL);
    } else {
        Usage(argv[0]);
        return 1;
    }
}
