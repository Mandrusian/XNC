[Setup]
AppId={{8D6B4A2E-1B89-4F0F-9D6A-6B7F4B7E9121}
AppName=XNC
AppVersion=0.2.0
AppPublisher=Mandrusian
AppPublisherURL=https://github.com/Mandrusian/XNC
DefaultDirName={localappdata}\Programs\XNC
DefaultGroupName=XNC
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=dist
OutputBaseFilename=XNC-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\xnc.exe

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "build\Release\xnc.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\shared\*"; DestDir: "{app}\shared"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\XNC"; Filename: "{app}\xnc.exe"
Name: "{autodesktop}\XNC"; Filename: "{app}\xnc.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\xnc.exe"; Description: "Launch XNC"; Flags: nowait postinstall skipifsilent
