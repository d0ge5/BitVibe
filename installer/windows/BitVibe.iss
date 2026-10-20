; BitVibe - Windows installer (Inno Setup 6).
; Build with installer\windows\build_installer.bat after building the plugin (build.bat).
; The VST3 goes to the standard VST3 folder: "C:\Program Files\Common Files\VST3" for an all-users
; install, or "%LOCALAPPDATA%\Programs\Common\VST3" if the user picks "Install for me only".

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#define MyAppName "BitVibe"
#define BuildDir "..\..\build\BitVibe_artefacts\Release"

[Setup]
; Never change AppId: Windows uses it to recognise upgrades and the uninstall entry.
AppId={{BAD024DF-E4D4-4868-BD76-AB2394315DCF}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher=BitVibe
AppPublisherURL=https://github.com/d0ge5/BitVibe
AppSupportURL=https://github.com/d0ge5/BitVibe
VersionInfoVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Windows-x64-Setup
UninstallDisplayName={#MyAppName} {#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline

[Types]
Name: "full"; Description: "Full installation (VST3 plug-in and Standalone app)"
Name: "plugin"; Description: "VST3 plug-in only"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full plugin custom; Flags: fixed
Name: "standalone"; Description: "Standalone application (use without a DAW)"; Types: full

[Files]
; {autocf} is "C:\Program Files\Common Files" for an all-users install and
; %LOCALAPPDATA%\Programs\Common for a per-user install. (Do not use {commoncf} or {commoncf64}:
; they stay on the system folder even in per-user mode, which a non-admin cannot write to.)
Source: "{#BuildDir}\VST3\{#MyAppName}.vst3\*"; DestDir: "{autocf}\VST3\{#MyAppName}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\{#MyAppName}.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\NOTICE.md"; DestDir: "{app}"; DestName: "NOTICE.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppName}.exe"; Components: standalone
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
