; NSIS script for Openterface Mini-KVM
; Generated based on the provided Inno Setup script

!define MyAppName "Openterface Mini-KVM For Window"
!define LicenseFile "LICENSE"
!define SourcePackage "."

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
!insertmacro MUI_PAGE_LICENSE "${SourcePackage}\${LicenseFile}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Icon "${SourcePackage}\images\icon_128.ico"

Section "Install"
    ; Create installation directory
    SetOutPath "$INSTDIR"

    ; Copy application files
    File /r "${WorkingDir}\*"

    ; Create shortcuts
    CreateShortcut "$SMPROGRAMS\${MyAppName}.lnk" "$INSTDIR\${MyAppExeName}"
    CreateShortcut "$DESKTOP\${MyAppName}.lnk" "$INSTDIR\${MyAppExeName}"
    
    ; Run the application after installation
    Exec "$INSTDIR\${MyAppExeName} /silent"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
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