@echo off
cls
if %1#==1# goto fdisk
CD D:\WFW311\DOS
suscr.exe /D "Windows for Workgroups 3.11 Setup" /M "Copying files needed for Windows setup..." /F "Copying..."
set tmpdest=X:
set xmss=2880
xmsdsk.exe %tmpdest% %xmss% /y > nul
dready.com %tmpdest% /w
if errorlevel 1 goto noramdisk
goto presetup

:noramdisk
suscr.exe /M "Creating a temporary folder for Setup files..."
dready.com c: /w
if errorlevel 1 goto notmpdir
set tmpdest=c:\WININST0.400
if exist %tmpdest% attr.com -s -h -r %tmpdest% > nul
if exist %tmpdest% del %tmpdest%
if not exist %tmpdest%\nul md %tmpdest% > nul
if exist %tmpdest%\nul goto presetup

:notmpdir
suscr.exe /M "Read Only Filesystem..." /F "Waiting..."
set tmpdest=
echo.
echo  Setup Cannot create a RAM drive or Temporary Folder
echo  for the Windows for Workgroups 3.11 Installation.
echo.
echo  Setup cannot Continue.
pause
goto exit

:presetup
suscr.exe /M "Copying files needed for Windows Setup...."
copy COMMAND.COM %tmpdest% >nul
copy ATTR.COM %tmpdest% >nul
copy BOOTPART.EXE %tmpdest% >nul
copy CHANGE.EXE %tmpdest% >nul
copy CPUDET.COM %tmpdest% >nul
copy DELTREE.COM %tmpdest% >nul
copy DREADY.COM %tmpdest% >nul
copy FIND.EXE %tmpdest% >nul
copy FINDCD.EXE %tmpdest% >nul
copy GDISK.EXE %tmpdest% >nul
copy INIFILE.COM %tmpdest% >nul
copy MCD.EXE %tmpdest% >nul
copy MESSAGES.EXE %tmpdest% >nul
copy SHUTDOWN.COM %tmpdest% >nul
copy D:\WFW311\SETUP.INF %tmpdest%\WINSETUP.INF >nul
copy SUSCR.EXE %tmpdest% >nul
copy W.BAT %tmpdest% >nul
copy WBAT.COM %tmpdest% >nul
copy XMSDSK.EXE %tmpdest% >nul
copy XFIND.COM %tmpdest% >nul
suscr.exe /F "Done..."
if %COMSPEC%#==A:\COMMAND.COM# set COMSPEC=%tmpdest%\COMMAND.COM
set TEMP=%tmpdest%\
set TMP=%tmpdest%\
set windir=c:\WINDOWS
set dosdir=c:\DOS
%tmpdest%
format.bat 1
goto exit

:fdisk
suscr.exe  /M "Examining Fixed Disks..." /F "Working..."
gdisk.exe 1|find.exe "No such fixed disk" > nul
if not errorlevel 1 goto nohd
gdisk.exe 1|find.exe "No partitions defined" > nul
if not errorlevel 1 goto partitionhdd
gdisk.exe 1|\find.exe " PRI DOS"|find.exe "DOS"|find.exe "FAT" > nul
if errorlevel 1 goto repartitionhdd
gdisk.exe 1|find.exe " PRI DOS"|find.exe "C:"|find.exe "Unformatted"|find.exe "FAT" > nul
if not errorlevel 1 goto formathd
goto checkpartition

:formathd
suscr.exe suscr.exe /D "Windows for Workgroups 3.11 Setup" /M "Formatting Drive C:\..." /F "working..."
echo y | format.com c: /V:WINDOWS
goto checkpartition

:partitionhdd
suscr.exe /M "Partitioning Hard Drive for Windows..." /F "working..."
gdisk.exe 1 /cre /pri /for /q /v:WINDOWS
suscr.exe /D "Windows for Workgroups 3.11 Setup " /M "Setup will now restart you computer..." /F "Waiting..."
echo.
echo  Partitining Complete.
echo  Setup needs to restart your computer.
echo.
echo  Please keep this disk in your drive.
echo.
pause
shutdown.com r > nul

:repartitionhdd
suscr.exe /M "NON-DOS partition found..." /F "Exiting..."
echo.
echo  Setup has found a NON-DOS partition on your Hard Drive!
echo.
echo  Windows for Workgroups 3.11 cannot be installed without
echo  a Pimary DOS Partition. 
echo.
echo  Setup will now exit.
echo.
pause
goto exit

:checkpartition
suscr.exe /D "Windows for Workgroups 3.11 Setup" /M "Checking Primary DOS Partition..." /F "C:\"
gdisk.exe 1|find.exe "H    PRI DOS"|find.exe "FAT" > nul
if not errorlevel 1 goto hiddenpart
goto activepart

:hiddenpart
suscr.exe /M "Found Hidden Primary DOS Partition..." /F "C:\"
echo.
echo  At least one Primary DOS Partition is probably hidden!
echo.
echo  A Hidden Primary FAT Partition may not be seen by Windows,
echo  so it may or may not cause some problems if you continue.
echo.
echo  If it does cause problems soon or later, please unhide the
echo  Primary FAT Partition manually first, and then try again.
echo.
pause
goto activepart

