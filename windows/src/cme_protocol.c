#include "cme_protocol.h"
#include <math.h>
#include <string.h>

static void RebuildLUT(CMEProtocolState *state) {
    if (!state) return;
    state->velocityLUT[0] = 0;
    for (int i = 1; i <= 127; i++) {
        state->velocityLUT[i] = CME_CalculateVelocity(&state->config, (uint8_t)i);
    }
}

uint8_t CME_CalculateVelocity(const CMEConfig *cfg, uint8_t input) {
    if (input == 0) return 0;
    if (input > 127) input = 127;

    int curve = cfg ? cfg->velocityCurve : CME_VELOCITY_LINEAR;
    double norm = (double)input / 127.0;
    double out = norm;

    switch (curve) {
        case CME_VELOCITY_SOFT:   /* Concave curve - easier to hit high velocities */
            out = pow(norm, 0.55);
            break;
        case CME_VELOCITY_HARD:   /* Convex curve - harder, greater dynamic control */
            out = pow(norm, 1.75);
            break;
        case CME_VELOCITY_SCURVE: /* S-Curve (Smoothstep) */
            out = norm * norm * (3.0 - 2.0 * norm);
            break;
        case CME_VELOCITY_FIXED: {
            int fixed = cfg ? cfg->fixedVelocity : 100;
            if (fixed < 1) fixed = 1;
            if (fixed > 127) fixed = 127;
            return (uint8_t)fixed;
        }
        case CME_VELOCITY_LINEAR:
        default:
            out = norm;
            break;
    }

    int res = (int)round(out * 127.0);
    if (res < 1) res = 1;
    if (res > 127) res = 127;
    return (uint8_t)res;
}

void CME_ProtocolInit(CMEProtocolState *state, const CMEConfig *config) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    if (config) {
        state->config = *config;
    } else {
        state->config.velocityCurve = CME_VELOCITY_LINEAR;
        state->config.fixedVelocity = 100;
        state->config.octaveShift = 0;
        state->config.transposeSemitones = 0;
        state->config.channelOverride = 0;
        state->config.faderMode = 0;
        state->config.transportMode = 0;
        state->config.pitchBendDeadzone = 0;
        state->config.filterActiveSensing = true;
        state->config.filterClock = true;
        state->config.filterAftertouch = false;
    }
    RebuildLUT(state);
}

void CME_ProtocolUpdateConfig(CMEProtocolState *state, const CMEConfig *config) {
    if (!state || !config) return;
    state->config = *config;
    RebuildLUT(state);
}

static void EmitTransportCC(CMEMIDICallback cb, void *userData, uint8_t ccNumber) {
    if (!cb) return;
    uint8_t msg[3] = { 0xB0, ccNumber, 0x7F };
    cb(msg, 3, userData);
}

static void ProcessChannelMessage(
    CMEProtocolState *state,
    const uint8_t *rawMsg,
    int dataLength,
    CMEMIDICallback cb,
    void *userData
) {
    if (dataLength < 2 || !cb) return;
    uint8_t msg[3];
    msg[0] = rawMsg[0];
    msg[1] = rawMsg[1];
    msg[2] = (dataLength >= 3) ? rawMsg[2] : 0;

    uint8_t statusHigh = msg[0] & 0xF0;
    uint8_t ch = msg[0] & 0x0F;

    /* 1. Filter Aftertouch if enabled */
    if (state && state->config.filterAftertouch) {
        if (statusHigh == 0xD0 || statusHigh == 0xA0) {
            return;
        }
    }

    /* 2. Fader Mode & Channel Override */
    bool isMultiChannelFader = (statusHigh == 0xB0 && msg[1] == 0x07 && ch <= 7);
    if (state) {
        if (isMultiChannelFader && state->config.faderMode == 1) {
            /* Remap Faders 1..8 (Ch 1..8 CC 7) -> Ch 1 CC 20..27 */
            msg[1] = (uint8_t)(20 + ch);
            ch = (state->config.channelOverride > 0) ? (uint8_t)(state->config.channelOverride - 1) : 0;
            msg[0] = (uint8_t)(statusHigh | (ch & 0x0F));
        } else if (state->config.channelOverride > 0 && !isMultiChannelFader) {
            ch = (uint8_t)(state->config.channelOverride - 1);
            msg[0] = (uint8_t)(statusHigh | (ch & 0x0F));
        }
    }

    /* 3. Note On / Note Off / Poly Aftertouch: Octave, Transpose, Velocity Curve */
    if (state && (statusHigh == 0x80 || statusHigh == 0x90 || statusHigh == 0xA0)) {
        int note = (int)msg[1] + (state->config.octaveShift * 12) + state->config.transposeSemitones;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        msg[1] = (uint8_t)note;

        if (statusHigh == 0x90 && msg[2] > 0) {
            msg[2] = state->velocityLUT[msg[2] & 0x7F];
        }
    }

    /* 4. Pitch Bend Deadzone */
    if (state && statusHigh == 0xE0 && state->config.pitchBendDeadzone > 0 && dataLength >= 3) {
        int bend = ((int)msg[2] << 7) | (int)msg[1];
        int diff = bend - 8192;
        int threshold = state->config.pitchBendDeadzone * 64;
        if (diff > -threshold && diff < threshold) {
            msg[1] = 0x00;
            msg[2] = 0x40; /* 8192 center */
        }
    }

    cb(msg, dataLength, userData);
}

