import SwiftUI
import AppKit
import CoreMIDI
import IOKit
import IOKit.usb

private let kCMEDriverPrefsID = "com.cme-pro.midi.driver.uf" as CFString
private let kCMEConfigNotifyName = "com.cme-pro.midi.driver.uf.configChanged" as CFString
private let kCMEVendorID: Int = 0x7104
private let kCMEProductID: Int = 0x2202

struct MIDILogEntry: Identifiable {
    let id = UUID()
    let timestamp: String
    let type: String
    let channel: String
    let detail: String
    let hex: String
}

final class CMEUF5ViewModel: ObservableObject {
    // Hardware & System Status
    @Published var usbConnected: Bool = false
    @Published var usbLocationText: String = "Не подключено"
    @Published var midiOnline: Bool = false
    @Published var driverInstalled: Bool = false
    @Published var logicSurfaceInstalled: Bool = false

    // Live Hardware Surface State
    @Published var ledText: String = "UF5 READY"
    @Published var masterFader: Double = 100
    @Published var faders: [Double] = Array(repeating: 100, count: 8)
    @Published var knobs: [Double] = [64, 64, 64, 64, 64, 40, 30, 64]
    @Published var pitchBend: Double = 0       // -8192 .. +8191
    @Published var modWheel: Double = 0        // 0 .. 127
    @Published var aftertouch: Double = 0      // 0 .. 127
    @Published var sustainPressed: Bool = false
    @Published var expressionPedal: Double = 127
    @Published var activeTransport: Int = 0    // 0=none, 0x72..0x77
    @Published var activeNotes: [Int: Int] = [:] // note -> velocity
    @Published var lastInputVelocity: Int = 0

    // Driver Preferences (persisted in com.cme-pro.midi.driver.uf)
    @Published var velocityCurve: Int = 0 { didSet { savePref("VelocityCurve", velocityCurve) } }
    @Published var fixedVelocity: Int = 100 { didSet { savePref("FixedVelocity", fixedVelocity) } }
    @Published var octaveShift: Int = 0 { didSet { savePref("OctaveShift", octaveShift) } }
    @Published var transposeSemitones: Int = 0 { didSet { savePref("TransposeSemitones", transposeSemitones) } }
    @Published var channelOverride: Int = 0 { didSet { savePref("ChannelOverride", channelOverride) } }
    @Published var faderMode: Int = 0 { didSet { savePref("FaderMode", faderMode) } }
    @Published var transportMode: Int = 0 { didSet { savePref("TransportMode", transportMode) } }
    @Published var pitchBendDeadzone: Int = 0 { didSet { savePref("PitchBendDeadzone", pitchBendDeadzone) } }
    @Published var filterActiveSensing: Bool = true { didSet { saveBoolPref("FilterActiveSensing", filterActiveSensing) } }
    @Published var filterClock: Bool = true { didSet { saveBoolPref("FilterClock", filterClock) } }
    @Published var filterAftertouch: Bool = false { didSet { saveBoolPref("FilterAftertouch", filterAftertouch) } }

    // MIDI Monitor
    @Published var logEntries: [MIDILogEntry] = []
    @Published var monitorPaused: Bool = false
    @Published var statusBanner: String = ""

    private var isLoadingPrefs = false
    private var midiClient: MIDIClientRef = 0
    private var inPort: MIDIPortRef = 0
    private var outPort: MIDIPortRef = 0
    private var cmeDest: MIDIEndpointRef = 0
    private var pollTimer: Timer?
    private let dateFormatter: DateFormatter = {
        let df = DateFormatter()
        df.dateFormat = "HH:mm:ss.SSS"
        return df
    }()

