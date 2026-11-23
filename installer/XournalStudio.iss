; Per-user installer of Xournal Studio (Win32) (Inno Setup 6, see Build-Installer.ps1). No admin rights, no questions beyond
; (one page: "Installieren", then progress; Setup ignores DisableReadyPage when no page was shown before). Installs the staged app (install.ps1 -SkipInstall: XournalStudio.exe, pdfium.dll, licenses\,
; THIRD-PARTY.md, README.txt) to %LOCALAPPDATA%\Programs\Xournal Studio (Win32) - the folder install.ps1 uses - plus
; the Start menu entry "Xournal Studio (Win32)" and the Apps & Features entry (icon, version, publisher).
; Uninstall removes exactly what was installed; settings, recovery copies and documents are never touched.
; AppId never changes: a setup of the same or a newer version installs over the installed one.
#ifndef Version
  #error Pass /DVersion=x.y.z
#endif
#ifndef Stage
  #error Pass /DStage=<folder with the staged app>
#endif
#ifndef AppIcon
  #error Pass /DAppIcon=<app.ico>
#endif

[Setup]
AppId={{B3C4C7F5-6B0A-4D2E-9A53-5E0F7C6D1A42}
AppName=Xournal Studio (Win32)
AppVersion={#Version}
AppPublisher=Robert Kleinschuster
VersionInfoVersion={#Version}
DefaultDirName={localappdata}\Programs\Xournal Studio (Win32)
UsePreviousAppDir=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableFinishedPage=yes
WizardStyle=modern
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\XournalStudio.exe
UninstallDisplayName=Xournal Studio (Win32)
CloseApplications=yes
RestartApplications=no
Compression=lzma2
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{userprograms}\Xournal Studio (Win32)"; Filename: "{app}\XournalStudio.exe"
