; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!

#define MyAppName "Openterface Mini-KVM"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Guangzhou Billio Tech, Inc."
#define MyAppURL "https://openterface.com/"
#define MyAppExeName "openterfaceQT.exe"
#define MyWorkingDir "{param:WorkingDir|D:\a\_temp\build\package}"

[Setup]
; NOTE: The value of AppId uniquely identifies this application. Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{4200459F-8F27-46C4-BFB2-65C1447AE912}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
;AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
; Uncomment the following line to run in non administrative install mode (install for current user only.)
;PrivilegesRequired=lowest
OutputBaseFilename=OpenterfaceInstaller
Compression=lzma
SolidCompression=yes
WizardStyle=modern


[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#MyWorkingDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyWorkingDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MyWorkingDir}\driver\CH341SER.INF"; DestDir: {app}\driver;
Source: "{#MyWorkingDir}\driver\CH341SER.SYS"; DestDir: {app}\driver;

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
Filename: {sys}\rundll32.exe; \
    Parameters: "setupapi,InstallHinfSection DefaultInstall 128 {app}\driver\CH341SER.inf"; \
    WorkingDir: {app}\driver; Flags: 32bit;