    init() {
        loadPreferences()
        setupCoreMIDI()
        refreshSystemStatus()
        pollTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            self?.refreshSystemStatus()
        }
    }

    deinit {
        pollTimer?.invalidate()
        if inPort != 0 { MIDIPortDispose(inPort) }
        if outPort != 0 { MIDIPortDispose(outPort) }
        if midiClient != 0 { MIDIClientDispose(midiClient) }
    }

    // MARK: - Preferences

    func loadPreferences() {
        isLoadingPrefs = true
        CFPreferencesAppSynchronize(kCMEDriverPrefsID)
        velocityCurve = readIntPref("VelocityCurve", defaultVal: 0)
        fixedVelocity = readIntPref("FixedVelocity", defaultVal: 100)
        octaveShift = readIntPref("OctaveShift", defaultVal: 0)
        transposeSemitones = readIntPref("TransposeSemitones", defaultVal: 0)
        channelOverride = readIntPref("ChannelOverride", defaultVal: 0)
        faderMode = readIntPref("FaderMode", defaultVal: 0)
        transportMode = readIntPref("TransportMode", defaultVal: 0)
        pitchBendDeadzone = readIntPref("PitchBendDeadzone", defaultVal: 0)
        filterActiveSensing = readBoolPref("FilterActiveSensing", defaultVal: true)
        filterClock = readBoolPref("FilterClock", defaultVal: true)
        filterAftertouch = readBoolPref("FilterAftertouch", defaultVal: false)
        isLoadingPrefs = false
    }

    func resetDefaults() {
        velocityCurve = 0
        fixedVelocity = 100
        octaveShift = 0
        transposeSemitones = 0
        channelOverride = 0
        faderMode = 0
        transportMode = 0
        pitchBendDeadzone = 0
        filterActiveSensing = true
        filterClock = true
        filterAftertouch = false
        showBanner("Настройки сброшены к значениям по умолчанию")
    }

    private func readIntPref(_ key: String, defaultVal: Int) -> Int {
        var exists: DarwinBoolean = false
        let val = CFPreferencesGetAppIntegerValue(key as CFString, kCMEDriverPrefsID, &exists)
        return exists.boolValue ? Int(val) : defaultVal
    }

    private func readBoolPref(_ key: String, defaultVal: Bool) -> Bool {
        var exists: DarwinBoolean = false
        let val = CFPreferencesGetAppBooleanValue(key as CFString, kCMEDriverPrefsID, &exists)
        return exists.boolValue ? val : defaultVal
    }

    private func savePref(_ key: String, _ value: Int) {
        guard !isLoadingPrefs else { return }
        let num = NSNumber(value: value)
        CFPreferencesSetAppValue(key as CFString, num, kCMEDriverPrefsID)
        CFPreferencesAppSynchronize(kCMEDriverPrefsID)
        notifyDriverConfigChanged()
    }

    private func saveBoolPref(_ key: String, _ value: Bool) {
        guard !isLoadingPrefs else { return }
        let cfBool: CFBoolean = value ? kCFBooleanTrue : kCFBooleanFalse
        CFPreferencesSetAppValue(key as CFString, cfBool, kCMEDriverPrefsID)
        CFPreferencesAppSynchronize(kCMEDriverPrefsID)
        notifyDriverConfigChanged()
    }

    private func notifyDriverConfigChanged() {
        let center = CFNotificationCenterGetDarwinNotifyCenter()
        CFNotificationCenterPostNotification(center, CFNotificationName(kCMEConfigNotifyName), nil, nil, true)
    }

    func mappedVelocity(for input: Int) -> Int {
        if input <= 0 { return 0 }
        let norm = Double(min(max(input, 1), 127)) / 127.0
        let out: Double
        switch velocityCurve {
        case 1: out = pow(norm, 0.55)
        case 2: out = pow(norm, 1.75)
        case 3: out = norm * norm * (3.0 - 2.0 * norm)
        case 4: return min(max(fixedVelocity, 1), 127)
        default: out = norm
        }
        return min(max(Int(round(out * 127.0)), 1), 127)
    }

    // MARK: - System & USB Status

    func refreshSystemStatus() {
        let fm = FileManager.default
        let home = fm.homeDirectoryForCurrentUser.path
        let userPlugin = "\(home)/Library/Audio/MIDI Drivers/CMEUFMIDIDriver.plugin"
        let sysPlugin = "/Library/Audio/MIDI Drivers/CMEUFMIDIDriver.plugin"
        driverInstalled = fm.fileExists(atPath: userPlugin) || fm.fileExists(atPath: sysPlugin)

        let logicScript = "\(home)/Music/Audio Music Apps/MIDI Device Scripts/CME/UF5.device/config.lua"
        let sysLogicScript = "/Library/Audio/MIDI Device Scripts/CME/UF5.device/config.lua"
        logicSurfaceInstalled = fm.fileExists(atPath: logicScript) || fm.fileExists(atPath: sysLogicScript)

        // Check IOKit USB
        var foundUSB = false
        var locStr = "Не подключено"
        if let matchDict = IOServiceMatching("IOUSBHostDevice") as NSMutableDictionary? {
            matchDict[kUSBVendorID] = NSNumber(value: kCMEVendorID)
            matchDict[kUSBProductID] = NSNumber(value: kCMEProductID)
            var iter: io_iterator_t = 0
            if IOServiceGetMatchingServices(kIOMainPortDefault, matchDict, &iter) == KERN_SUCCESS {
                var dev = IOIteratorNext(iter)
                while dev != 0 {
                    foundUSB = true
                    var addr = 0
                    if let addrProp = IORegistryEntryCreateCFProperty(dev, "USB Address" as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue() as? NSNumber {
                        addr = addrProp.intValue
                    }
                    locStr = "USB адрес \(addr) (0x7104:0x2202)"
                    IOObjectRelease(dev)
                    dev = IOIteratorNext(iter)
                }
                IOObjectRelease(iter)
            }
        }
        usbConnected = foundUSB
        usbLocationText = locStr

        // Check CoreMIDI device & endpoints
        var foundMIDI = false
        let devCount = MIDIGetNumberOfDevices()
        for i in 0..<devCount {
            let dev = MIDIGetDevice(i)
            var nameRef: Unmanaged<CFString>?
            MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &nameRef)
            let name = (nameRef?.takeRetainedValue() as String?) ?? ""
            if name.contains("CME") {
                var offline: Int32 = 0
                MIDIObjectGetIntegerProperty(dev, kMIDIPropertyOffline, &offline)
                if offline == 0 {
                    foundMIDI = true
                }
            }
        }
        midiOnline = foundMIDI
        reconnectMIDISources()
    }

    // MARK: - CoreMIDI Setup & I/O

    private func setupCoreMIDI() {
        let notifyProc: MIDINotifyProc = { _, refCon in
            guard let refCon = refCon else { return }
            let vm = Unmanaged<CMEUF5ViewModel>.fromOpaque(refCon).takeUnretainedValue()
            DispatchQueue.main.async {
                vm.refreshSystemStatus()
            }
        }

        let selfPtr = Unmanaged.passUnretained(self).toOpaque()
        MIDIClientCreate("CMEUF5ControlCenter" as CFString, notifyProc, selfPtr, &midiClient)

        let readProc: MIDIReadProc = { pktListPtr, readProcRefCon, _ in
            guard let readProcRefCon = readProcRefCon else { return }
            let vm = Unmanaged<CMEUF5ViewModel>.fromOpaque(readProcRefCon).takeUnretainedValue()
            let pktList = pktListPtr.pointee
            var packet = pktList.packet
            for _ in 0..<pktList.numPackets {
                let length = Int(packet.length)
                if length > 0 {
                    var bytes = [UInt8](repeating: 0, count: length)
                    withUnsafeBytes(of: &packet.data) { rawBuf in
                        for idx in 0..<min(length, rawBuf.count) {
                            bytes[idx] = rawBuf[idx]
                        }
                    }
                    DispatchQueue.main.async {
                        vm.handleIncomingMIDI(bytes)
                    }
                }
                packet = MIDIPacketNext(&packet).pointee
            }
        }

        MIDIInputPortCreate(midiClient, "Input" as CFString, readProc, selfPtr, &inPort)
        MIDIOutputPortCreate(midiClient, "Output" as CFString, &outPort)
        reconnectMIDISources()
    }

    func reconnectMIDISources() {
        guard inPort != 0 else { return }
        let srcCount = MIDIGetNumberOfSources()
        for i in 0..<srcCount {
            let src = MIDIGetSource(i)
            MIDIPortConnectSource(inPort, src, nil)
        }

        cmeDest = 0
        let dstCount = MIDIGetNumberOfDestinations()
        for i in 0..<dstCount {
            let dst = MIDIGetDestination(i)
            var nameRef: Unmanaged<CFString>?
            MIDIObjectGetStringProperty(dst, kMIDIPropertyDisplayName, &nameRef)
            if nameRef == nil {
                MIDIObjectGetStringProperty(dst, kMIDIPropertyName, &nameRef)
            }
            let name = (nameRef?.takeRetainedValue() as String?) ?? ""
            if name.contains("CME") || name.contains("UF5") || cmeDest == 0 {
                cmeDest = dst
            }
        }
    }

    private func handleIncomingMIDI(_ bytes: [UInt8]) {
        guard let status = bytes.first else { return }
        let hexStr = bytes.map { String(format: "%02X", $0) }.joined(separator: " ")

        var msgType = "MIDI"
        var chStr = "-"
        var detail = ""

        if status >= 0x80 && status <= 0xEF {
            let high = status & 0xF0
            let ch = Int(status & 0x0F) + 1
            chStr = "\(ch)"
            let d1 = bytes.count > 1 ? Int(bytes[1]) : 0
            let d2 = bytes.count > 2 ? Int(bytes[2]) : 0

            switch high {
            case 0x80:
                msgType = "Note Off"
                let nName = Self.noteName(d1)
                detail = "\(nName) (#\(d1)) vel=\(d2)"
                activeNotes.removeValue(forKey: d1)
                ledText = "NOTE \(nName)"
            case 0x90:
                let nName = Self.noteName(d1)
                if d2 == 0 {
                    msgType = "Note Off"
                    detail = "\(nName) (#\(d1)) vel=0"
                    activeNotes.removeValue(forKey: d1)
                } else {
                    msgType = "Note On"
                    detail = "\(nName) (#\(d1)) vel=\(d2)"
                    activeNotes[d1] = d2
                    lastInputVelocity = d2
                    ledText = "\(nName) V\(d2)"
                }
            case 0xA0:
                msgType = "Poly Touch"
                detail = "\(Self.noteName(d1)) val=\(d2)"
                aftertouch = Double(d2)
            case 0xB0:
                msgType = "CC"
                let ccTitle = Self.ccName(d1)
                detail = "CC \(d1) (\(ccTitle)) = \(d2)"
                ledText = "CC\(d1): \(d2)"
                updateSurfaceForCC(channel: ch, cc: d1, value: d2)
            case 0xC0:
                msgType = "Program"
                detail = "Program #\(d1)"
                ledText = "PGM \(d1)"
            case 0xD0:
                msgType = "Aftertouch"
                detail = "Pressure = \(d1)"
                aftertouch = Double(d1)
                ledText = "AT \(d1)"
            case 0xE0:
                msgType = "Pitch Bend"
                let bend = ((d2 << 7) | d1) - 8192
                detail = "Bend = \(bend)"
                pitchBend = Double(bend)
                ledText = "PB \(bend)"
            default:
                break
            }
        } else {
            switch status {
            case 0xF0:
                msgType = "SysEx"
                detail = "\(bytes.count) байт"
                ledText = "SYSEX"
            case 0xF2:
                msgType = "Song Pos"
                let pos = bytes.count >= 3 ? ((Int(bytes[2]) << 7) | Int(bytes[1])) : 0
                detail = "SPP = \(pos)"
            case 0xF8:
                msgType = "Clock"
                detail = "0xF8"
            case 0xFA:
                msgType = "Start"
                detail = "0xFA"
                activeTransport = 0x77
                ledText = "PLAY"
            case 0xFB:
                msgType = "Continue"
                detail = "0xFB"
                activeTransport = 0x77
                ledText = "CONT"
            case 0xFC:
                msgType = "Stop"
                detail = "0xFC"
                activeTransport = 0x76
                ledText = "STOP"
            case 0xFE:
                msgType = "Active Sense"
                detail = "0xFE"
            default:
                detail = hexStr
            }
        }

        if !monitorPaused {
            let entry = MIDILogEntry(
                timestamp: dateFormatter.string(from: Date()),
                type: msgType,
                channel: chStr,
                detail: detail,
                hex: hexStr
            )
            logEntries.insert(entry, at: 0)
            if logEntries.count > 250 {
                logEntries.removeLast(logEntries.count - 250)
            }
        }
    }

    private func updateSurfaceForCC(channel: Int, cc: Int, value: Int) {
        let val = Double(value)
        switch cc {
        case 0x01:
            modWheel = val
        case 0x07:
            if (1...8).contains(channel) {
                faders[channel - 1] = val
            }
        case 0x14...0x1B:
            faders[cc - 0x14] = val
        case 0x27:
            masterFader = val
        case 0x40:
            sustainPressed = (value >= 64)
        case 0x0B:
            expressionPedal = val
        case 0x4A: knobs[0] = val
        case 0x47: knobs[1] = val
        case 0x49: knobs[2] = val
        case 0x48: knobs[3] = val
        case 0x0A: knobs[4] = val
        case 0x5B: knobs[5] = val
        case 0x5D: knobs[6] = val
        case 0x5E: knobs[7] = val
        case 0x72...0x77:
            activeTransport = cc
        default:
            break
        }
    }

    // MARK: - Outgoing MIDI & Actions

    func sendMIDI(_ bytes: [UInt8]) {
        guard outPort != 0, !bytes.isEmpty else { return }
        var packetList = MIDIPacketList()
        let pkt = MIDIPacketListInit(&packetList)
        _ = MIDIPacketListAdd(&packetList, MemoryLayout<MIDIPacketList>.size, pkt, mach_absolute_time(), bytes.count, bytes)

        if cmeDest != 0 {
            MIDISend(outPort, cmeDest, &packetList)
        }
        // Also log outgoing
        handleIncomingMIDI(bytes)
    }

    func triggerTransport(cc: Int, name: String) {
        activeTransport = cc
        ledText = name
        sendMIDI([0xB0, UInt8(cc & 0x7F), 0x7F])
    }

    func sendPanic() {
        for ch: UInt8 in 0..<16 {
            sendMIDI([0xB0 | ch, 123, 0]) // All Notes Off
            sendMIDI([0xB0 | ch, 120, 0]) // All Sound Off
            sendMIDI([0xB0 | ch, 121, 0]) // Reset All Controllers
        }
        activeNotes.removeAll()
        ledText = "PANIC OK"
        showBanner("Отправлен сигнал All Notes Off / Reset Controllers на все 16 каналов")
    }

    func sendGMReset() {
        sendMIDI([0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7])
        ledText = "GM ON"
        showBanner("Отправлено сообщение GM System On (F0 7E 7F 09 01 F7)")
    }

    func restartMIDIServer() {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/killall")
        task.arguments = ["MIDIServer"]
        try? task.run()
        task.waitUntilExit()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) {
            self.refreshSystemStatus()
            self.showBanner("Драйвер CoreMIDI (MIDIServer) успешно перезапущен")
        }
    }

    func installLogicProSurface() {
        let fm = FileManager.default
        let home = fm.homeDirectoryForCurrentUser.path
        let targets = [
            "\(home)/Music/Audio Music Apps/MIDI Device Scripts/CME/UF5.device",
            "\(home)/Music/Audio Music Apps/MIDI Device Scripts/CME/CME UF5.device",
            "\(home)/Music/Audio Music Apps/MIDI Device Scripts/Central Music Co/CME UF MIDI Device.device"
        ]
        for dir in targets {
            try? fm.createDirectory(atPath: dir, withIntermediateDirectories: true)
            let luaPath = "\(dir)/config.lua"
            try? Self.logicProLuaScript.write(toFile: luaPath, atomically: true, encoding: .utf8)
        }
        refreshSystemStatus()
        showBanner("Профиль Control Surface для Logic Pro успешно установлен")
    }

    func openLogicPro() {
        let urls = [
            URL(fileURLWithPath: "/Applications/Logic Pro Creator Studio.app"),
            URL(fileURLWithPath: "/Applications/Logic Pro.app"),
            URL(fileURLWithPath: "/Applications/Logic Pro X.app")
        ]
        for url in urls where FileManager.default.fileExists(atPath: url.path) {
            NSWorkspace.shared.open(url)
            return
        }
        showBanner("Приложение Logic Pro не найдено в /Applications")
    }

    func openAudioMIDISetup() {
        let url = URL(fileURLWithPath: "/System/Applications/Utilities/Audio MIDI Setup.app")
        NSWorkspace.shared.open(url)
    }

    private func showBanner(_ message: String) {
        statusBanner = message
        DispatchQueue.main.asyncAfter(deadline: .now() + 4.0) { [weak self] in
            if self?.statusBanner == message {
                self?.statusBanner = ""
            }
        }
    }

    // MARK: - Helpers

    static func noteName(_ note: Int) -> String {
        let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        let oct = (note / 12) - 1
        return "\(names[max(0, note) % 12])\(oct)"
    }

    static func ccName(_ cc: Int) -> String {
        switch cc {
        case 1: return "Modulation"
        case 7: return "Channel Volume"
        case 10: return "Pan (Knob 5)"
        case 11: return "Expression"
        case 20...27: return "Fader \(cc - 19) (SC)"
        case 39: return "Master Volume"
        case 64: return "Sustain Pedal"
        case 71: return "Resonance (Knob 2)"
        case 72: return "Release (Knob 4)"
        case 73: return "Attack (Knob 3)"
        case 74: return "Cutoff (Knob 1)"
        case 91: return "Reverb (Knob 6)"
        case 93: return "Chorus (Knob 7)"
        case 94: return "Variation (Knob 8)"
        case 114: return "Transport Record"
        case 115: return "Transport Top"
        case 116: return "Transport Rewind"
        case 117: return "Transport Forward"
        case 118: return "Transport Stop"
        case 119: return "Transport Play"
        default: return "Control"
        }
    }

    static let logicProLuaScript = """
    -- Logic Pro / MainStage MIDI Device Script for CME UF5 Master Keyboard
    -- Automatically matched when CoreMIDI device Manufacturer="CME", Model="UF5" (VID 0x7104, PID 0x2202) is online.

    function controller_midi_in(midiEvent, portName)
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
            usb_vendor_id = 28932,
            usb_product_id = 8706,

            items = {
                {name='Pitch Bend', objectType='Wheel', midi={0xE0,MIDI_MSB,MIDI_LSB}},
                {name='Modulation', objectType='Wheel', midi={0xB0,0x01,MIDI_LSB}},
                {name='Channel Pressure', objectType='Wheel', midi={0xD0,MIDI_LSB}},
                {name='Keyboard', label='CME UF5', objectType='Keyboard', midiType='Keyboard', startKey=36, numberKeys=49, midi={0x90,MIDI_Wildcard,MIDI_Wildcard}},

                {name='Sustain Pedal', label='Sustain', objectType='Sustain Pedal', midiType='Momentary', midi={0xB0,0x40,MIDI_LSB}},
                {name='Expression Pedal', label='Expr', objectType='Pedal', midi={0xB0,0x0B,MIDI_LSB}},

                {name='M.Volume', label='Master', objectType='Volume', midi={0xB0,0x27,MIDI_LSB}},

                {name='Fader 1', label='F1', objectType='VFader', midi={0xB0,0x07,MIDI_LSB}},
                {name='Fader 2', label='F2', objectType='VFader', midi={0xB1,0x07,MIDI_LSB}},
                {name='Fader 3', label='F3', objectType='VFader', midi={0xB2,0x07,MIDI_LSB}},
                {name='Fader 4', label='F4', objectType='VFader', midi={0xB3,0x07,MIDI_LSB}},
                {name='Fader 5', label='F5', objectType='VFader', midi={0xB4,0x07,MIDI_LSB}},
                {name='Fader 6', label='F6', objectType='VFader', midi={0xB5,0x07,MIDI_LSB}},
                {name='Fader 7', label='F7', objectType='VFader', midi={0xB6,0x07,MIDI_LSB}},
                {name='Fader 8', label='F8', objectType='VFader', midi={0xB7,0x07,MIDI_LSB}},

                {name='Knob 1 A', label='Cutoff',    objectType='Knob', midi={0xB0,0x4A,MIDI_LSB}},
                {name='Knob 2 A', label='Resonance', objectType='Knob', midi={0xB0,0x47,MIDI_LSB}},
                {name='Knob 3 A', label='Attack',    objectType='Knob', midi={0xB0,0x49,MIDI_LSB}},
                {name='Knob 4 A', label='Release',   objectType='Knob', midi={0xB0,0x48,MIDI_LSB}},
                {name='Knob 5 A', label='Pan',       objectType='Knob', midi={0xB0,0x0A,MIDI_LSB}},
                {name='Knob 6 A', label='Reverb',    objectType='Knob', midi={0xB0,0x5B,MIDI_LSB}},
                {name='Knob 7 A', label='Chorus',    objectType='Knob', midi={0xB0,0x5D,MIDI_LSB}},
                {name='Knob 8 A', label='Variation', objectType='Knob', midi={0xB0,0x5E,MIDI_LSB}},

                {name='Top',     label='⏮ Top',  objectType='Button', midiType='Single', midi={0xB0,0x73,MIDI_LSB}},
                {name='Rewind',  label='◀◀ Rew', objectType='Button', midiType='Single', midi={0xB0,0x74,MIDI_LSB}},
                {name='Forward', label='▶▶ Fwd', objectType='Button', midiType='Single', midi={0xB0,0x75,MIDI_LSB}},
                {name='Record',  label='● Rec',  objectType='Button', midiType='Single', midi={0xB0,0x72,MIDI_LSB}},
                {name='Stop',    label='■ Stop', objectType='Button', midiType='Single', midi={0xB0,0x76,MIDI_LSB}},
                {name='Play',    label='▶ Play', objectType='Button', midiType='Single', midi={0xB0,0x77,MIDI_LSB}},
            }
        }
    end
    """
}

