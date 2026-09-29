; OpenTV as SAPI 5 voices -- installer.
;
; Build it with:  makensis sapi5/installer.nsi
; from the top of the repository, after sh harness/build.sh has produced
; build/bin/tvsapi.dll and build/bin/tvsapi64.dll.
;
; Both word widths are installed, because SAPI voices are not shared between
; them: a 32-bit application can only load a 32-bit voice and a 64-bit one only
; a 64-bit voice, and each registers under its own view of the registry.  A
; machine with only the 64-bit voice installed shows nothing to the 32-bit
; readers, which is the usual way this goes wrong.
;
; Registration is the DLL's own: regsvr32 calls DllRegisterServer, which writes
; the class and one token per voice.  Uninstalling calls DllUnregisterServer,
; which takes out exactly what it wrote.

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!define PRODUCT    "OpenTV"
!define PUBLISHER  "OpenTV"
!define VERSION    "1.0"
!define REGKEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenTV"

Name "${PRODUCT} (SAPI 5 voices)"
OutFile "..\build\bin\OpenTV-SAPI5-Setup.exe"
Unicode true
RequestExecutionLevel admin          ; the voices live in HKEY_LOCAL_MACHINE
InstallDir "$PROGRAMFILES64\OpenTV"
InstallDirRegKey HKLM "${REGKEY}" "InstallLocation"
ShowInstDetails show
ShowUnInstDetails show

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; --- calling regsvr32, at both word widths -----------------------------------
;
; A 32-bit process reaching $SYSDIR is redirected to SysWOW64, so the 64-bit
; regsvr32 has to be run with redirection turned off, and the 32-bit one is
; named outright.  Getting this backwards registers one DLL twice and the other
; not at all -- which reports success and leaves half the voices missing.
;
; The same body serves registering and unregistering, and the installer and the
; uninstaller; only the /u and NSIS's required "un." prefix differ.

!macro RegFunc NAME PREFIX FLAG
Function ${PREFIX}${NAME}
  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
    ExecWait '"$SYSDIR\regsvr32.exe" /s ${FLAG}"$INSTDIR\tvsapi64.dll"'
    ${EnableX64FSRedirection}
    ExecWait '"$WINDIR\SysWOW64\regsvr32.exe" /s ${FLAG}"$INSTDIR\tvsapi.dll"'
  ${Else}
    ExecWait '"$SYSDIR\regsvr32.exe" /s ${FLAG}"$INSTDIR\tvsapi.dll"'
  ${EndIf}
FunctionEnd
!macroend

!insertmacro RegFunc RegisterBoth   ""    ""
!insertmacro RegFunc UnregisterBoth ""    "/u "
!insertmacro RegFunc UnregisterBoth "un." "/u "

Section "OpenTV SAPI 5 voices" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Installing over a registered copy: take the old registrations out first, so
  ; nothing is left pointing at a DLL that is about to be replaced.  Harmless
  ; when there is nothing there to unregister.
  IfFileExists "$INSTDIR\tvsapi64.dll" 0 +2
    Call UnregisterBoth

  ; A SAPI application that is still speaking holds the DLL open and the file
  ; cannot be replaced while it does.  Say so plainly rather than registering a
  ; new token against an old binary.
  ClearErrors
  SetOverwrite try
  File "/oname=tvsapi64.dll" "..\build\bin\tvsapi64.dll"
  File "/oname=tvsapi.dll"   "..\build\bin\tvsapi.dll"
  IfErrors 0 +4
    DetailPrint "Could not replace the voice files."
    MessageBox MB_OK|MB_ICONSTOP "A program is using the OpenTV voices.$\n$\nClose anything that speaks -- a screen reader, a book reader, the Speech control panel -- and run this installer again.  Nothing has been changed."
    Abort

  SetOverwrite on
  File "/oname=LICENSE.txt" "..\LICENSE"
  File "/oname=NOTICE.txt"  "..\NOTICE"

  Call RegisterBoth

  WriteRegStr HKLM "${REGKEY}" "DisplayName"     "${PRODUCT}"
  WriteRegStr HKLM "${REGKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKLM "${REGKEY}" "Publisher"       "${PUBLISHER}"
  WriteRegStr HKLM "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${REGKEY}" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  Call un.UnregisterBoth

  Delete /REBOOTOK "$INSTDIR\tvsapi64.dll"
  Delete /REBOOTOK "$INSTDIR\tvsapi.dll"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\NOTICE.txt"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "${REGKEY}"
SectionEnd
