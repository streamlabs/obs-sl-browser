; Check if PACKAGE_DIR is defined, if not throw an error
!ifdef PACKAGE_DIR
    !echo "Packaging directory: ${PACKAGE_DIR}"
!else
    !error "PACKAGE_DIR not defined. Please define PACKAGE_DIR before compiling."
!endif

; Check if OUTPUT_NAME is defined, if not throw an error
!ifdef OUTPUT_NAME
    !echo "Output name: ${OUTPUT_NAME}"
!else
    !error "OUTPUT_NAME not defined. Please define OUTPUT_NAME before compiling."
!endif

; Define the name of the installer as it will appear in Windows
Name "Streamlabs Plugin Package"

; Specify the output installer file using the OUTPUT_NAME parameter
Outfile "${OUTPUT_NAME}"
RequestExecutionLevel admin  ; Request admin rights

; Use Modern UI for the installer interface
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

; Set the installation branding
!define MUI_ICON "streamlabs.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "streamlabs.bmp"

; Define the pages of the installer
!define MUI_PAGE_HEADER_TEXT "Choose Install Location"
!define MUI_PAGE_HEADER_SUBTEXT "Specify the location of your OBS plugins folder"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
BrandingText "Streamlabs Plugin Package" 
!insertmacro MUI_PAGE_FINISH

; Define the sections of the installer
Section "MainSection" SEC01    
   ; Read previous installation directory from the registry and delete plugin
   ReadRegStr $R0 HKLM "Software\Streamlabs OBS Plugin" "InstallDir"
   StrCmp $R0 "" doneDelete   
   Delete "$R0\sl-browser-plugin.dll"
   doneDelete:

   SetOutPath $INSTDIR
   File /r "${PACKAGE_DIR}\*.*"

   ; Write the installation path to the registry
   WriteRegStr HKLM "Software\Streamlabs OBS Plugin" "InstallDir" $INSTDIR

   ; Write the uninstaller
   WriteUninstaller "$INSTDIR\Uninstall.exe"
   
   ; Write the installation path to the registry for uninstall purposes
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "DisplayName" "Streamlabs Plugin Package"
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "UninstallString" "$INSTDIR\Uninstall.exe"
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "DisplayIcon" "$INSTDIR\Uninstall.exe"
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "Publisher" "Streamlabs"
   WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "NoModify" 1
   WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "NoRepair" 1
SectionEnd

; Language files (choose the languages you want to support)
!insertmacro MUI_LANGUAGE "English"

; Define uninstaller section
Section "Uninstall"
   ; Delete plugin
   Delete "$INSTDIR\sl-browser-plugin.dll"
   
   ; Check if the file is deleted
   IfFileExists "$INSTDIR\sl-browser-plugin.dll" 0 +2
   MessageBox MB_ICONERROR "Error: Failed to delete plugin file. Make sure OBS is not running."
   Abort
   
   ; Read the installation directory from the registry
   ReadRegStr $INSTDIR HKLM "Software\Streamlabs OBS Plugin" "InstallDir"

   ; Remove uninstaller itself
   Delete "$INSTDIR\Uninstall.exe"

   ; Clean up the registry entry
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg"
SectionEnd
