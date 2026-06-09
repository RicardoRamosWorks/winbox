!define VER_MAYOR 1
!define VER_MINOR 0
!define APP_NAME "NTVDBM ${VER_MAYOR}.${VER_MINOR} Installer"
!define COMP_NAME "RicardoRamosWorks.com"
!define COPYRIGHT "Copyright © 2002-2026 RicardoRamosWorks.com and DOSBox Team"
!define DESCRIPTION "NTVDBM Installer"

VIProductVersion "${VER_MAYOR}.${VER_MINOR}.0.0"
VIAddVersionKey  "ProductName"  "${APP_NAME}"
VIAddVersionKey  "CompanyName"  "${COMP_NAME}"
VIAddVersionKey  "FileDescription"  "${DESCRIPTION}"
VIAddVersionKey  "FileVersion"  "${VER_MAYOR}.${VER_MINOR}.0.0"
VIAddVersionKey  "ProductVersion"  "${VER_MAYOR}, ${VER_MINOR}, 0, 0"
VIAddVersionKey  "LegalCopyright"  "${COPYRIGHT}"

; The name of the installer
Name "${APP_NAME}"

; The file to write
OutFile "NTVDBM${VER_MAYOR}.${VER_MINOR}-win32-installer.exe"

; The default installation directory
InstallDir "$PROGRAMFILES\NTVDBM-${VER_MAYOR}.${VER_MINOR}"

; The text to prompt the user to enter a directory
DirText "This will install NTVDBM v${VER_MAYOR}.${VER_MINOR} on your computer. Choose a directory"
SetCompressor /solid lzma


LicenseData COPYING
LicenseText "NTVDBM v${VER_MAYOR}.${VER_MINOR} License" "Next >"

; Else vista enables compatibility mode
RequestExecutionLevel admin
; Shortcuts in all users

ComponentText "Select components for NTVDBM"
; The stuff to install
Section "!Core files" Core
SetShellVarContext all

  ; Set output path to the installation directory.
  ClearErrors
  SetOutPath $INSTDIR
  IfErrors error_createdir
  SectionIn RO

  ; Put file there
  
  CreateDirectory "$INSTDIR\Video Codec"
  CreateDirectory "$INSTDIR\Documentation"
  SetOutPath "$INSTDIR\Documentation"
  File /oname=README.txt README
  File /oname=COPYING.txt COPYING
  File /oname=THANKS.txt THANKS
  File /oname=NEWS.txt NEWS
  File /oname=AUTHORS.txt AUTHORS
  File /oname=INSTALL.txt INSTALL
  SetOutPath "$INSTDIR"

  File "/oname=NTVDBM ${VER_MAYOR}.${VER_MINOR} Manual.txt" README
  File "/oname=NTVDBM.exe" NTVDBM.exe
  File SDL.dll
  File SDL_net.dll
  File "/oname=Video Codec\zmbv.dll" zmbv.dll
  File "/oname=Video Codec\zmbv.inf" zmbv.inf
  File "/oname=Video Codec\Video Instructions.txt" README.video
  File "/oname=NTVDBM ${VER_MAYOR}.${VER_MINOR} Options.bat" editconf.bat
  File "/oname=Reset KeyMapper.bat" resetmapper.bat
  File "/oname=Reset Options.bat" resetconf.bat
  File "/oname=Screenshots & Recordings.bat" captures.bat
  
  CreateDirectory "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}"
  CreateDirectory "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras"
  CreateDirectory "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Video"
  CreateDirectory "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Options"
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\NTVDBM ${VER_MAYOR}.${VER_MINOR}.lnk" "$INSTDIR\NTVDBM.exe" "-userconf" "$INSTDIR\NTVDBM.exe" 0
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\NTVDBM ${VER_MAYOR}.${VER_MINOR} Manual.lnk" "$INSTDIR\Documentation\README.txt"
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\NTVDBM ${VER_MAYOR}.${VER_MINOR} (noconsole).lnk" "$INSTDIR\NTVDBM.exe" "-noconsole -userconf" "$INSTDIR\NTVDBM.exe" 0
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Screenshots & Recordings.lnk" "$INSTDIR\NTVDBM.exe" "-opencaptures explorer.exe"

  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Options\NTVDBM ${VER_MAYOR}.${VER_MINOR} Options.lnk" "$INSTDIR\NTVDBM.exe" "-editconf notepad.exe -editconf $\"%SystemRoot%\system32\notepad.exe$\" -editconf $\"%WINDIR%\notepad.exe$\""
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Options\Reset Options.lnk" "$INSTDIR\NTVDBM.exe" "-eraseconf"
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Options\Reset KeyMapper.lnk" "$INSTDIR\NTVDBM.exe" "-erasemapper"

  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Video\Video instructions.lnk" "$INSTDIR\Video Codec\Video Instructions.txt"
