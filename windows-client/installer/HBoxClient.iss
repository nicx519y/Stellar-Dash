#define MyAppName "HBox Client"
#define MyAppVersion "0.1.0-internal"
#define MyAppExeName "HBoxClient.exe"

[Setup]
AppId={{CB104688-0D8D-4EDB-A86B-48424F584331}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={localappdata}\Programs\HBox Client
DefaultGroupName=HBox Client
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
OutputBaseFilename=HBoxClient-internal-x64
Compression=lzma2
SolidCompression=yes

[Files]
Source: "..\build-msvc\HBoxClient.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\ui\out\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs createallsubdirs
; Internal dependency supplied by the build environment. Never download it here.
Source: "..\third_party\vigem\bin\ViGEmClient.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\HBox Client"; Filename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--install-autostart"; Flags: runhidden waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Parameters: "--background"; Description: "启动 HBox Client"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--remove-autostart"; Flags: runhidden waituntilterminated skipifdoesntexist