// MARK: - Main Content View

struct ContentView: View {
    @StateObject private var vm = CMEUF5ViewModel()
    @State private var selectedTab: Int = 0

    var body: some View {
        VStack(spacing: 0) {
            HeaderStatusView(vm: vm)
            Divider()

            TabView(selection: $selectedTab) {
                SurfaceVisualizerView(vm: vm)
                    .tabItem { Label("Панель контроллера", systemImage: "pianokeys.inverse") }
                    .tag(0)

                DriverSettingsView(vm: vm)
                    .tabItem { Label("Настройки контроллера", systemImage: "slider.horizontal.3") }
                    .tag(1)

                LogicProSurfaceView(vm: vm)
                    .tabItem { Label("Logic Pro Surface", systemImage: "dial.medium.fill") }
                    .tag(2)

                MIDIMonitorView(vm: vm)
                    .tabItem { Label("MIDI Монитор", systemImage: "waveform.path.ecg") }
                    .tag(3)
            }
            .padding(14)

            if !vm.statusBanner.isEmpty {
                HStack {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundColor(.green)
                    Text(vm.statusBanner)
                        .font(.callout)
                    Spacer()
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 8)
                .background(Color(NSColor.controlBackgroundColor))
            }
        }
        .frame(minWidth: 980, minHeight: 680)
    }
}