void CME_ParseUSBInput(
    CMEProtocolState *state,
    const uint8_t *buffer,
    int length,
    CMEMIDICallback cb,
    void *userData
) {
    if (!buffer || length < 4 || !cb) return;

    bool translateTransport = (state == NULL || state->config.transportMode == 0);
    bool filterActiveSensing = state ? state->config.filterActiveSensing : true;
    bool filterClock         = state ? state->config.filterClock : true;

    int remaining = length;
    const uint8_t *ptr = buffer;

    while (remaining >= 4) {
        int sourceLength = 4;
        uint8_t cin = ptr[0] & 0x0F;

        if (cin == 0x00) {
            /* Empty padding slot */
            ptr += 4;
            remaining -= 4;
            continue;
        }

        if (cin == 0x0F) {
            int dataLength = 1;
            uint8_t status = ptr[1];

            if (status == 0xF0) {
                /*
                 * Contiguous SysEx (QUIRK_MIDI_CME / ESIMidiport.sys):
                 * Starts at ptr[1] (0xF0) and continues until 0xF7.
                 */
                const uint8_t *tmp = ptr + 2;
                int tmpLen = remaining - 2;
                while (tmpLen > 1 && *tmp != 0xF7) {
                    tmp++;
                    tmpLen--;
                }
                dataLength = (int)(tmp - ptr);
                sourceLength = dataLength + 1;
            } else if (status == 0xF2) {
                /* Song Position Pointer (3 bytes) */
                dataLength = 3;
            } else if (status == 0xF1 || status == 0xF3) {
                /* MTC Quarter Frame / Song Select (2 bytes) */
                dataLength = 2;
            } else if (status >= 0x80 && status <= 0xEF) {
                /* Channel voice message wrapped in CIN 0x0F */
                uint8_t msgCin = (status >> 4) & 0x0F;
                dataLength = (msgCin == 0x0C || msgCin == 0x0D) ? 2 : 3;
            }

            if (dataLength > 0 && (1 + dataLength) <= remaining) {
                const uint8_t *midiBytes = &ptr[1];
                bool skip = false;
                if (dataLength == 1) {
                    if (status == 0xFE && filterActiveSensing) skip = true;
                    if (status == 0xF8 && filterClock) skip = true;
                }

                if (!skip) {
                    if (status >= 0x80 && status <= 0xEF) {
                        ProcessChannelMessage(state, midiBytes, dataLength, cb, userData);
                    } else {
                        /* System Common / Real-Time / SysEx */
                        cb(midiBytes, dataLength, userData);

                        /* Transport translation for DAWs */
                        if (translateTransport) {
                            if (status == 0xFA || status == 0xFB) {
                                /* Start / Continue -> Play CC 119 */
                                EmitTransportCC(cb, userData, CME_TRANSPORT_PLAY);
                            } else if (status == 0xFC) {
                                /* Stop -> Stop CC 118 */
                                EmitTransportCC(cb, userData, CME_TRANSPORT_STOP);
                            } else if (status == 0xF2 && dataLength >= 3) {
                                /* Song Position Pointer */
                                int pos = ((int)midiBytes[2] << 7) | (int)midiBytes[1];
                                int prev = state ? state->lastSongPosition : 0;
                                if (state) state->lastSongPosition = pos;
                                if (pos == 0) {
                                    EmitTransportCC(cb, userData, CME_TRANSPORT_TOP);
                                } else if (pos > prev) {
                                    EmitTransportCC(cb, userData, CME_TRANSPORT_FORWARD);
                                } else {
                                    EmitTransportCC(cb, userData, CME_TRANSPORT_REWIND);
                                }
                            } else if (status == 0xF0 && dataLength >= 6) {
                                /* MMC SysEx */
                                if (midiBytes[1] == 0x7F && midiBytes[3] == 0x06) {
                                    uint8_t cmd = midiBytes[4];
                                    if (cmd == 0x01) EmitTransportCC(cb, userData, CME_TRANSPORT_STOP);
                                    else if (cmd == 0x02 || cmd == 0x03) EmitTransportCC(cb, userData, CME_TRANSPORT_PLAY);
                                    else if (cmd == 0x04) EmitTransportCC(cb, userData, CME_TRANSPORT_FORWARD);
                                    else if (cmd == 0x05) EmitTransportCC(cb, userData, CME_TRANSPORT_REWIND);
                                    else if (cmd == 0x06 || cmd == 0x07) EmitTransportCC(cb, userData, CME_TRANSPORT_RECORD);
                                    else if (cmd == 0x44) EmitTransportCC(cb, userData, CME_TRANSPORT_TOP);
                                } else if (midiBytes[1] == 0x00 && midiBytes[2] == 0x00) {
                                    /* CME UF proprietary Transport SysEx */
                                    EmitTransportCC(cb, userData, CME_TRANSPORT_RECORD);
                                }
                            }
                        }
                    }
                }
            }

            ptr += sourceLength;
            remaining -= sourceLength;
            continue;
        }

        /* Standard 4-byte USB-MIDI packet */
        static const uint8_t cinLens[16] = { 0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1 };
        int cinLen = cinLens[cin];
        if (cinLen > 0) {
            uint8_t status = ptr[1];
            bool skip = false;
            if (cinLen == 1) {
                if (status == 0xFE && filterActiveSensing) skip = true;
                if (status == 0xF8 && filterClock) skip = true;
            }
            if (!skip) {
                if (status >= 0x80 && status <= 0xEF) {
                    ProcessChannelMessage(state, &ptr[1], cinLen, cb, userData);
                } else {
                    cb(&ptr[1], cinLen, userData);
                }
            }
        }

        ptr += 4;
        remaining -= 4;
    }
}

