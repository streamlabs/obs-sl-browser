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
Name "Streamlabs Plugin for OBS"

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
BrandingText "Streamlabs Plugin for OBS" 
!insertmacro MUI_PAGE_FINISH

Var installDir

; Call the function to stop and remove the service before the installation
Section "PreInstall" SEC00
    
    ; Get InstallDir from cmd line
    ${GetOptions} $CMDLINE "/p" $installDir

    ; Check if $installDir has a value, if so, set it as $INSTDIR
    StrCmp $installDir "" skip
    StrCpy $INSTDIR $installDir
    skip:
    
SectionEnd

; Define the sections of the installer
Section "MainSection" SEC01    

   ; Set the output path to the directory selected by the user or provided via command line
   SetOutPath $INSTDIR

   ; Include the files from PACKAGE_DIR directory recursively
   File /r "${PACKAGE_DIR}\*.*"

SectionEnd

; Language files (choose the languages you want to support)
!insertmacro MUI_LANGUAGE "English"
