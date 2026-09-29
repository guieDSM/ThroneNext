Unicode true
Name "Throne Next"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\ThroneNext"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!define MUI_ICON "${SOURCE_DIR}\res\Throne.ico"
!define MUI_UNICON "${SOURCE_DIR}\res\ThroneDel.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\Throne.exe"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetShellVarContext current
  !include "${INSTALL_FILES}"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\Throne Next.lnk" "$INSTDIR\Throne.exe" "" "$INSTDIR\Throne.exe" 0
  CreateShortcut "$SMPROGRAMS\Throne Next.lnk" "$INSTDIR\Throne.exe" "" "$INSTDIR\Throne.exe" 0
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThroneNext" "DisplayName" "Throne Next"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThroneNext" "DisplayIcon" "$INSTDIR\Throne.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThroneNext" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThroneNext" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThroneNext" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$DESKTOP\Throne Next.lnk"
  Delete "$SMPROGRAMS\Throne Next.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThroneNext"
  !include "${UNINSTALL_FILES}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  ; The config directory remains in place so uninstalling does not erase profiles.
SectionEnd
