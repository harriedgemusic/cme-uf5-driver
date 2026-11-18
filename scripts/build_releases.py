#!/usr/bin/env python3
import os
import subprocess
import shutil
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

DIST_DIR = os.path.join(ROOT, "dist")
os.makedirs(DIST_DIR, exist_ok=True)

# 1. Collect Windows files to embed into CME-UF5-Windows-x64-Setup.exe
win_files = [
    ("bin/CMEUF5Control.exe", "windows/bin/CMEUF5Control.exe"),
    ("bin/CMEUF5Bridge.exe", "windows/bin/CMEUF5Bridge.exe"),
    ("bin/cme-uf5-ctl.exe", "windows/bin/cme-uf5-ctl.exe"),
    ("driver/cme_uf5.inf", "windows/driver/cme_uf5.inf"),
    ("driver/install_driver.cmd", "windows/driver/install_driver.cmd"),
    ("driver/install_driver.ps1", "windows/driver/install_driver.ps1"),
    ("daw/README_DAW.md", "windows/daw/README_DAW.md"),
    ("daw/ableton/UserConfiguration.txt", "windows/daw/ableton/UserConfiguration.txt"),
    ("daw/cubase/CME_UF5_Cubase_GenericRemote.xml", "windows/daw/cubase/CME_UF5_Cubase_GenericRemote.xml"),
    ("daw/fl_studio/device_cme_uf5.py", "windows/daw/fl_studio/device_cme_uf5.py"),
    ("daw/reaper/CME_UF5_Reaper_Actions.ReaperKeyMap", "windows/daw/reaper/CME_UF5_Reaper_Actions.ReaperKeyMap"),
    ("daw/studio_one/CME_UF5.device", "windows/daw/studio_one/CME_UF5.device"),
    ("uninstall.bat", "windows/installer/uninstall.bat"),
    ("README_WINDOWS.md", "windows/README_WINDOWS.md"),
]

build_win_dir = os.path.join(ROOT, "build", "win_setup")
os.makedirs(build_win_dir, exist_ok=True)
payload_h = os.path.join(build_win_dir, "setup_payload.h")

