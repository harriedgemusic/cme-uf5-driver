-- Logic Pro / MainStage MIDI Device Script for CME UF5 Master Keyboard
-- Automatically matched when CoreMIDI device Manufacturer="CME", Model="UF5" (VID 0x7104, PID 0x2202) is online.

function controller_midi_in(midiEvent, portName)
	-- Filter out raw 0xF8 Clock or 0xFE Active Sensing if passthrough is enabled
	if midiEvent[0] == 0xF8 or midiEvent[0] == 0xFE then
		return {}
	end
	return nil
end

function controller_info()
	return {
		model = 'UF5',
		manufacturer = 'CME',
		preset_name = 'CME UF5 Logic Pro Control Surface',
		usb_vendor_id = 28932,  -- 0x7104 (CME)
		usb_product_id = 8706,  -- 0x2202 (UF Series)

		items = {
			{name='Pitch Bend', objectType='Wheel', midi={0xE0,MIDI_MSB,MIDI_LSB}},
			{name='Modulation', objectType='Wheel', midi={0xB0,0x01,MIDI_LSB}},
			{name='Channel Pressure', objectType='Wheel', midi={0xD0,MIDI_LSB}},
			{name='Keyboard', label='CME UF5', objectType='Keyboard', midiType='Keyboard', startKey=36, numberKeys=49, midi={0x90,MIDI_Wildcard,MIDI_Wildcard}},

			{name='Sustain Pedal', label='Sustain', objectType='Sustain Pedal', midiType='Momentary', midi={0xB0,0x40,MIDI_LSB}},
			{name='Expression Pedal', label='Expr', objectType='Pedal', midi={0xB0,0x0B,MIDI_LSB}},

			-- Master Volume Fader (Fader 9)
			{name='M.Volume', label='Master', objectType='Volume', midi={0xB0,0x27,MIDI_LSB}},

			-- 8 Channel Faders (Multi-Channel Ch 1..8 CC 7 for Logic Pro Mixer)
			{name='Fader 1', label='F1', objectType='VFader', midi={0xB0,0x07,MIDI_LSB}},
			{name='Fader 2', label='F2', objectType='VFader', midi={0xB1,0x07,MIDI_LSB}},
			{name='Fader 3', label='F3', objectType='VFader', midi={0xB2,0x07,MIDI_LSB}},
			{name='Fader 4', label='F4', objectType='VFader', midi={0xB3,0x07,MIDI_LSB}},
			{name='Fader 5', label='F5', objectType='VFader', midi={0xB4,0x07,MIDI_LSB}},
			{name='Fader 6', label='F6', objectType='VFader', midi={0xB5,0x07,MIDI_LSB}},
			{name='Fader 7', label='F7', objectType='VFader', midi={0xB6,0x07,MIDI_LSB}},
			{name='Fader 8', label='F8', objectType='VFader', midi={0xB7,0x07,MIDI_LSB}},

			-- 8 Rotary Knobs (Bank A: Cutoff, Resonance, Attack, Release, Pan, Reverb, Chorus, Variation/Tempo)
			{name='Knob 1 A', label='Cutoff',    objectType='Knob', midi={0xB0,0x4A,MIDI_LSB}},
			{name='Knob 2 A', label='Resonance', objectType='Knob', midi={0xB0,0x47,MIDI_LSB}},
			{name='Knob 3 A', label='Attack',    objectType='Knob', midi={0xB0,0x49,MIDI_LSB}},
			{name='Knob 4 A', label='Release',   objectType='Knob', midi={0xB0,0x48,MIDI_LSB}},
			{name='Knob 5 A', label='Pan',       objectType='Knob', midi={0xB0,0x0A,MIDI_LSB}},
			{name='Knob 6 A', label='Reverb',    objectType='Knob', midi={0xB0,0x5B,MIDI_LSB}},
			{name='Knob 7 A', label='Chorus',    objectType='Knob', midi={0xB0,0x5D,MIDI_LSB}},
			{name='Knob 8 A', label='Variation', objectType='Knob', midi={0xB0,0x5E,MIDI_LSB}},

			-- 6 Transport Buttons (Translated by CMEUFMIDIDriver to CC 114..119 on Channel 1)
			{name='Top',     label='⏮ Top',  objectType='Button', midiType='Single', midi={0xB0,0x73,MIDI_LSB}},
			{name='Rewind',  label='◀◀ Rew', objectType='Button', midiType='Single', midi={0xB0,0x74,MIDI_LSB}},
			{name='Forward', label='▶▶ Fwd', objectType='Button', midiType='Single', midi={0xB0,0x75,MIDI_LSB}},
			{name='Record',  label='● Rec',  objectType='Button', midiType='Single', midi={0xB0,0x72,MIDI_LSB}},
			{name='Stop',    label='■ Stop', objectType='Button', midiType='Single', midi={0xB0,0x76,MIDI_LSB}},
			{name='Play',    label='▶ Play', objectType='Button', midiType='Single', midi={0xB0,0x77,MIDI_LSB}},
		}
	}
end