struct HeaderStatusView: View {
    @ObservedObject var vm: CMEUF5ViewModel

    var body: some View {
        HStack(spacing: 16) {
            HStack(spacing: 10) {
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color(red: 0.78, green: 0.12, blue: 0.16))
                    .frame(width: 8, height: 34)
                VStack(alignment: .leading, spacing: 2) {
                    Text("CME UF5 Control Center")
                        .font(.system(size: 17, weight: .bold))
                    Text(vm.usbLocationText)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Spacer()

            StatusBadge(title: "USB UF5", active: vm.usbConnected)
            StatusBadge(title: "CoreMIDI", active: vm.midiOnline)
            StatusBadge(title: "Logic Pro Surface", active: vm.logicSurfaceInstalled)

            Button(action: { vm.sendPanic() }) {
                Label("MIDI Panic", systemImage: "exclamationmark.triangle.fill")
            }
            .help("Сбросить все зависшие ноты (All Notes Off)")

            Button(action: { vm.restartMIDIServer() }) {
                Label("Перезапуск драйвера", systemImage: "arrow.clockwise")
            }
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 10)
        .background(Color(NSColor.windowBackgroundColor))
    }
}

struct StatusBadge: View {
    let title: String
    let active: Bool

    var body: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(active ? Color.green : Color.red)
                .frame(width: 8, height: 8)
            Text(title)
                .font(.system(size: 12, weight: .medium))
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .fill(active ? Color.green.opacity(0.12) : Color.red.opacity(0.12))
        )
    }
}

