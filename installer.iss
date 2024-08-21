; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!

#define MyAppName "Openterface Mini-KVM"
#define MyAppExeName "openterfaceQT.exe"

#pragma message "AppVersion: " + MyAppVersion
#pragma message "MyAppPublisher: " + MyAppPublisher
#pragma message "MyAppURL: " + MyAppURL

[Setup]
; NOTE: The value of AppId uniquely identifies this application. Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{4200459F-8F27-46C4-BFB2-65C1447AE912}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
;AppVerName={#MyAppName} {#MyAppVersion}
VersionInfoProductTextVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
VersionInfoDescription={#MyAppName} {#MyAppVersion} Installer
VersionInfoCompany={#MyAppPublisher}
; Uncomment the following line to run in non administrative install mode (install for current user only.)
;PrivilegesRequired=lowest
OutputBaseFilename=openterface-installer
Compression=lzma
SolidCompression=yes
WizardStyle=modern


[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "package\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "package\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "package\driver\CH341SER.INF"; DestDir: {app}\driver;
Source: "package\driver\CH341SER.SYS"; DestDir: {app}\driver;

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; \
    Flags: nowait postinstall skipifsilent; \
    Parameters: "/silent"
Filename: {sys}\rundll32.exe; \
    Parameters: "setupapi,InstallHinfSection DefaultInstall 128 {app}\driver\CH341SER.inf"; \
    WorkingDir: {app}\driver; Flags: 32bit runhidden;