# CME UF5 / UF6 / UF7 / UF8 — macOS & Windows Driver, Control Center & DAW Suite

[**🇷🇺 Русский**](#-русский) | [**🇬🇧 English**](#-english)

---

<a name="-русский"></a>
## 🇷🇺 Русский

Нативный комплект драйверов, графической панели управления (**Control Center**), профилей **Control Surface для Logic Pro / DAW** и установщиков для USB MIDI мастер-клавиатур серии **CME UF** (**UF5**, **UF6**, **UF7**, **UF8** — `VID 0x7104`, `PID 0x2202`) под современные **macOS** (Apple Silicon `arm64` и Intel `x86_64`) и **Windows 10/11** (`x64`).

### Техническая проблема клавиатур CME UF и её решение

1. **Нестандартный USB-дескриптор**: USB-контроллер **Philips PDIUSBD12** внутри клавиатур CME UF сообщает класс интерфейса `bInterfaceClass = 0x02` (CDC Control), `bInterfaceSubClass = 0x01`, `bInterfaceProtocol = 0x01` вместо стандартного класса USB Audio/MIDI Streaming (`0x01` / `0x03`). Из-за этого стандартные драйверы macOS (`AppleMIDIUSBDriver`) и Windows (`usbaudio.sys`) игнорируют устройство.
2. **Нестандартный протокол `QUIRK_MIDI_CME`**:
   - Сообщения SysEx (`0xF0..0xF7`), Song Position Pointer (`0xF2`), System Real-Time (`0xF8`, `0xFA`, `0xFB`, `0xFC`) и Active Sensing (`0xFE`) передаются с префиксом `0x0F` (Code Index Number) без разбиения SysEx на 4-байтные чанки.
   - Несколько MIDI-сообщений склеиваются в один 64-байтный USB Bulk-пакет.
3. **Защита от пропуска нот (ZLP FIFO Drain)**:
   - В режиме простоя микроконтроллер 8051 в клавиатуре каждые ~1.5 мс подтверждает пустые пакеты (Zero-Length Packets, ZLP) в двойном аппаратном буфере Endpoint 2 IN (`0x82`). Если хост не вычитывает Bulk IN непрерывно, оба банка памяти PDIUSBD12 заполняются ZLP, и нажатия клавиш теряются («срабатывают через раз»).
   - Наш драйвер держит в аппаратном кольце xHCI **32 параллельных асинхронных запроса чтения (`ReadPipeAsync`)** без задержек `usleep` в потоке реального времени (`QOS_CLASS_USER_INTERACTIVE`), что гарантирует **0% потерь нот** при минимальной нагрузке на CPU.

---

### Поддержка macOS (Apple Silicon & Intel)

#### 1. Драйвер CoreMIDI (`CMEUFMIDIDriver.plugin`)
- Универсальный плагин (`arm64` + `x86_64`) для `/Library/Audio/MIDI Drivers/` и `~/Library/Audio/MIDI Drivers/`.
- Поддержка горячего подключения USB, автоматического сброса `ClearPipeStallBothEnds` на Endpoint 2 IN (`0x82`) и OUT (`0x02`).
- Обработка параметров в реальном времени без перезапуска `MIDIServer` (через Darwin Notification `com.cme-pro.midi.driver.uf.configChanged`):
  - **Кривая чувствительности (`Velocity Curve`)**: Линейная (`Linear`), Мягкая (`Soft`), Жесткая (`Hard`), S-образная (`S-Curve`), Фиксированная (`Fixed Velocity 1..127`).
  - **Сдвиг октавы (`-3..+3`) и транспонирование (`-24..+24` полутонов)**.
  - **Принудительный выбор MIDI-канала (`1..16`)** и мертвая зона центра колеса **Pitch Bend (`0..16`)**.
  - **Трансляция кнопок транспорта** (`MMC SysEx`, `0xFA`/`0xFB`/`0xFC`, `0xF2 SPP`) в управляющие CC-команды Logic Pro (`CC 114–119`).
  - **Фильтры**: подавление фоновых импульсов `0xF8 Timing Clock`, `0xFE Active Sensing` и `Aftertouch`.

#### 2. Приложение настроек (`CME UF5 Control.app`)
Нативное macOS-приложение (`SwiftUI` + `CoreMIDI` + `IOKit`), устанавливаемое в `/Applications/CME UF5 Control.app`:
- **Панель контроллера**: живая интерактивная визуализация корпуса CME UF5 — синий цифровой дисплей, **9 фейдеров** (`Master` + `Ch 1–8`), **8 регуляторов (Knobs)** (`Cutoff`, `Resonance`, `Attack`, `Release`, `Pan`, `Reverb`, `Chorus`, `Variation`), **6 кнопок транспорта** (`Top`, `Rew`, `Fwd`, `Rec`, `Stop`, `Play`), колеса **Pitch Bend / Modulation**, индикаторы педалей и **49-клавишная клавиатура (`C2–C6`)**, подсвечивающая нажатые ноты и скорость нажатия.
- **Настройки контроллера**: управление кривой Velocity с живым графиком, октавами, транспонированием, режимом фейдеров и фильтрами.
- **Logic Pro Surface**: проверка статуса и установка профиля Control Surface для Logic Pro одним нажатием.
- **MIDI Монитор**: лог входящих/исходящих MIDI-сообщений с расшифровкой, кнопки `MIDI Panic (All Notes Off)` и `GM System On`.

#### 3. Surface Controller для Logic Pro (`logic/UF5.device`)
Устанавливается в `~/Music/Audio Music Apps/MIDI Device Scripts/CME/UF5.device/config.lua` и автоматически распознается Logic Pro при запуске:

| Элемент на CME UF5 | MIDI-сообщение | Назначение в Logic Pro |
| :--- | :--- | :--- |
| **Fader 1 – Fader 8** | `Ch 1–8, CC 7` (или `Ch 1, CC 20–27`) | Громкость каналов 1–8 микшера (`VFader`) |
| **Master Fader (9)** | `Ch 1, CC 39 (0x27)` | Основной уровень (`M.Volume`) |
| **Knob 1 – Knob 4** | `CC 74, 71, 73, 72` | Smart Controls: `Cutoff`, `Resonance`, `Attack`, `Release` |
| **Knob 5 – Knob 8** | `CC 10, 91, 93, 94` | `Pan`, `Reverb Send`, `Chorus Send`, `Variation` |
| **Transport (6 кнопок)** | `Ch 1, CC 114–119 (0x72–0x77)` | `Record`, `Top`, `Rewind`, `Forward`, `Stop`, `Play` |
| **Колеса и педали** | `Pitch Bend`, `CC 1`, `CC 64`, `CC 11` | `Pitch Bend`, `Modulation`, `Sustain`, `Expression` |

#### 4. Быстрая установка и сборка на macOS

Готовый графический установщик `.pkg` находится в `dist/CME-UF5-macOS-Installer.pkg`:
```zsh
open dist/CME-UF5-macOS-Installer.pkg
```

Или сборка и установка из исходного кода:
```zsh
make install
```

Диагностика и мониторинг через CLI-утилиту `cme-uf5-ctl`:
```zsh
./build/cme-uf5-ctl status
./build/cme-uf5-ctl monitor
./build/cme-uf5-ctl clock off
./build/cme-uf5-ctl reload
```

---

### Поддержка Windows 10 / 11 (`windows/`)

В директории [`windows/`](windows/README_WINDOWS.md) расположен полный нативный комплект для 64-битных **Windows 10 / 11**:
- **`windows/driver/cme_uf5.inf`** — привязка к официальному подписанному WHQL-драйверу Microsoft `WinUSB.sys` (без необходимости отключать Secure Boot или Driver Signature Enforcement).
- **`windows/bin/CMEUF5Bridge.exe`** — фоновый высокоприоритетный мост `WinUSB -> Virtual MIDI` (16 параллельных URB).
- **`windows/bin/CMEUF5Control.exe`** — графическая панель управления на Win32/GDI+ с поддержкой системного трея.
- **`windows/daw/`** — готовые профили для **Cubase / Nuendo**, **FL Studio**, **Ableton Live**, **Reaper** и **Studio One**.

Подробная документация по Windows-версии: [`windows/README_WINDOWS.md`](windows/README_WINDOWS.md).

---
---

<a name="-english"></a>
## 🇬🇧 English

Native driver suite, GUI **Control Center** application, **Logic Pro / DAW Control Surface** scripts, and installers for the **CME UF Series** USB MIDI Master Keyboards (**UF5**, **UF6**, **UF7**, **UF8** — `VID 0x7104`, `PID 0x2202`) on modern **macOS** (Apple Silicon `arm64` & Intel `x86_64`) and **Windows 10/11** (`x64`).

### Technical Background & Architecture

1. **Non-Standard USB Interface Class**: The **Philips PDIUSBD12** USB controller inside CME UF keyboards reports `bInterfaceClass = 0x02` (CDC Control), `bInterfaceSubClass = 0x01`, `bInterfaceProtocol = 0x01` instead of standard USB Audio/MIDI Streaming (`0x01` / `0x03`). As a result, macOS's built-in `AppleMIDIUSBDriver` and Windows's `usbaudio.sys` ignore the keyboard.
2. **Non-Standard `QUIRK_MIDI_CME` Wire Protocol**:
   - SysEx (`0xF0..0xF7`), Song Position Pointer (`0xF2`), System Real-Time (`0xF8`, `0xFA`, `0xFB`, `0xFC`), and Active Sensing (`0xFE`) are prefixed with a `0x0F` Code Index Number (CIN) byte, and SysEx payloads are sent contiguously without 4-byte chunking.
   - Multiple MIDI events are concatenated inside a single 64-byte USB Bulk packet.
3. **Zero Note Drops (Multi-URB ZLP Drain)**:
   - When idle, the keyboard's 8051 MCU validates Zero-Length Packets (ZLPs) into the double-buffered Endpoint 2 IN (`0x82`) FIFO every ~1.5 ms. If the host pauses polling even briefly, both hardware banks fill up with ZLPs and the 8051 drops Note On/Off events during key presses.
   - Our driver keeps **32 concurrent asynchronous Bulk IN transfers (`ReadPipeAsync`)** queued in the xHCI hardware ring on a `QOS_CLASS_USER_INTERACTIVE` thread with zero artificial sleep delays, ensuring **zero dropped notes** and minimal CPU usage.

---

### macOS Suite (Apple Silicon & Intel Universal)

#### 1. CoreMIDI Driver Plug-in (`CMEUFMIDIDriver.plugin`)
- Universal (`arm64` + `x86_64`) CFPlugIn bundle installed to `/Library/Audio/MIDI Drivers/` and `~/Library/Audio/MIDI Drivers/`.
- Hot-plug support via `IOServiceAddMatchingNotification` and pipe toggle reset via `ClearPipeStallBothEnds` on Endpoint 2 IN (`0x82`) and OUT (`0x02`).
- Live DSP configuration reload without restarting `MIDIServer` (via Darwin notification `com.cme-pro.midi.driver.uf.configChanged`):
  - **Velocity Curves**: Linear, Soft, Hard, S-Curve, and Fixed (`1..127`) using an O(1) lookup table.
  - **Octave Shift (`-3..+3`) & Semitone Transpose (`-24..+24`)**.
  - **MIDI Channel Override (`1..16`)** and **Pitch Bend Center Deadzone (`0..16`)**.
  - **Transport Translation**: Translates CME UF5 transport messages (`MMC SysEx`, `0xFA`/`0xFB`/`0xFC`, `0xF2 SPP`) into clean Logic Pro Control Surface CC triggers (`CC 114–119`).
  - **Hardware Filters**: Filters `0xF8 Timing Clock` heartbeats, `0xFE Active Sensing`, and `Aftertouch`.

#### 2. Controller Settings Application (`CME UF5 Control.app`)
Native macOS GUI application (`SwiftUI` + `CoreMIDI` + `IOKit`) installed in `/Applications/CME UF5 Control.app`:
- **Controller Surface Tab**: Real-time interactive visualization of the CME UF5 — blue digital LED display, **9 Faders** (`Master` + `Ch 1–8`), **8 Rotary Knobs** (`Cutoff`, `Resonance`, `Attack`, `Release`, `Pan`, `Reverb`, `Chorus`, `Variation`), **6 Transport Buttons** (`Top`, `Rew`, `Fwd`, `Rec`, `Stop`, `Play`), **Pitch Bend & Modulation Wheels**, pedal indicators, and a **49-key virtual keyboard (`C2–C6`)** highlighting pressed keys and velocity in real time.
- **Controller & Driver Settings Tab**: Interactive Velocity Curve editor with live input/output dot, octave/transpose controls, fader/transport mode selectors, and protocol filters.
- **Logic Pro Surface Tab**: One-click installation and status check for the Logic Pro Control Surface profile.
- **MIDI Monitor Tab**: Real-time incoming/outgoing MIDI packet inspector plus `MIDI Panic (All Notes Off)` and `GM System On` tools.

#### 3. Logic Pro Control Surface (`logic/UF5.device`)
Installed to `~/Music/Audio Music Apps/MIDI Device Scripts/CME/UF5.device/config.lua` and automatically matched by Logic Pro on startup:

| CME UF5 Control | MIDI Message | Logic Pro Assignment |
| :--- | :--- | :--- |
| **Fader 1 – Fader 8** | `Ch 1–8, CC 7` (or `Ch 1, CC 20–27`) | Mixer Channel Strips 1–8 Volume (`VFader`) |
| **Master Fader (9)** | `Ch 1, CC 39 (0x27)` | Master Volume (`M.Volume`) |
| **Knob 1 – Knob 4** | `CC 74, 71, 73, 72` | Smart Controls: `Cutoff`, `Resonance`, `Attack`, `Release` |
| **Knob 5 – Knob 8** | `CC 10, 91, 93, 94` | `Pan`, `Reverb Send`, `Chorus Send`, `Variation` |
| **Transport (6 Buttons)** | `Ch 1, CC 114–119 (0x72–0x77)` | `Record`, `Top`, `Rewind`, `Forward`, `Stop`, `Play` |
| **Wheels & Pedals** | `Pitch Bend`, `CC 1`, `CC 64`, `CC 11` | `Pitch Bend`, `Modulation`, `Sustain`, `Expression` |

#### 4. macOS Installation & Build Instructions

Run the pre-built macOS `.pkg` installer in `dist/CME-UF5-macOS-Installer.pkg`:
```zsh
open dist/CME-UF5-macOS-Installer.pkg
```

Or build and install everything from source:
```zsh
make install
```

Inspect status or monitor MIDI events from the terminal with `cme-uf5-ctl`:
```zsh
./build/cme-uf5-ctl status
./build/cme-uf5-ctl monitor
./build/cme-uf5-ctl clock off
./build/cme-uf5-ctl reload
```

---

### Windows 10 / 11 Suite (`windows/`)

The [`windows/`](windows/README_WINDOWS.md) directory contains a complete native 64-bit suite for **Windows 10 and Windows 11**:
- **`windows/driver/cme_uf5.inf`** — binds the CME UF5 (`VID_7104&PID_2202`) to Microsoft's official WHQL-signed `WinUSB.sys` driver (no need to disable Secure Boot or Driver Signature Enforcement).
- **`windows/bin/CMEUF5Bridge.exe`** — real-time multi-URB `WinUSB -> Virtual MIDI` bridge service.
- **`windows/bin/CMEUF5Control.exe`** — lightweight native Win32/GDI+ Control Center GUI with system tray integration.
- **`windows/daw/`** — ready-to-use Control Surface profiles and scripts for **Cubase / Nuendo**, **FL Studio**, **Ableton Live**, **Reaper**, and **Studio One**.

---

### Repository Structure / Структура репозитория

```text
.
├── src/
│   ├── CMEUFMIDIDriver.c         # macOS CoreMIDI Driver Plug-in (C / IOKit / CoreMIDI)
│   └── cme_uf5_ctl.c             # macOS CLI diagnostics & real-time MIDI monitor
├── app/
│   ├── CMEUF5Control.swift       # macOS Control Center GUI application (SwiftUI)
│   └── Info.plist                # App bundle metadata
├── logic/
│   └── UF5.device/
│       ├── config.lua            # Logic Pro / MainStage Control Surface script
│       ├── icon.png              # Device icon (1x)
│       └── icon@2x.png           # Device icon (2x Retina)
├── installer/
│   ├── Distribution.xml          # macOS productbuild installer specification
│   ├── welcome.html              # Installer welcome screen
│   ├── conclusion.html           # Installer completion screen
│   └── scripts/postinstall       # Post-install hook (user Library + MIDIServer reload)
├── scripts/
│   ├── generate_icons.swift      # CoreGraphics iconset generator
│   └── build_pkg.sh              # macOS .pkg builder script
├── dist/
│   └── CME-UF5-macOS-Installer.pkg # Ready-to-use macOS Installer Package
├── windows/                      # Windows 10/11 Driver, Bridge, Control App & DAW scripts
├── Info.plist                    # CoreMIDI CFPlugIn Info.plist
├── Makefile                      # Build system for Driver, App, CLI & .pkg
├── install.sh                    # Quick install script for macOS
└── uninstall.sh                  # Quick uninstall script for macOS
```