// MARK: - Tab 1: Interactive Hardware Surface Visualizer

struct SurfaceVisualizerView: View {
    @ObservedObject var vm: CMEUF5ViewModel
    private let knobLabels = ["Cutoff", "Resonance", "Attack", "Release", "Pan", "Reverb", "Chorus", "Variation"]
    private let knobCCs: [UInt8] = [0x4A, 0x47, 0x49, 0x48, 0x0A, 0x5B, 0x5D, 0x5E]

    var body: some View {
        VStack(spacing: 14) {
            // Top Hardware Chassis Panel
            HStack(spacing: 14) {
                // Red Left Cheek
                RoundedRectangle(cornerRadius: 8)
                    .fill(LinearGradient(
                        colors: [Color(red: 0.82, green: 0.14, blue: 0.18), Color(red: 0.55, green: 0.06, blue: 0.09)],
                        startPoint: .top, endPoint: .bottom
                    ))
                    .frame(width: 16)

                // Faders Section (Master + 1..8)
                VStack(alignment: .leading, spacing: 8) {
                    Text("9 ФЕЙДЕРОВ (MASTER + КАНАЛЫ 1–8)")
                        .font(.system(size: 11, weight: .bold))
                        .foregroundColor(.secondary)

                    HStack(spacing: 10) {
                        VerticalFaderView(
                            label: "M.VOL",
                            value: $vm.masterFader,
                            accent: .red
                        ) { newVal in
                            vm.sendMIDI([0xB0, 0x27, UInt8(newVal)])
                        }

                        Divider().frame(height: 150)

                        ForEach(0..<8, id: \.self) { idx in
                            VerticalFaderView(
                                label: "CH \(idx + 1)",
                                value: $vm.faders[idx],
                                accent: .cyan
                            ) { newVal in
                                if vm.faderMode == 1 {
                                    vm.sendMIDI([0xB0, UInt8(20 + idx), UInt8(newVal)])
                                } else {
                                    vm.sendMIDI([0xB0 | UInt8(idx), 0x07, UInt8(newVal)])
                                }
                            }
                        }
                    }
                }
                .padding(12)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.black.opacity(0.28)))

                // Center LED Display + Transport Section
                VStack(spacing: 12) {
                    // Classic CME Blue LED Display
                    VStack(spacing: 4) {
                        Text("CME UF5 DIGITAL DISPLAY")
                            .font(.system(size: 9, weight: .bold))
                            .foregroundColor(.cyan.opacity(0.8))
                        Text(vm.ledText)
                            .font(.system(size: 22, weight: .heavy, design: .monospaced))
                            .foregroundColor(.white)
                            .frame(width: 175, height: 44)
                            .background(
                                RoundedRectangle(cornerRadius: 6)
                                    .fill(LinearGradient(
                                        colors: [Color(red: 0.02, green: 0.36, blue: 0.85), Color(red: 0.01, green: 0.20, blue: 0.52)],
                                        startPoint: .top, endPoint: .bottom
                                    ))
                            )
                            .overlay(
                                RoundedRectangle(cornerRadius: 6)
                                    .stroke(Color.cyan.opacity(0.6), lineWidth: 1.5)
                            )
                    }

                    // Pedals & Aftertouch status
                    HStack(spacing: 10) {
                        HStack(spacing: 5) {
                            Circle()
                                .fill(vm.sustainPressed ? Color.orange : Color.gray.opacity(0.4))
                                .frame(width: 10, height: 10)
                            Text("Sustain")
                                .font(.caption2)
                        }
                        Text("AT: \(Int(vm.aftertouch))")
                            .font(.caption2.monospacedDigit())
                            .foregroundColor(.secondary)
                    }

                    // 6 Transport Buttons
                    VStack(spacing: 6) {
                        Text("ТРАНСПОРТ (LOGIC PRO SURFACE)")
                            .font(.system(size: 10, weight: .bold))
                            .foregroundColor(.secondary)
                        HStack(spacing: 6) {
                            TransportButton(symbol: "backward.end.fill", label: "Top", active: vm.activeTransport == 0x73) {
                                vm.triggerTransport(cc: 0x73, name: "TOP")
                            }
                            TransportButton(symbol: "backward.fill", label: "Rew", active: vm.activeTransport == 0x74) {
                                vm.triggerTransport(cc: 0x74, name: "REW")
                            }
                            TransportButton(symbol: "forward.fill", label: "Fwd", active: vm.activeTransport == 0x75) {
                                vm.triggerTransport(cc: 0x75, name: "FWD")
                            }
                        }
                        HStack(spacing: 6) {
                            TransportButton(symbol: "circle.fill", label: "Rec", isRecord: true, active: vm.activeTransport == 0x72) {
                                vm.triggerTransport(cc: 0x72, name: "REC")
                            }
                            TransportButton(symbol: "stop.fill", label: "Stop", active: vm.activeTransport == 0x76) {
                                vm.triggerTransport(cc: 0x76, name: "STOP")
                            }
                            TransportButton(symbol: "play.fill", label: "Play", active: vm.activeTransport == 0x77) {
                                vm.triggerTransport(cc: 0x77, name: "PLAY")
                            }
                        }
                    }
                }
                .padding(12)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.black.opacity(0.28)))

                // 8 Rotary Knobs Section
                VStack(alignment: .leading, spacing: 8) {
                    Text("8 РЕГУЛЯТОРОВ (KNOBS 1–8)")
                        .font(.system(size: 11, weight: .bold))
                        .foregroundColor(.secondary)

                    LazyVGrid(columns: Array(repeating: GridItem(.fixed(68), spacing: 10), count: 4), spacing: 12) {
                        ForEach(0..<8, id: \.self) { idx in
                            RotaryKnobView(
                                title: knobLabels[idx],
                                value: $vm.knobs[idx]
                            ) { newVal in
                                vm.sendMIDI([0xB0, knobCCs[idx], UInt8(newVal)])
                            }
                        }
                    }
                }
                .padding(12)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.black.opacity(0.28)))

                // Red Right Cheek
                RoundedRectangle(cornerRadius: 8)
                    .fill(LinearGradient(
                        colors: [Color(red: 0.82, green: 0.14, blue: 0.18), Color(red: 0.55, green: 0.06, blue: 0.09)],
                        startPoint: .top, endPoint: .bottom
                    ))
                    .frame(width: 16)
            }
            .frame(height: 235)

            // Bottom Section: Pitch/Mod Wheels + 49-Key Keyboard (C2..C6)
            HStack(spacing: 14) {
                // Pitch & Modulation Wheels
                HStack(spacing: 12) {
                    WheelColumnView(
                        title: "PITCH",
                        normalizedValue: (vm.pitchBend + 8192.0) / 16383.0,
                        displayValue: "\(Int(vm.pitchBend))"
                    )
                    WheelColumnView(
                        title: "MOD",
                        normalizedValue: vm.modWheel / 127.0,
                        displayValue: "\(Int(vm.modWheel))"
                    )
                }
                .padding(12)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.black.opacity(0.28)))

                // 49-Key Interactive Keyboard
                Keyboard49View(activeNotes: vm.activeNotes) { note, pressed in
                    let shifted = min(max(note + vm.octaveShift * 12 + vm.transposeSemitones, 0), 127)
                    if pressed {
                        let vel = UInt8(vm.mappedVelocity(for: 100))
                        vm.sendMIDI([0x90, UInt8(shifted), vel])
                    } else {
                        vm.sendMIDI([0x80, UInt8(shifted), 0])
                    }
                }
            }
            .frame(height: 210)
        }
    }
}

