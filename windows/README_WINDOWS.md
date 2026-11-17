# CME UF5 / UF6 / UF7 / UF8 — Пакет драйвера и Control Center для Windows 10/11

Нативный комплект программного обеспечения и драйвера для легендарных USB MIDI мастер-клавиатур серии **CME UF** (**UF5**, **UF6**, **UF7**, **UF8** — `VID 0x7104`, `PID 0x2202`) для современных 64-битных операционных систем **Windows 10** и **Windows 11** (x64, ARM64 и x86).

---

## Проблема CME UF на современных Windows 10 и 11

1. **Нестандартный класс USB**: Контроллер Philips PDIUSBD12 в клавиатурах CME UF сообщает `bInterfaceClass = 0x02` (Communications / CDC) вместо стандартного класса `0x01` SubClass `0x03` (USB MIDI Streaming). Из-за этого встроенный аудио-драйвер Windows `usbaudio.sys` полностью игнорирует устройство.
2. **Устаревшие 32-битные драйверы 2005 года**: Оригинальный драйвер CME (`CMEUF.sys` / `ESIMidiport.sys`) разрабатывался для 32-битной Windows XP/2000. В современных 64-битных версиях Windows 10/11 32-битные драйверы уровня ядра заблокированы политикой Driver Signature Enforcement (DSE), что приводило к невозможности использования USB-порта клавиатуры на современных ПК.
3. **Нестандартный протокол `QUIRK_MIDI_CME`**: Склеенные USB-bulk пакеты, неразбитые SysEx-сообщения с префиксом `0x0F` и фоновые импульсы `0xF8`/`0xFE`, переполнявшие буфер PDIUSBD12 при быстрой игре.

---

## Наше решение для Windows 10/11

Данный пакет решает проблему радикально и безопасно:

1. **Драйвер Microsoft WinUSB (`WinUSB.sys`)**:
   - Использует официальный, подписанный цифровой подписью WHQL драйвер Microsoft, встроенный во все версии Windows 10 и Windows 11.
   - Никаких сторонних нестабильных `.sys` файлов ядра, риска синих экранов (BSOD) и необходимости отключать Secure Boot или включать Test Signing!
   - Устанавливается через файл `cme_uf5.inf` одной командой `pnputil`.

2. **Движок реального времени и виртуальный MIDI-порт (`CMEUF5Bridge.exe`)**:
   - Асинхронное многопоточное чтение через 16 параллельных URB I/O буферов (Endpoint 2 IN `0x82`), полностью исключающее пропуск клавиш при аккордах и glissando.
   - Полный декодер протокола CME (обработка SysEx, фильтрация Active Sensing `0xFE` и Clock `0xF8`).
   - Встроенный виртуальный MIDI-порт:
     - При наличии драйвера **teVirtualMIDI** (или **loopMIDI**) автоматически создает нативный порт **`CME UF5`**, видимый в любых DAW (Cubase, FL Studio, Ableton, Reaper, Studio One, Cakewalk).
     - Поддержка двусторонней связи (Endpoint 2 OUT `0x02`).

3. **Графическая панель управления (`CMEUF5Control.exe`)**:
   - Написана на Win32/GDI+ — один автономный `.exe` размером 170 КБ без зависимостей от тяжелых фреймворков (.NET / WebView2).
   - **Интерактивная визуализация консоли**:
     - 49 клавиш с подсветкой скорости нажатия (Velocity) в реальном времени.
     - 9 фейдеров (Master + Ch 1..8) с индикаторами уровня.
     - 8 регуляторов (Knobs: Cutoff, Reso, Attack, Release, Pan, Reverb, Chorus, Var).
     - Колеса Pitch Bend и Modulation, педали Sustain и Expression.
     - 6 кнопок транспорта (Top, Rew, Fwd, Rec, Stop, Play).
     - Виртуальный флуоресцентный дисплей (LCD).
   - **Настройки кривых чувствительности (Velocity Curves)**:
     - График кривой чувствительности с визуализацией точки касания в реальном времени (Linear, Soft, Hard, S-Curve, Fixed).
     - Сдвиг октав (`-3..+3`) и транспонирование полутонов (`-24..+24`).
     - Выбор MIDI-канала, режима фейдеров и мертвой зоны Pitch Bend.
   - **Интеграция с секвенсорами (DAW)**: Готовые профили и подсказки для Cubase, FL Studio, Ableton, Reaper, Studio One.
   - **MIDI-монитор**: Лог входящих и исходящих пакетов, кнопки `MIDI Panic` и сброса GM.
   - **Сворачивание в системный трей (System Tray)**.

4. **Консольная утилита (`cme-uf5-ctl.exe`)**:
   - Полная диагностика состояния шины USB, драйвера и параметров: `cme-uf5-ctl status`
   - Консольный монитор пакетов в реальном времени: `cme-uf5-ctl monitor`
   - Мгновенный сброс зависших нот: `cme-uf5-ctl panic`
   - Изменение настроек из командной строки: `cme-uf5-ctl set curve 1`

---

## Состав директории `windows/`

