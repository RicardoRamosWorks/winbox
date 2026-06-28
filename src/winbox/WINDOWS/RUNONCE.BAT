@echo off
inifile %winbootdir%\win.ini [windows] run=ALL3D.EXE DOSBAR.EXE OSFRAMER.EXE
inifile %winbootdir%\win.ini [winsetup] source_disk_path=%winbootdir%\WINSETUP