struct VerticalFaderView: View {
    let label: String
    @Binding var value: Double
    let accent: Color
    var onChanged: (Double) -> Void

    var body: some View {
        VStack(spacing: 6) {
            Text("\(Int(value))")
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(.secondary)

            GeometryReader { geo in
                let h = geo.size.height
                let norm = min(max(value / 127.0, 0), 1)
                ZStack(alignment: .bottom) {
                    Capsule()
                        .fill(Color.gray.opacity(0.25))
                        .frame(width: 6)
                    Capsule()
                        .fill(accent)
                        .frame(width: 6, height: max(4, h * norm))
                    RoundedRectangle(cornerRadius: 3)
                        .fill(Color.white)
                        .frame(width: 22, height: 11)
                        .shadow(radius: 1)
                        .offset(y: -((h - 11) * norm))
                }
                .frame(maxWidth: .infinity)
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { drag in
                            let ratio = 1.0 - min(max(drag.location.y / h, 0), 1)
                            let newVal = round(ratio * 127.0)
                            if newVal != value {
                                value = newVal
                                onChanged(newVal)
                            }
                        }
                )
            }
            .frame(width: 26, height: 125)

            Text(label)
                .font(.system(size: 10, weight: .semibold))
        }
    }
}

struct RotaryKnobView: View {
    let title: String
    @Binding var value: Double
    var onChanged: (Double) -> Void

    var body: some View {
        VStack(spacing: 4) {
            ZStack {
                Circle()
                    .stroke(Color.gray.opacity(0.25), lineWidth: 5)
                    .frame(width: 44, height: 44)
                Circle()
                    .trim(from: 0.0, to: CGFloat(min(max(value / 127.0, 0), 1) * 0.75))
                    .stroke(Color.cyan, style: StrokeStyle(lineWidth: 5, lineCap: .round))
                    .rotationEffect(.degrees(135))
                    .frame(width: 44, height: 44)
                Text("\(Int(value))")
                    .font(.system(size: 11, weight: .bold, design: .monospaced))
            }
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        let delta = -drag.translation.height * 0.4
                        let clamped = min(max(round(value + delta), 0), 127)
                        if clamped != value {
                            value = clamped
                            onChanged(clamped)
                        }
                    }
            )
            Text(title)
                .font(.system(size: 10, weight: .medium))
                .lineLimit(1)
        }
    }
}

struct TransportButton: View {
    let symbol: String
    let label: String
    var isRecord: Bool = false
    let active: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 3) {
                Image(systemName: symbol)
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(isRecord ? .red : (active ? .cyan : .primary))
                Text(label)
                    .font(.system(size: 9, weight: .semibold))
            }
            .frame(width: 52, height: 38)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(active ? Color.cyan.opacity(0.22) : Color.gray.opacity(0.16))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(active ? Color.cyan : Color.gray.opacity(0.3), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
    }
}

struct WheelColumnView: View {
    let title: String
    let normalizedValue: Double
    let displayValue: String

    var body: some View {
        VStack(spacing: 6) {
            Text(displayValue)
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(.secondary)
            GeometryReader { geo in
                let h = geo.size.height
                let norm = min(max(normalizedValue, 0), 1)
                ZStack(alignment: .bottom) {
                    RoundedRectangle(cornerRadius: 6)
                        .fill(Color.black.opacity(0.55))
                    RoundedRectangle(cornerRadius: 3)
                        .fill(Color.orange)
                        .frame(width: 24, height: 10)
                        .offset(y: -((h - 10) * norm))
                }
            }
            .frame(width: 28, height: 130)
            Text(title)
                .font(.system(size: 10, weight: .bold))
        }
    }
}

