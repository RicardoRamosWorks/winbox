@echo off
cls
SET CDNUM=1

:CHECKCD
SET CDROM=
FINDCD.EXE %CDNUM% > NUL
if %CDROM%#==# GOTO TRYNEXTCD
GOTO checkcd2

:TRYNEXTCD
if %cdnum%#==4# goto NOCDROM
if %cdnum%#==3# set CDNUM=4
if %cdnum%#==2# set CDNUM=3
if %cdnum%#==1# set CDNUM=2
GOTO CHECKCD

:checkcd2
dready %CDROM% > nul
if errorlevel 2 goto cont
goto checkvalidcd

:checkvalidcd
if not exist %CDROM%\WFW311\*.* goto cont
inifile %ins_path%\win.ini [winsetup] source_disk_path=%CDROM%\WFW311 > NUL

:cont
change /i C:\AUTOEXEC.BAT "%ins_path%\SETEXEC.BAT" "REM" > NUL
change /i C:\AUTOEXEC.BAT "set ins_path=%ins_path%" "REM" > NUL
inifile C:\MSDOS.SYS [Options] BootGUI=1 > NUL
attr +s +h +r C:\MSDOS.SYS > NUL
cd C:\WINDOWS 
call system\dosx.exe /b /q 
