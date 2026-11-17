; Inno Setup 6 Script for CME UF5 Windows Package
#define MyAppName "CME UF5 Control Center"
#define MyAppVersion "2.0.0"
#define MyAppPublisher "Central Music Co. / OpenSource"
#define MyAppExeName "CMEUF5Control.exe"

[Setup]
AppId={{D5E2F1B4-3A8C-4D2A-98C3-6A1E2B7C8D9E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\CME UF5
DefaultGroupName=CME UF5
DisableProgramGroupPage=yes
OutputBaseFilename=CME-UF5-Windows-Setup
OutputDir=..\dist
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\bin\CMEUF5Control.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\CMEUF5Bridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\cme-uf5-ctl.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\driver\cme_uf5.inf"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "..\daw\*"; DestDir: "{app}\daw"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\README_WINDOWS.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\DAW Integration Guides"; Filename: "{app}\daw\README_DAW.md"
Name: "{group}\Uninstall CME UF5"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Install WinUSB driver via pnputil
Filename: "pnputil.exe"; Parameters: "/add-driver ""{app}\driver\cme_uf5.inf"" /install"; Flags: runhidden
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

