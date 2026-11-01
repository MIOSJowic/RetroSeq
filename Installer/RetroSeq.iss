; RetroSeq installer (Inno Setup 6.3 or later)
; Build with build-installer.cmd, which finds the Release build, the version and ISCC.exe.

#ifndef AppVersion
  #define AppVersion "0.21"
#endif
#ifndef ExePath
  #define ExePath "..\out\build\x64-Release\RetroSeq_artefacts\Release\RetroSeq.exe"
#endif

[Setup]
AppId={{8F3C2A51-6B7D-4E2F-9A1C-5D3E7B9F0A21}
AppName=RetroSeq
AppVersion={#AppVersion}
AppVerName=RetroSeq {#AppVersion}
AppPublisher=Michael Östman
AppComments=Keyboard-driven MIDI sequencer and VST3 host in the style of Sequencer Plus Gold
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\RetroSeq
DefaultGroupName=RetroSeq
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=Output
OutputBaseFilename=RetroSeq-{#AppVersion}-Setup
SetupIconFile=RetroSeq.ico
UninstallDisplayIcon={app}\RetroSeq.exe
UninstallDisplayName=RetroSeq {#AppVersion}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
ChangesAssociations=yes
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assocmid"; Description: "Add RetroSeq to ""Open with"" for .mid files"; GroupDescription: "File types:"

[Files]
Source: "{#ExePath}"; DestDir: "{app}"; Flags: ignoreversion
Source: "Manual\RetroSeq Reference Manual.pdf"; DestDir: "{app}\Manual"; Flags: ignoreversion
Source: "Manual\Kosmische Manual.pdf"; DestDir: "{app}\Manual"; Flags: ignoreversion
Source: "Examples\*"; DestDir: "{app}\Examples"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\RetroSeq"; Filename: "{app}\RetroSeq.exe"
Name: "{group}\RetroSeq Reference Manual"; Filename: "{app}\Manual\RetroSeq Reference Manual.pdf"
Name: "{group}\Kosmische Manual"; Filename: "{app}\Manual\Kosmische Manual.pdf"
Name: "{group}\Example Songs"; Filename: "{app}\Examples"
Name: "{group}\Uninstall RetroSeq"; Filename: "{uninstallexe}"
Name: "{autodesktop}\RetroSeq"; Filename: "{app}\RetroSeq.exe"; Tasks: desktopicon

[Registry]
; "Open with" for .mid files. Windows 10/11 let only the user choose the default program.
Root: HKA; Subkey: "Software\Classes\RetroSeq.MidiFile"; ValueType: string; ValueName: ""; ValueData: "MIDI song (RetroSeq)"; Flags: uninsdeletekey; Tasks: assocmid
Root: HKA; Subkey: "Software\Classes\RetroSeq.MidiFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\RetroSeq.exe,0"; Tasks: assocmid
Root: HKA; Subkey: "Software\Classes\RetroSeq.MidiFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\RetroSeq.exe"" ""%1"""; Tasks: assocmid
Root: HKA; Subkey: "Software\Classes\.mid\OpenWithProgids"; ValueType: string; ValueName: "RetroSeq.MidiFile"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocmid
Root: HKA; Subkey: "Software\Classes\Applications\RetroSeq.exe"; Flags: uninsdeletekey; Tasks: assocmid
Root: HKA; Subkey: "Software\Classes\Applications\RetroSeq.exe\SupportedTypes"; ValueType: string; ValueName: ".mid"; ValueData: ""; Tasks: assocmid

[Run]
Filename: "{app}\RetroSeq.exe"; Description: "{cm:LaunchProgram,RetroSeq}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\Manual\RetroSeq Reference Manual.pdf"; Description: "Open the Reference Manual"; Flags: shellexec nowait postinstall skipifsilent unchecked
