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
!ifdef OBS_CORE_LAYOUT
!define MUI_PAGE_HEADER_SUBTEXT "Specify the location of your OBS installation folder"
InstallDir "$PROGRAMFILES64\obs-studio"
!else
!define MUI_PAGE_HEADER_SUBTEXT "Specify the location of your OBS plugins folder"
!endif
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
BrandingText "Streamlabs Plugin Package" 
!insertmacro MUI_PAGE_FINISH

; Define the sections of the installer
Section "MainSection" SEC01    
!ifdef OBS_CORE_LAYOUT
   IfFileExists "$INSTDIR\bin\64bit\obs64.exe" obsRootValid
   MessageBox MB_OK|MB_ICONEXCLAMATION "Select the OBS installation folder containing bin\64bit\obs64.exe."
   Abort
obsRootValid:
   IfFileExists "$INSTDIR\core\obs-browser\libcef.dll" cefValid
   MessageBox MB_OK|MB_ICONEXCLAMATION "The selected OBS installation is missing core\obs-browser\libcef.dll."
   Abort
cefValid:
!endif
   ; Remove the previous installation only after the destination is validated.
   ReadRegStr $R0 HKLM "Software\Streamlabs OBS Plugin" "InstallDir"
   StrCmp $R0 "" doneDelete
   Delete "$R0\sl-browser-plugin.dll"
   Delete "$R0\obs-plugins\64bit\sl-browser-plugin.dll"
   IfFileExists "$R0\sl-browser-plugin.dll" oldInstallInUse
   IfFileExists "$R0\obs-plugins\64bit\sl-browser-plugin.dll" oldInstallInUse
   Delete "$R0\sl-browser.exe"
   Delete "$R0\sl-browser-page.exe"
   Delete "$R0\streamlabs-app-icon.png"
   Delete "$R0\core\obs-browser\sl-browser.exe"
   Delete "$R0\core\obs-browser\sl-browser-page.exe"
   Delete "$R0\core\obs-browser\streamlabs-app-icon.png"
   IfFileExists "$R0\sl-browser.exe" oldInstallInUse
   IfFileExists "$R0\sl-browser-page.exe" oldInstallInUse
   IfFileExists "$R0\streamlabs-app-icon.png" oldInstallInUse
   IfFileExists "$R0\core\obs-browser\sl-browser.exe" oldInstallInUse
   IfFileExists "$R0\core\obs-browser\sl-browser-page.exe" oldInstallInUse
   IfFileExists "$R0\core\obs-browser\streamlabs-app-icon.png" oldInstallInUse
   ; A root-level Uninstall.exe may belong to OBS itself. Only remove the
   ; generic name from an older installation in a plugins directory.
   IfFileExists "$R0\bin\64bit\obs64.exe" skipLegacyUninstaller
   Delete "$R0\Uninstall.exe"
   IfFileExists "$R0\Uninstall.exe" oldInstallInUse
skipLegacyUninstaller:
   Delete "$R0\Uninstall-sl-browser-plugin.exe"
   IfFileExists "$R0\Uninstall-sl-browser-plugin.exe" oldInstallInUse
   Goto doneDelete
oldInstallInUse:
   MessageBox MB_OK|MB_ICONEXCLAMATION "Close OBS and the previous Streamlabs Plugin uninstaller, then retry."
   Abort
doneDelete:

   ClearErrors
!ifdef OBS_CORE_LAYOUT
   ; OBS 33 discovers external plugins in the legacy obs-plugins path, while
   ; its bundled CEF runtime is under core\obs-browser.
   SetOutPath "$INSTDIR\obs-plugins\64bit"
   File "${PACKAGE_DIR}\sl-browser-plugin.dll"
   SetOutPath "$INSTDIR\core\obs-browser"
   File "${PACKAGE_DIR}\sl-browser.exe"
   File "${PACKAGE_DIR}\sl-browser-page.exe"
   File "${PACKAGE_DIR}\streamlabs-app-icon.png"
!else
   SetOutPath $INSTDIR
   File "${PACKAGE_DIR}\sl-browser-plugin.dll"
   File "${PACKAGE_DIR}\sl-browser.exe"
   File "${PACKAGE_DIR}\sl-browser-page.exe"
   File "${PACKAGE_DIR}\streamlabs-app-icon.png"
!endif
   IfErrors onError noError

onError:   
    SetErrorLevel 1
    Abort
	
noError:
   ; Write the uninstaller
   ClearErrors
   WriteUninstaller "$INSTDIR\Uninstall-sl-browser-plugin.exe"
   IfErrors onError

   ; Write the installation path to the registry
   WriteRegStr HKLM "Software\Streamlabs OBS Plugin" "InstallDir" $INSTDIR
   
   ; Write the installation path to the registry for uninstall purposes
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "DisplayName" "Streamlabs Plugin Package"
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "UninstallString" "$INSTDIR\Uninstall-sl-browser-plugin.exe"
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "DisplayIcon" "$INSTDIR\Uninstall-sl-browser-plugin.exe"
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "Publisher" "Streamlabs"
   WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "NoModify" 1
   WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg" "NoRepair" 1
SectionEnd

; Language files (choose the languages you want to support)
!insertmacro MUI_LANGUAGE "English"

; Define uninstaller section
Section "Uninstall"
   ; Read the installation directory from the registry
   ReadRegStr $INSTDIR HKLM "Software\Streamlabs OBS Plugin" "InstallDir"
   StrCmp $INSTDIR "" uninstDone
   
   ; Delete plugin
!ifdef OBS_CORE_LAYOUT
   Delete "$INSTDIR\obs-plugins\64bit\sl-browser-plugin.dll"
   Delete "$INSTDIR\core\obs-browser\sl-browser.exe"
   Delete "$INSTDIR\core\obs-browser\sl-browser-page.exe"
   Delete "$INSTDIR\core\obs-browser\streamlabs-app-icon.png"
   IfFileExists "$INSTDIR\obs-plugins\64bit\sl-browser-plugin.dll" file_exists file_not_exists
!else
   Delete "$INSTDIR\sl-browser-plugin.dll"
   Delete "$INSTDIR\sl-browser.exe"
   Delete "$INSTDIR\sl-browser-page.exe"
   Delete "$INSTDIR\streamlabs-app-icon.png"
   
   ; Check if the file is deleted
   IfFileExists "$INSTDIR\sl-browser-plugin.dll" file_exists file_not_exists
!endif
file_exists:
    MessageBox MB_OK|MB_ICONEXCLAMATION "Failed to delete a plugin file. Close OBS and retry."
    Abort
file_not_exists:
!ifdef OBS_CORE_LAYOUT
   IfFileExists "$INSTDIR\core\obs-browser\sl-browser.exe" file_exists
   IfFileExists "$INSTDIR\core\obs-browser\sl-browser-page.exe" file_exists
   IfFileExists "$INSTDIR\core\obs-browser\streamlabs-app-icon.png" file_exists
!else
   IfFileExists "$INSTDIR\sl-browser.exe" file_exists
   IfFileExists "$INSTDIR\sl-browser-page.exe" file_exists
   IfFileExists "$INSTDIR\streamlabs-app-icon.png" file_exists
!endif
   
   ; Remove uninstaller itself
   Delete "$INSTDIR\Uninstall-sl-browser-plugin.exe"
   IfFileExists "$INSTDIR\Uninstall-sl-browser-plugin.exe" file_exists

   ; Clean up the registry entry
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SLPluginPkg"
   DeleteRegKey HKLM "Software\Streamlabs OBS Plugin"
uninstDone:
SectionEnd
