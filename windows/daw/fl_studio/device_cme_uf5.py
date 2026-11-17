# name=CME UF Series Controller
# supportedHardware=CME UF5, CME UF6, CME UF7, CME UF8
# url=https://github.com/cme-pro

import transport
import mixer
import general
import midi

def OnInit():
    print("CME UF Series MIDI Controller initialized for FL Studio!")

def OnDeInit():
    pass

def OnMidiMsg(event):
    event.handled = False

    # Check for Control Change (0xB0 .. 0xBF)
    if (event.status & 0xF0) == 0xB0:
        ch = (event.status & 0x0F) + 1
        cc = event.data1
        val = event.data2

        # Transport Controls
        if cc == 119 and val > 0: # Play
            transport.start()
            event.handled = True
        elif cc == 118 and val > 0: # Stop
            transport.stop()
            event.handled = True
        elif cc == 114 and val > 0: # Record
            transport.record()
            event.handled = True
        elif cc == 116: # Rewind
            transport.rewind(2 if val > 0 else 0)
            event.handled = True
        elif cc == 117: # Forward
            transport.fastForward(2 if val > 0 else 0)
            event.handled = True
        elif cc == 115 and val > 0: # Top / Return to Zero
            transport.setSongPos(0)
            event.handled = True

        # Master Fader
        elif cc == 39:
            mixer.setTrackVolume(0, val / 127.0)
            event.handled = True

        # Multi-Channel Mixer Faders 1..8 (Ch 1..8 CC 7)
        elif cc == 7 and 1 <= ch <= 8:
            mixer.setTrackVolume(ch, val / 127.0)
            event.handled = True

        # Knobs: Pan (CC 10)
        elif cc == 10 and 1 <= ch <= 8:
            pan = (val - 64) / 64.0
            mixer.setTrackPan(ch, pan)
            event.handled = True

