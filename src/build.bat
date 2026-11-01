
set "DIR=%~dp0"
set "DIR=%DIR:~0,-1%"
gcc.exe -g main.c  renderer.c -o ..\game.exe -fdiagnostics-color=always -lgdi32 -mwindows -std=c99 -lwinmm -O0