:activepart
suscr.exe /D "Windows for Workgroups 3.11 Setup" /M "Checking Primary DOS Partition...." /F "C:\"
gdisk.exe 1|find.exe "      PRI DOS"|find.exe "C:"|find.exe "FAT" > nul
if not errorlevel 1 goto makeactive
goto checkhd

:makeactive
suscr.exe /M "Inactive Partition Found...." /F "C:\"
echo.
echo  Primary DOS partition is not active!
echo.
echo  Setup will now mark the partition as active
echo.
gdisk.exe 1 /act /p:1 > nul
goto checkhd

:checkhd
suscr.exe /D "Windows for Workgroups 3.11 Setup" /M "Checking Primary DOS Partition...." /F "C:\"
dready.com c: /w > nul
if errorlevel 1 goto nohd
goto mbr

:nohd
suscr.exe /M "NON-DOS partition found..." /F "Exiting..."
echo.
echo  Setup Cannot Configure your Hard Drive!
echo.
echo  There was an unknown error with your Hard Drive.
echo  Setup cannot continue.
echo.
echo Setup will now exit.
echo.
pause
goto exit

:reboot
x:\shutdown.com r > nul
goto exit

:mbr
suscr.exe /D "Windows for Workgroups 3.11 Setup" /M "Setup is processing MBR..." /F "C:\"
gdisk.exe 1 /mbr > nul

:copysystem
suscr.exe /D "Windows for Workgroups 3.11 Setup" /M "Making Drive Bootable..." /F "C:\"
attr.com -s -h -r c:\IO.SYS > nul
copy A:\IO.SYS c:\IO.SYS /Y > nul
attr.com +s +h +r C:\IO.sys > nul
attr.com -s -h -r c:\msdos.sys > nul
copy D:\WFW311\DOS\MSDOS.SYS c:\MSDOS.SYS /Y > nul
inifile C:\MSDOS.SYS [Paths] WinDir=%windir% > nul
inifile C:\MSDOS.SYS [Paths] WinBootDir=%windir% > nul
attr.com +s +h +r c:\msdos.sys > nul
attr.com -s -h -r c:\LOGO.SYS > nul
copy D:\WFW311\LOGO.SY_ C:\LOGO.SYS >nul
attr.com +s +h +r c:\LOGO.SYS > nul
copy D:\WFW311\DOS\COMMAND.COM c:\COMMAND.COM /Y > nul
suscr.exe /M "Creating Directories..." /F %windir%
deltree /y %windir% > nul
md %windir% > nul
suscr.exe  /M "Creating Directories..." /F %dosdir%
deltree /y %dosdir% > nul
md %dosdir% > nul
suscr.exe  /M "Copying MS-DOS Files..." /F "Copying..."
copy D:\WFW311\DOS\*.* %dosdir% /Y > nul
suscr.exe  /M "Configuring System..." /F "MS-DOS"
copy D:\WFW311\DOS\CONFIG.SYS c:\CONFIG.SYS /Y > nul
copy D:\WFW311\DOS\AUTOEXEC.BAT c:\AUTOEXEC.BAT /Y > nul
echo %windir%\SETEXEC.BAT >> C:\AUTOEXEC.BAT
copy D:\WFW311\DOS\SETEXC.BA_ %windir%\SETEXEC.BAT /Y > nul
copy D:\WFW311\SETUP.INF %windir%\WINSETUP.INF >nul
echo y | lock c: > nul

:setupSHH
echo [sysinfo] > %windir%\setup.shh
echo showsysinfo=no >> %windir%\setup.shh
echo [configuration] >> %windir%\setup.shh
echo machine = ibm_compatible >> %windir%\setup.shh
echo display = vga >> %windir%\setup.shh
echo mouse = ps2mouse >> %windir%\setup.shh
echo keyboard = t4s0enha >> %windir%\setup.shh
echo language = enu >> %windir%\setup.shh
echo kblayout = nodll >> %windir%\setup.shh
echo [windir] >> %windir%\setup.shh
echo %windir% >> %windir%\setup.shh
echo [options] >> %windir%\setup.shh
echo setupapps >> %windir%\setup.shh
echo [endinstall] >> %windir%\setup.shh
echo configfiles = save >> %windir%\setup.shh
echo endopt = reboot >> %windir%\setup.shh

copy D:\WFW311\SETUP.INF %windir%\WINSETUP.INF >nul

echo D:\WFW311\setup.exe /H:%windir%\setup.shh /O:%windir%\WINSETUP.INF >> %windir%\SETEXEC.BAT
suscr.exe /D "Windows for Workgroups 3.11 Setup " /M "System Pre-Configuration Completed..." /F "Waiting..."
echo.
echo  Setup has finished the Pre-Configuration of your system.
echo.
echo        Setup will now reboot your computer.
echo  Please do not remove this disk from your computer.
echo.
Pause
goto reboot

:exit
cls
