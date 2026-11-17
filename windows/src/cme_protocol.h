#ifndef CME_PROTOCOL_H
#define CME_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CME_USB_VID             0x7104
#define CME_USB_PID             0x2202
#define CME_ENDPOINT_IN         0x82    /* 64-byte Bulk IN (Endpoint 2) */
#define CME_ENDPOINT_OUT        0x02    /* 64-byte Bulk OUT (Endpoint 2) */
#define CME_BULK_PACKET_SIZE    64

/* Transport CC definitions matching DAW control surfaces */
#define CME_TRANSPORT_RECORD    114     /* 0x72 */
#define CME_TRANSPORT_TOP       115     /* 0x73 Return to Zero / Marker */
#define CME_TRANSPORT_REWIND    116     /* 0x74 */
#define CME_TRANSPORT_FORWARD   117     /* 0x75 */
#define CME_TRANSPORT_STOP      118     /* 0x76 */
#define CME_TRANSPORT_PLAY      119     /* 0x77 */

/* Velocity Curves */
enum {
    CME_VELOCITY_LINEAR  = 0,
    CME_VELOCITY_SOFT    = 1,
    CME_VELOCITY_HARD    = 2,
    CME_VELOCITY_SCURVE  = 3,
    CME_VELOCITY_FIXED   = 4
};

typedef struct {
    int  velocityCurve;         /* 0=Linear, 1=Soft, 2=Hard, 3=S-Curve, 4=Fixed */
    int  fixedVelocity;         /* 1..127 */
    int  octaveShift;           /* -3..+3 */
    int  transposeSemitones;    /* -24..+24 */
    int  channelOverride;       /* 0=Pass, 1..16=Force Channel */
    int  faderMode;             /* 0=Multi-Ch (Ch 1..8 CC7), 1=Single-Ch (Ch 1 CC20..27) */
    int  transportMode;         /* 0=DAW Transport CC (114..119), 1=Raw Passthrough */
    int  pitchBendDeadzone;     /* 0..16 */
    bool filterActiveSensing;   /* Filter 0xFE */
    bool filterClock;           /* Filter 0xF8 */
    bool filterAftertouch;      /* Filter 0xA0 and 0xD0 */
} CMEConfig;

typedef struct {
    CMEConfig config;
    uint8_t   velocityLUT[128];
    int       lastSongPosition;
} CMEProtocolState;

/* Callback signature when decoded MIDI packet is produced */
typedef void (*CMEMIDICallback)(const uint8_t *data, int length, void *userData);

void CME_ProtocolInit(CMEProtocolState *state, const CMEConfig *config);
void CME_ProtocolUpdateConfig(CMEProtocolState *state, const CMEConfig *config);
uint8_t CME_CalculateVelocity(const CMEConfig *config, uint8_t input);

/*
 * Parse bulk USB data received from Endpoint 2 IN (0x82).
 * Invokes midiCallback for each valid decoded MIDI message.
 */
void CME_ParseUSBInput(
    CMEProtocolState *state,
    const uint8_t *buffer,
    int length,
    CMEMIDICallback midiCallback,
    void *userData
);

#ifdef __cplusplus
}
#endif

#endif /* CME_PROTOCOL_H */

