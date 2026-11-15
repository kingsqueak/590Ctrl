; Windows installer for 590Ctrl (Inno Setup 6). Built by CI after
; scripts/make-windows.sh: iscc /DAppVersion=<ver> /DSourceDir=<folder> this file.
; Output: dist/590Ctrl-<ver>-win-x64-setup.exe
;
; Installs for the current user only (no administrator password) in
; %LOCALAPPDATA%\Programs\590Ctrl, with a Start menu entry, an optional
; desktop icon and an uninstaller. Settings live in the registry
; (HKCU\Software\590Ctrl) and the EQ presets in %APPDATA%\590Ctrl, outside the
; program folder, so upgrades keep them and uninstalling leaves them.
; Not code-signed: SmartScreen still warns the first time.

#ifndef AppVersion
  #error AppVersion must be defined (/DAppVersion=...)
#endif
#ifndef SourceDir
  #error SourceDir must be defined (/DSourceDir=...)
#endif

[Setup]
; The AppId identifies 590Ctrl to Windows across versions: never change it.
AppId={{14FF94D0-E411-4931-A374-F10EB249D0CC}
AppName=590Ctrl
AppVersion={#AppVersion}
AppVerName=590Ctrl {#AppVersion}
AppPublisher=KC2RGW
AppPublisherURL=https://github.com/kingsqueak/590Ctrl
AppSupportURL=https://github.com/kingsqueak/590Ctrl/issues
AppUpdatesURL=https://github.com/kingsqueak/590Ctrl/releases
DefaultDirName={localappdata}\Programs\590Ctrl
DefaultGroupName=590Ctrl
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
SetupIconFile=..\resources\590Ctrl.ico
UninstallDisplayIcon={app}\bin\590Ctrl.exe
UninstallDisplayName=590Ctrl
OutputDir=..\dist
OutputBaseFilename=590Ctrl-{#AppVersion}-win-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Close a running 590Ctrl before replacing files.
CloseApplications=force
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[InstallDelete]
; An upgrade starts from a clean program folder, so no file from an older
; version is left behind. Settings aren't in here.
Type: filesandordirs; Name: "{app}\*"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\590Ctrl"; Filename: "{app}\bin\590Ctrl.exe"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\590Ctrl"; Filename: "{app}\bin\590Ctrl.exe"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\590Ctrl.exe"; Description: "{cm:LaunchProgram,590Ctrl}"; WorkingDir: "{app}\bin"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; A running 590Ctrl would keep its files locked.
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM 590Ctrl.exe"; Flags: runhidden; RunOnceId: "StopApp"