;change outpath so the working directory gets set to zmbv
SetOutPath "$INSTDIR\Video Codec"
  ; Shortcut creation depends on wether we are 9x of NT
  ClearErrors
  ReadRegStr $R0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" CurrentVersion
  IfErrors we_9x we_nt
we_nt:
  ;shortcut for win NT
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Video\Install movie codec.lnk" "rundll32" "setupapi,InstallHinfSection DefaultInstall 128 $INSTDIR\Video Codec\zmbv.inf"
  goto end
we_9x:
  ;shortcut for we_9x
  CreateShortCut "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Video\Install movie codec.lnk" "rundll" "setupx.dll,InstallHinfSection DefaultInstall 128 $INSTDIR\Video Codec\zmbv.inf"
end:
SetOutPath $INSTDIR
WriteUninstaller "uninstall.exe"

  goto end_section

error_createdir:
  MessageBox MB_OK "Can't create NTVDBM program directory, aborting."
  Abort
  goto end_section

end_section:
SectionEnd ; end the section

Section "Desktop Shortcut" SecDesktop
SetShellVarContext all

CreateShortCut "$DESKTOP\NTVDBM ${VER_MAYOR}.${VER_MINOR}.lnk" "$INSTDIR\NTVDBM.exe" "-userconf" "$INSTDIR\NTVDBM.exe" 0

SectionEnd ; end the section 


UninstallText "This will uninstall NTVDBM  v${VER_MAYOR}.${VER_MINOR}. Hit next to continue."

Section "Uninstall"

; Shortcuts in all users
SetShellVarContext all

  Delete "$DESKTOP\NTVDBM ${VER_MAYOR}.${VER_MINOR}.lnk"
  ; remove registry keys
  ; remove files
  Delete $INSTDIR\Documentation\README.txt
  Delete $INSTDIR\Documentation\COPYING.txt
  Delete $INSTDIR\Documentation\THANKS.txt
  Delete $INSTDIR\Documentation\NEWS.txt
  Delete $INSTDIR\Documentation\AUTHORS.txt
  Delete $INSTDIR\Documentation\INSTALL.txt
  Delete "$INSTDIR\NTVDBM ${VER_MAYOR}.${VER_MINOR} Manual.txt"
  Delete "$INSTDIR\NTVDBM.exe"
  Delete $INSTDIR\SDL.dll
  Delete $INSTDIR\SDL_net.dll
  Delete "$INSTDIR\Video Codec\zmbv.dll"
  Delete "$INSTDIR\Video Codec\zmbv.inf"
  Delete "$INSTDIR\Video Codec\Video Instructions.txt"
  ;Files left by sdl taking over the console
  Delete $INSTDIR\stdout.txt
  Delete $INSTDIR\stderr.txt
  Delete "$INSTDIR\NTVDBM ${VER_MAYOR}.${VER_MINOR} Options.bat"
  Delete "$INSTDIR\Reset KeyMapper.bat"
  Delete "$INSTDIR\Reset Options.bat"
  Delete "$INSTDIR\Screenshots & Recordings.bat"

  ; MUST REMOVE UNINSTALLER, too
  Delete $INSTDIR\uninstall.exe


  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\NTVDBM ${VER_MAYOR}.${VER_MINOR}.lnk"
  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\NTVDBM ${VER_MAYOR}.${VER_MINOR} Manual.lnk"
  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\NTVDBM ${VER_MAYOR}.${VER_MINOR} (noconsole).lnk"
  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Uninstall.lnk"
  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Screenshots & Recordings.lnk"

  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Options\NTVDBM ${VER_MAYOR}.${VER_MINOR} Options.lnk"
  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Options\Reset Options.lnk"
  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Options\Reset KeyMapper.lnk"

  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Video\Video instructions.lnk"
  Delete "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Video\Install movie codec.lnk"



; remove shortcuts, if any.
; remove directories used.
  RMDir "$INSTDIR\Documentation"
  RMDir "$INSTDIR\Video Codec"
  RMDir "$INSTDIR"
  RMDir "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Options"
  RMDir "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras\Video"
  RMDir "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}\Extras"
  RMDir "$SMPROGRAMS\NTVDBM-${VER_MAYOR}.${VER_MINOR}"
SectionEnd

; eof
