@echo off
set "DIR=%~dp0"
set "DIR=%DIR:~0,-1%"
gcc "%DIR%\mapcreate.c" -o "%DIR%\mapcreate.exe"
%DIR%\mapcreate.exe
move "%DIR%\*.gmap" "%DIR%\..\..\assets\" 
del %DIR%\mapcreate.exe