struct Keyboard49View: View {
    let activeNotes: [Int: Int]
    let onNoteEvent: (Int, Bool) -> Void
    @State private var mousePressedNote: Int? = nil

    private let whiteOffsets = [0, 2, 4, 5, 7, 9, 11]
    private var whiteNotes: [Int] {
        var list: [Int] = []
        for oct in 0..<4 {
            for off in whiteOffsets {
                list.append(36 + oct * 12 + off)
            }
        }
        list.append(84) // Top C5/C6
        return list
    }

    private func isBlackKeyAfterWhiteIndex(_ idx: Int) -> Int? {
        if idx >= 28 { return nil }
        let posInOct = idx % 7
        let oct = idx / 7
        switch posInOct {
        case 0: return 36 + oct * 12 + 1
        case 1: return 36 + oct * 12 + 3
        case 3: return 36 + oct * 12 + 6
        case 4: return 36 + oct * 12 + 8
        case 5: return 36 + oct * 12 + 10
        default: return nil
        }
    }

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let h = geo.size.height
            let whiteCount = whiteNotes.count // 29 white keys
            let whiteW = w / CGFloat(whiteCount)
            let blackW = whiteW * 0.62
            let blackH = h * 0.60

            ZStack(alignment: .topLeading) {
                // White keys
                HStack(spacing: 1) {
                    ForEach(whiteNotes, id: \.self) { note in
                        let vel = activeNotes[note] ?? 0
                        ZStack(alignment: .bottom) {
                            RoundedRectangle(cornerRadius: 3)
                                .fill(vel > 0 ? Color.cyan : Color.white)
                            if note % 12 == 0 {
                                Text(CMEUF5ViewModel.noteName(note))
                                    .font(.system(size: 9, weight: .bold))
                                    .foregroundColor(.black.opacity(0.6))
                                    .padding(.bottom, 6)
                            }
                        }
                        .contentShape(Rectangle())
                        .onTapGesture {
                            onNoteEvent(note, true)
                            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                                onNoteEvent(note, false)
                            }
                        }
                    }
                }

                // Black keys
                ForEach(0..<whiteCount, id: \.self) { idx in
                    if let blackNote = isBlackKeyAfterWhiteIndex(idx) {
                        let vel = activeNotes[blackNote] ?? 0
                        let xPos = CGFloat(idx + 1) * whiteW - (blackW * 0.5)
                        RoundedRectangle(cornerRadius: 3)
                            .fill(vel > 0 ? Color.orange : Color(red: 0.12, green: 0.13, blue: 0.15))
                            .frame(width: blackW, height: blackH)
                            .position(x: xPos, y: blackH * 0.5)
                            .onTapGesture {
                                onNoteEvent(blackNote, true)
                                DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
                                    onNoteEvent(blackNote, false)
                                }
                            }
                    }
                }
            }
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color.black.opacity(0.35)))
    }
}

// MARK: - Tab 2: Controller & Driver Settings

struct DriverSettingsView: View {
    @ObservedObject var vm: CMEUF5ViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                HStack(alignment: .top, spacing: 18) {
                    // Left Column: Velocity Curve & Pitch/Channel
                    VStack(alignment: .leading, spacing: 16) {
                        GroupBox(label: Label("Кривая чувствительности клавиш (Velocity Curve)", systemImage: "chart.xyaxis.line").font(.headline)) {
                            VStack(alignment: .leading, spacing: 12) {
                                Picker("Режим чувствительности:", selection: $vm.velocityCurve) {
                                    Text("Линейная (Стандартная)").tag(0)
                                    Text("Мягкая (Soft — легче играть громко)").tag(1)
                                    Text("Жесткая (Hard — для динамичной игры)").tag(2)
                                    Text("S-образная (S-Curve)").tag(3)
                                    Text("Фиксированная (Fixed Velocity)").tag(4)
                                }
                                .pickerStyle(.radioGroup)

                                if vm.velocityCurve == 4 {
                                    HStack {
                                        Text("Фиксированное значение Velocity: \(vm.fixedVelocity)")
                                        Slider(value: Binding(
                                            get: { Double(vm.fixedVelocity) },
                                            set: { vm.fixedVelocity = Int($0) }
                                        ), in: 1...127)
                                    }
                                }

                                VelocityCurvePreview(vm: vm)
                                    .frame(height: 130)
                            }
                            .padding(10)
                        }

                        GroupBox(label: Label("Октава, Транспонирование и MIDI-канал", systemImage: "music.quarternote.3").font(.headline)) {
                            VStack(alignment: .leading, spacing: 12) {
                                Stepper("Сдвиг октавы: \(vm.octaveShift >= 0 ? "+\(vm.octaveShift)" : "\(vm.octaveShift)") окт.", value: $vm.octaveShift, in: -3...3)
                                Stepper("Транспонирование: \(vm.transposeSemitones >= 0 ? "+\(vm.transposeSemitones)" : "\(vm.transposeSemitones)") полутонов", value: $vm.transposeSemitones, in: -12...12)

                                Picker("MIDI-канал:", selection: $vm.channelOverride) {
                                    Text("Авто (как установлено на CME UF5)").tag(0)
                                    ForEach(1...16, id: \.self) { ch in
                                        Text("Принудительно Канал \(ch)").tag(ch)
                                    }
                                }

                                HStack {
                                    Text("Мертвая зона центра Pitch Bend: \(vm.pitchBendDeadzone)")
                                    Slider(value: Binding(
                                        get: { Double(vm.pitchBendDeadzone) },
                                        set: { vm.pitchBendDeadzone = Int($0) }
                                    ), in: 0...16, step: 1)
                                }
                            }
                            .padding(10)
                        }
                    }

                    // Right Column: Logic Pro Modes & Hardware Filters
                    VStack(alignment: .leading, spacing: 16) {
                        GroupBox(label: Label("Режим работы фейдеров и транспорта", systemImage: "slider.vertical.3").font(.headline)) {
                            VStack(alignment: .leading, spacing: 12) {
                                Picker("Режим 8 фейдеров:", selection: $vm.faderMode) {
                                    Text("Микшер Logic Pro (Каналы 1–8, CC 7 Volume)").tag(0)
                                    Text("Одиночный канал для плагинов (Канал 1, CC 20–27)").tag(1)
                                }
                                .pickerStyle(.radioGroup)

                                Divider()

                                Picker("Режим кнопок транспорта:", selection: $vm.transportMode) {
                                    Text("Logic Pro Control Surface (CC 114–119 + MMC)").tag(0)
                                    Text("Прямой аппаратный проброс (Raw SysEx / Real-Time)").tag(1)
                                }
                                .pickerStyle(.radioGroup)
                            }
                            .padding(10)
                        }

                        GroupBox(label: Label("Аппаратные фильтры протокола CME UF5", systemImage: "line.3.horizontal.decrease.circle").font(.headline)) {
                            VStack(alignment: .leading, spacing: 10) {
                                Toggle("Фильтровать 0xF8 Timing Clock (убирает фоновый пульс)", isOn: $vm.filterClock)
                                Toggle("Фильтровать 0xFE Active Sensing", isOn: $vm.filterActiveSensing)
                                Toggle("Игнорировать послекасание (Aftertouch)", isOn: $vm.filterAftertouch)
                            }
                            .padding(10)
                        }

                        GroupBox(label: Label("Быстрые команды и сброс", systemImage: "wrench.and.screwdriver").font(.headline)) {
                            HStack(spacing: 12) {
                                Button("Отправить GM System On") {
                                    vm.sendGMReset()
                                }
                                Button("Сбросить настройки по умолчанию") {
                                    vm.resetDefaults()
                                }
                            }
                            .padding(10)
                        }
                    }
                }
            }
            .padding(4)
        }
    }
}

