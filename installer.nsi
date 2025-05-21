; NSIS script for Openterface Mini-KVM
; Generated based on the provided Inno Setup script

!define MyAppName "Openterface Mini-KVM"
!define MyAppExeName "openterfaceQT.exe"
!define MyAppVersion "1.0.0" ; Replace with your version
!define MyAppPublisher "TechxArtisan Limited"
!define MyAppURL "https://openterface.com"
!define LicenseFile "LICENSE"
!define OutputDir "Output"
!define OutputBaseFileName "openterfaceQT.windows.amd64.installer"
!define SourcePackage "package_online"

; Set output file
OutFile "${OutputDir}\${OutputBaseFileName}.exe"

; Installer settings
Name "${MyAppName} ${MyAppVersion}"
InstallDir "$PROGRAMFILES\${MyAppName}"
ShowInstDetails show
ShowUninstDetails show

; Include license
LicenseData "${SourcePackage}\${LicenseFile}"

; Modern UI settings
!include "MUI2.nsh"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    ; Create installation directory
    SetOutPath "$INSTDIR"

    ; Copy application files
    File /r "${SourcePackage}\*"

    ; Copy driver files
    File "${SourcePackage}\driver\windows\CH341SER.INF"
    File "${SourcePackage}\driver\windows\CH341S64.SYS"

    ; Create shortcuts
    CreateShortcut "$SMPROGRAMS\${MyAppName}.lnk" "$INSTDIR\${MyAppExeName}"
    CreateShortcut "$DESKTOP\${MyAppName}.lnk" "$INSTDIR\${MyAppExeName}"

    ; Install driver silently
    ExecWait '"$SYSDIR\pnputil.exe" /add-driver "$INSTDIR\driver\CH341SER.INF" /install'

    ; Run the application after installation
    Exec "$INSTDIR\${MyAppExeName} /silent"
SectionEnd

Section "Uninstall"
    ; Remove application files
    Delete "$INSTDIR\*.*"
    RMDir /r "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\${MyAppName}.lnk"
    Delete "$DESKTOP\${MyAppName}.lnk"

    ; Uninstall driver
    ExecWait '"$SYSDIR\pnputil.exe" /delete-driver CH341SER.INF /uninstall'

    ; Remove installation directory
    RMDir /r "$INSTDIR"
SectionEnd