```
windows/
├── bin/                          # Скомпилированные 64-битные бинарные файлы
│   ├── CMEUF5Control.exe         # Графическая панель управления (Control Center)
│   ├── CMEUF5Bridge.exe          # Фоновый мост USB -> Virtual MIDI
│   └── cme-uf5-ctl.exe           # CLI-утилита диагностики и мониторинга
├── driver/                       # Драйвер WinUSB
│   ├── cme_uf5.inf               # INF-файл для PnP установки устройства (0x7104:0x2202)
│   ├── install_driver.cmd        # Быстрая установка драйвера администратором
│   └── install_driver.ps1        # PowerShell скрипт установки
├── daw/                          # Пресеты и скрипты для популярных DAW
│   ├── README_DAW.md             # Пошаговая инструкция для каждого секвенсора
│   ├── cubase/                   # Generic Remote XML для Cubase / Nuendo
│   ├── fl_studio/                # Python-скрипт контроллера для FL Studio 20+
│   ├── ableton/                  # UserConfiguration.txt для Ableton Live
│   ├── reaper/                   # ReaperKeyMap с привязкой транспорта
│   └── studio_one/               # Профиль устройства для PreSonus Studio One
├── installer/                    # Установщики
│   ├── install.bat               # Установка в Program Files и создание ярлыков
│   ├── install.ps1               # PowerShell инсталлятор
│   ├── uninstall.bat             # Скрипт удаления
│   └── cme_uf5_setup.iss         # Исходный скрипт Inno Setup 6 для создания Setup.exe
├── src/                          # Исходный код на C / C++17
│   ├── cme_protocol.h / .c       # Декодер USB-MIDI протокола QUIRK_MIDI_CME
│   ├── cme_winusb.h / .c         # Слой работы с WinUSB и асинхронные URB
│   ├── cme_virtual_midi.h / .c   # Виртуальный MIDI-порт (teVirtualMIDI / WinMM)
│   ├── cme_shared_state.h        # Межпроцессное взаимодействие (Shared Memory & Event)
│   ├── cme_service.c             # Исходный код CMEUF5Bridge
│   ├── cme_uf5_ctl.c             # Исходный код CLI-утилиты
│   └── cme_gui_app.cpp           # Исходный код Control Center (GDI+)
├── Makefile                      # Сборка через MinGW-w64
└── CMakeLists.txt                # Сборка через Visual Studio / MSBuild / CLion
```

---

## Быстрая установка

### Вариант 1: Автоматическая установка (Рекомендуется)

1. Запустите файл `windows\installer\install.bat` от имени Администратора (скрипт запросит повышение прав UAC при необходимости).
2. Скрипт установит драйвер WinUSB в хранилище драйверов Windows, скопирует исполняемые файлы в `C:\Program Files\CME UF5` и создаст ярлыки на Рабочем столе и в меню «Пуск».
3. Подключите клавиатуру CME UF5 по USB.
4. Запустите ярлык **CME UF5 Control**.

### Вариант 2: Запуск без установки (Портативный режим)

1. Установите драйвер: правый клик по `windows\driver\cme_uf5.inf` -> **Установить** (или запустите `install_driver.cmd`).
2. Запустите `windows\bin\CMEUF5Control.exe`. Приложение автоматически подключится к клавиатуре и активирует виртуальный MIDI-порт!

---

## Настройка виртуального MIDI-порта (Виртуальный кабель)

Для передачи MIDI-нот и контроллеров из моста в ваши секвенсоры используется виртуальный MIDI-драйвер:
- **teVirtualMIDI** (стандарт де-факто, входит в комплект популярной бесплатной утилиты **loopMIDI** от Тобиаса Эрихсена: [tobias-erichsen.de/software/loopmidi.html](https://www.tobias-erichsen.de/software/loopmidi.html)).
- Если loopMIDI / teVirtualMIDI установлен в системе, порт **`CME UF5`** создается автоматически при старте приложения!
- В ваших DAW (Cubase, FL Studio, Ableton, Reaper и т.д.) просто выберите входное устройство **`CME UF5`**.

---

## Использование CLI-утилиты (`cme-uf5-ctl.exe`)

```cmd
:: Проверка статуса подключения и текущих настроек
cme-uf5-ctl.exe status

:: Запуск монитора MIDI-сообщений в реальном времени
cme-uf5-ctl.exe monitor

:: Отправка команды Panic (сброс всех нот на 16 каналах)
cme-uf5-ctl.exe panic

:: Выбор кривой чувствительности (0=Linear, 1=Soft, 2=Hard, 3=S-Curve, 4=Fixed)
cme-uf5-ctl.exe set curve 1

:: Сдвиг октавы на +1
cme-uf5-ctl.exe set octave 1

:: Транспонирование на 2 полутона вверх
cme-uf5-ctl.exe set transpose 2

:: Переключение режима фейдеров (0 = Ch 1..8 Volume, 1 = Ch 1 CC 20..27)
cme-uf5-ctl.exe set fadermode 0
```

---

## Сборка из исходников

### Сборка через MinGW-w64 (на Windows или кросс-компиляция на macOS/Linux):
```bash
cd windows
make clean
make
```

### Сборка через CMake и Visual Studio:
```cmd
cd windows
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