struct VelocityCurvePreview: View {
    @ObservedObject var vm: CMEUF5ViewModel

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let h = geo.size.height
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(Color.black.opacity(0.25))

                Path { path in
                    path.move(to: CGPoint(x: 0, y: h))
                    for x in 1...127 {
                        let out = vm.mappedVelocity(for: x)
                        let px = (CGFloat(x) / 127.0) * w
                        let py = h - (CGFloat(out) / 127.0) * h
                        path.addLine(to: CGPoint(x: px, y: py))
                    }
                }
                .stroke(Color.cyan, lineWidth: 2.5)

                if vm.lastInputVelocity > 0 {
                    let inV = vm.lastInputVelocity
                    let outV = vm.mappedVelocity(for: inV)
                    let px = (CGFloat(inV) / 127.0) * w
                    let py = h - (CGFloat(outV) / 127.0) * h
                    Circle()
                        .fill(Color.orange)
                        .frame(width: 10, height: 10)
                        .position(x: px, y: py)
                }
            }
        }
    }
}

// MARK: - Tab 3: Logic Pro Control Surface Integration

struct LogicProSurfaceView: View {
    @ObservedObject var vm: CMEUF5ViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            GroupBox(label: Label("Статус Control Surface для Logic Pro", systemImage: "checkmark.seal.fill").font(.headline)) {
                HStack(spacing: 16) {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(vm.logicSurfaceInstalled
                             ? "Профиль CME UF5 Control Surface установлен и активен"
                             : "Профиль CME UF5 Control Surface еще не установлен")
                            .font(.headline)
                            .foregroundColor(vm.logicSurfaceInstalled ? .green : .orange)
                        Text("Путь: ~/Music/Audio Music Apps/MIDI Device Scripts/CME/UF5.device/config.lua")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Text("Logic Pro автоматически определяет CME UF5 (Manufacturer = CME, Model = UF5, VID 0x7104, PID 0x2202) при запуске.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                    Button("Переустановить профиль Logic Pro") {
                        vm.installLogicProSurface()
                    }
                    Button("Открыть Logic Pro") {
                        vm.openLogicPro()
                    }
                    Button("Audio MIDI Setup") {
                        vm.openAudioMIDISetup()
                    }
                }
                .padding(10)
            }

            GroupBox(label: Label("Таблица назначений CME UF5 в Logic Pro", systemImage: "tablecells").font(.headline)) {
                VStack(alignment: .leading, spacing: 8) {
                    MappingRow(control: "Fader 1 – Fader 8", midi: "Ch 1–8, CC 7 (или Ch 1, CC 20–27)", logicRole: "Громкость треков 1–8 микшера Logic Pro (VFader)")
                    Divider()
                    MappingRow(control: "Master Fader (Fader 9)", midi: "Ch 1, CC 39 (0x27)", logicRole: "Master Volume / Основной уровень")
                    Divider()
                    MappingRow(control: "Knob 1 – Knob 4 (Bank A)", midi: "CC 74, 71, 73, 72", logicRole: "Smart Controls: Cutoff, Resonance, Attack, Release")
                    Divider()
                    MappingRow(control: "Knob 5 – Knob 8 (Bank A)", midi: "CC 10, 91, 93, 94", logicRole: "Pan, Reverb Send, Chorus Send, Variation")
                    Divider()
                    MappingRow(control: "Transport (Top, Rew, Fwd, Rec, Stop, Play)", midi: "Ch 1, CC 114–119 (0x72–0x77)", logicRole: "Управление транспортом Logic Pro (В начало, Перемотка, Запись, Стоп, Старт)")
                    Divider()
                    MappingRow(control: "Pitch Bend / Modulation / Pedals", midi: "PitchBend, CC 1, CC 64, CC 11", logicRole: "Высота тона, Модуляция, Сустейн-педаль, Экспрессия")
                }
                .padding(10)
            }
            Spacer()
        }
    }
}

struct MappingRow: View {
    let control: String
    let midi: String
    let logicRole: String

    var body: some View {
        HStack {
            Text(control)
                .font(.system(size: 13, weight: .semibold))
                .frame(width: 260, alignment: .leading)
            Text(midi)
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(.cyan)
                .frame(width: 240, alignment: .leading)
            Text(logicRole)
                .font(.system(size: 13))
                .foregroundColor(.secondary)
            Spacer()
        }
    }
}

// MARK: - Tab 4: Real-Time MIDI Monitor

struct MIDIMonitorView: View {
    @ObservedObject var vm: CMEUF5ViewModel

    var body: some View {
        VStack(spacing: 10) {
            HStack {
                Toggle("Пауза монитора", isOn: $vm.monitorPaused)
                    .toggleStyle(.checkbox)
                Spacer()
                Text("Событий в буфере: \(vm.logEntries.count)")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Button("Очистить") {
                    vm.logEntries.removeAll()
                }
            }

            List(vm.logEntries) { entry in
                HStack(spacing: 12) {
                    Text(entry.timestamp)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(.secondary)
                        .frame(width: 90, alignment: .leading)
                    Text(entry.type)
                        .font(.system(size: 12, weight: .semibold))
                        .frame(width: 95, alignment: .leading)
                    Text("Ch: \(entry.channel)")
                        .font(.system(size: 11, design: .monospaced))
                        .frame(width: 55, alignment: .leading)
                    Text(entry.detail)
                        .font(.system(size: 12))
                        .frame(maxWidth: .infinity, alignment: .leading)
                    Text("[\(entry.hex)]")
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(.cyan)
                }
            }
            .font(.body)
        }
    }
}

// MARK: - Application Entry Point

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
}

@main
struct CMEUF5ControlApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    var body: some Scene {
        WindowGroup("CME UF5 Control Center") {
            ContentView()
        }
    }
}