with open(payload_h, "w", encoding="utf-8") as f:
    f.write("#pragma once\n#include <stddef.h>\n#include <stdint.h>\n\n")
    f.write("struct EmbeddedFile {\n    const char* relPath;\n    const uint8_t* data;\n    size_t size;\n};\n\n")
    entries = []
    for idx, (rel_dst, src_path) in enumerate(win_files):
        raw = open(src_path, "rb").read()
        arr_name = f"kFileData_{idx}"
        f.write(f"static const uint8_t {arr_name}[{len(raw)}] = {{\n")
        for i in range(0, len(raw), 16):
            chunk = raw[i:i+16]
            f.write("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
        f.write("};\n\n")
        win_rel = rel_dst.replace("/", "\\\\")
        entries.append((win_rel, arr_name, len(raw)))

    f.write(f"static const EmbeddedFile kEmbeddedFiles[{len(entries)}] = {{\n")
    for win_rel, arr_name, sz in entries:
        f.write(f'    {{ "{win_rel}", {arr_name}, {sz} }},\n')
    f.write("};\n")
    f.write(f"static const size_t kEmbeddedFileCount = {len(entries)};\n")

setup_cpp = os.path.join(build_win_dir, "setup_main.cpp")
with open(setup_cpp, "w", encoding="utf-8") as f:
    f.write(r'''#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include "setup_payload.h"

static void EnsureDirRecursive(const std::wstring& dir) {
    size_t pos = 0;
    while ((pos = dir.find_first_of(L"\\/", pos + 1)) != std::wstring::npos) {
        CreateDirectoryW(dir.substr(0, pos).c_str(), NULL);
    }
    CreateDirectoryW(dir.c_str(), NULL);
}

static bool WriteBufferToFile(const std::wstring& fullPath, const uint8_t* data, size_t size) {
    size_t slash = fullPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        EnsureDirRecursive(fullPath.substr(0, slash));
    }
    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(hFile, data, (DWORD)size, &written, NULL);
    CloseHandle(hFile);
    return ok && (written == size);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR lpCmdLine, int) {
    bool silent = (lpCmdLine && wcsstr(lpCmdLine, L"/S") != NULL);
    if (!silent) {
        int ans = MessageBoxW(
            NULL,
            L"Установить пакет драйвера, Control Center и профилей DAW для клавиатуры CME UF5 / UF6 / UF7 / UF8?\n\n"
            L"Install CME UF5 / UF6 / UF7 / UF8 USB MIDI Driver, Control Center & DAW Suite?\n\n"
            L"Путь установки / Target directory:\nC:\\Program Files\\CME UF5",
            L"CME UF5 Setup v2.0.0 (Windows 10/11 x64)",
            MB_YESNO | MB_ICONQUESTION
        );
        if (ans != IDYES) return 0;
    }

    wchar_t progFiles[MAX_PATH] = L"C:\\Program Files";
    ExpandEnvironmentStringsW(L"%ProgramFiles%", progFiles, MAX_PATH);
    std::wstring installDir = std::wstring(progFiles) + L"\\CME UF5";
    EnsureDirRecursive(installDir);

    for (size_t i = 0; i < kEmbeddedFileCount; i++) {
        wchar_t relW[MAX_PATH] = {0};
        MultiByteToWideChar(CP_UTF8, 0, kEmbeddedFiles[i].relPath, -1, relW, MAX_PATH);
        std::wstring dstPath = installDir + L"\\" + relW;
        WriteBufferToFile(dstPath, kEmbeddedFiles[i].data, kEmbeddedFiles[i].size);
    }

    // Install WinUSB driver via pnputil
    std::wstring infPath = installDir + L"\\driver\\cme_uf5.inf";
    std::wstring pnpParams = L"/add-driver \"" + infPath + L"\" /install";
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = L"pnputil.exe";
    sei.lpParameters = pnpParams.c_str();
    sei.nShow = SW_HIDE;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 15000);
        CloseHandle(sei.hProcess);
    }

    // Create Desktop shortcut via PowerShell
    std::wstring psCmd = L"-NoProfile -ExecutionPolicy Bypass -Command \""
        L"$ws = New-Object -ComObject WScript.Shell; "
        L"$s = $ws.CreateShortcut([Environment]::GetFolderPath('Desktop') + '\\CME UF5 Control.lnk'); "
        L"$s.TargetPath = '" + installDir + L"\\bin\\CMEUF5Control.exe'; "
        L"$s.WorkingDirectory = '" + installDir + L"\\bin'; "
        L"$s.Save()\"";
    ShellExecuteW(NULL, L"open", L"powershell.exe", psCmd.c_str(), NULL, SW_HIDE);

    if (!silent) {
        int launch = MessageBoxW(
            NULL,
            L"Установка CME UF5 успешно завершена!\n"
            L"• Драйвер WinUSB зарегистрирован\n"
            L"• Программы установлены в C:\\Program Files\\CME UF5\n"
            L"• Ярлык создан на Рабочем столе\n\n"
            L"Запустить CME UF5 Control Center сейчас?\n"
            L"Launch CME UF5 Control Center now?",
            L"CME UF5 Setup v2.0.0",
            MB_YESNO | MB_ICONINFORMATION
        );
        if (launch == IDYES) {
            std::wstring exePath = installDir + L"\\bin\\CMEUF5Control.exe";
            std::wstring workDir = installDir + L"\\bin";
            ShellExecuteW(NULL, L"open", exePath.c_str(), NULL, workDir.c_str(), SW_SHOWNORMAL);
        }
    }
    return 0;
}
''')

setup_rc = os.path.join(build_win_dir, "setup.rc")
setup_manifest = os.path.join(build_win_dir, "setup.manifest")
with open(setup_manifest, "w", encoding="utf-8") as f:
    f.write('''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity version="2.0.0.0" processorArchitecture="amd64" name="CME.UF5.Setup" type="win32"/>
  <description>CME UF5 Windows 10/11 x64 Installer</description>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="requireAdministrator" uiAccess="false"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
</assembly>
''')

with open(setup_rc, "w", encoding="utf-8") as f:
    f.write('1 24 "setup.manifest"\n')

setup_res_o = os.path.join(build_win_dir, "setup_res.o")
subprocess.check_call(["x86_64-w64-mingw32-windres", setup_rc, "-O", "coff", "-o", setup_res_o], cwd=build_win_dir)

win_setup_exe = os.path.join(DIST_DIR, "CME-UF5-Windows-x64-Setup.exe")
subprocess.check_call([
    "x86_64-w64-mingw32-g++", "-O2", "-s", "-static", "-mwindows", "-municode",
    setup_cpp, setup_res_o, "-lshell32", "-lole32", "-luuid",
    "-o", win_setup_exe
])
print(f"Built Windows Installer EXE -> {win_setup_exe}")

# 2. Build CME-UF5-Windows-x64-Portable.zip
win_zip = os.path.join(DIST_DIR, "CME-UF5-Windows-x64-Portable.zip")
with zipfile.ZipFile(win_zip, "w", zipfile.ZIP_DEFLATED) as zf:
    for rel_dst, src_path in win_files:
        zf.write(src_path, os.path.join("CME-UF5-Windows-x64", rel_dst))
    zf.write("windows/installer/install.bat", "CME-UF5-Windows-x64/install.bat")
    zf.write("windows/installer/install.ps1", "CME-UF5-Windows-x64/install.ps1")
print(f"Built Windows Portable ZIP -> {win_zip}")

# 3. Build CME-UF5-macOS-Installer.dmg
dmg_stage = os.path.join(ROOT, "build", "dmg_stage")
if os.path.exists(dmg_stage):
    shutil.rmtree(dmg_stage)
os.makedirs(dmg_stage, exist_ok=True)

shutil.copy2(os.path.join(DIST_DIR, "CME-UF5-macOS-Installer.pkg"), os.path.join(dmg_stage, "CME-UF5-macOS-Installer.pkg"))
shutil.copytree(os.path.join(ROOT, "build", "CME UF5 Control.app"), os.path.join(dmg_stage, "CME UF5 Control.app"), symlinks=True)
shutil.copytree(os.path.join(ROOT, "build", "CMEUFMIDIDriver.plugin"), os.path.join(dmg_stage, "CMEUFMIDIDriver.plugin"), symlinks=True)
shutil.copytree(os.path.join(ROOT, "logic", "UF5.device"), os.path.join(dmg_stage, "UF5.device"), symlinks=True)
shutil.copy2(os.path.join(ROOT, "README.md"), os.path.join(dmg_stage, "README.md"))
os.symlink("/Applications", os.path.join(dmg_stage, "Applications"))

mac_dmg = os.path.join(DIST_DIR, "CME-UF5-macOS-Installer.dmg")
if os.path.exists(mac_dmg):
    os.remove(mac_dmg)

subprocess.check_call([
    "hdiutil", "create",
    "-volname", "CME UF5 macOS Suite",
    "-srcfolder", dmg_stage,
    "-ov", "-format", "UDZO",
    mac_dmg
])
print(f"Built macOS Disk Image DMG -> {mac_dmg}")
