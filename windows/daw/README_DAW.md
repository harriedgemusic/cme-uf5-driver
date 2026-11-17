# Руководство по настройке CME UF5 / UF6 / UF7 / UF8 в Windows DAW

После установки драйвера WinUSB и запуска **CME UF5 Control Center** в системе появляется виртуальный MIDI-порт **`CME UF5`**. Ниже приведена пошаговая инструкция по подключению клавиатуры в популярных секвенсорах (DAW).

---

## 1. Cubase / Nuendo

1. Откройте **Studio** -> **Studio Setup...** (или *Devices* -> *Device Setup...*).
2. В разделе **MIDI Port Setup**:
   - Убедитесь, что для входа **CME UF5** стоит галочка в колонке **"In 'All MIDI Inputs'"**.
3. Добавление контрольной панели микшера и транспорта:
   - В левом верхнем углу окна Studio Setup нажмите значок **`+`** и выберите **Generic Remote**.
   - В поле **MIDI Input** выберите **CME UF5**.
   - Нажмите кнопку **Import** и выберите файл:
     `windows\daw\cubase\CME_UF5_Cubase_GenericRemote.xml`
   - Нажмите **Apply** и **OK**.
4. Теперь кнопки транспорта (Play, Stop, Rec, Rewind, Forward, Top) управляют воспроизведением проекта, а фейдеры 1–8 регулируют громкость первых 8 дорожек микшера!

---

## 2. FL Studio

1. Откройте **Options** -> **MIDI Settings** (или нажмите клавишу **F10**).
2. В списке **Input** выберите **CME UF5**:
   - Включите тумблер **Enable** (светодиод загорится оранжевым).
   - В поле **Controller type** выберите **(generic controller)** или используйте встроенный Python-скрипт.
3. Установка нативного скрипта интеграции с FL Studio:
   - Скопируйте папку со скриптом в директорию пользовательских скриптов:
     `%USERPROFILE%\Documents\Image-Line\FL Studio\Settings\Hardware\CME_UF5\`
   - Поместите туда файл `windows\daw\fl_studio\device_cme_uf5.py`.
   - В настройках MIDI FL Studio выберите контроллер **CME UF Series Controller**.
4. Кнопки транспорта и фейдеры автоматически синхронизируются с микшером FL Studio!

---

## 3. Ableton Live

1. Скопируйте файл `windows\daw\ableton\UserConfiguration.txt` в папку:
   `%APPDATA%\Ableton\Live [Ваша_Версия]\Preferences\User Remote Scripts\CME_UF5\`
   *(Если папки `User Remote Scripts` или `CME_UF5` не существуют, создайте их).*
2. Запустите Ableton Live и откройте **Preferences** (Ctrl + ,) -> вкладка **Link / MIDI**.
3. В первой строке **Control Surface**:
   - Выберите **CME_UF5**.
   - В качестве **Input** выберите **CME UF5**.
4. В секции **MIDI Ports** включите тумблеры:
   - `Input: CME UF5` -> **Track**: ON, **Remote**: ON.

---

## 4. Cockos REAPER

1. Откройте **Options** -> **Preferences** (Ctrl + P) -> **Audio** -> **MIDI Devices**.
2. В списке MIDI inputs найдите **CME UF5**:
   - Щелкните правой кнопкой мыши -> **Enable input** и **Enable input for control messages**.
3. Привязка кнопок транспорта:
   - Откройте **Actions** -> **Show action list...** (клавиша `?`).
   - Нажмите **Key map** -> **Import...** и выберите:
     `windows\daw\reaper\CME_UF5_Reaper_Actions.ReaperKeyMap`.
   - Кнопки Play, Stop, Record, Rewind, Fast Forward и Return to Zero начнут управлять транспортом REAPER.

---

## 5. PreSonus Studio One

1. Откройте **Studio One** -> **Options** -> **External Devices**.
2. Нажмите **Add...** (Добавить):
   - Выберите **New Keyboard** (Новая клавиатура).
   - Имя устройства: `CME UF5`.
   - **Receive From**: выберите `CME UF5`.
   - **Send To**: выберите `CME UF5`.
3. Для транспорта и фейдеров:
   - Нажмите **Add...** -> **New Control Surface**.
   - **Receive From**: `CME UF5`.
   - Импортируйте профиль `windows\daw\studio_one\CME_UF5.device`.

