@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set MINGW=D:\mingw64\bin
set CXX=%MINGW%\g++.exe
set RC=%MINGW%\windres.exe

if not exist build mkdir build

echo [1/2] compiling resources ...
"%RC%" -i res\app.rc -o build\app_res.o --input-format=rc --output-format=coff -I res
if errorlevel 1 goto fail

echo [2/2] compiling sources ...
"%CXX%" -std=c++17 -O2 -DUNICODE -D_UNICODE -municode -mwindows ^
  -Wall -Wextra -Wno-unused-parameter ^
  src\main.cpp src\core.cpp src\ui.cpp src\account.cpp build\app_res.o ^
  -o "每日待办.exe" ^
  -static -static-libgcc ^
  -lgdiplus -lole32 -loleaut32 -lshell32 -luser32 -lgdi32 ^
  -lcomctl32 -ladvapi32 -ldwmapi -luxtheme -lshlwapi -lmsimg32 -limm32 -lws2_32
if errorlevel 1 goto fail

echo.
echo BUILD OK
for %%F in ("每日待办.exe") do echo   output: %%~fF  (%%~zF bytes)
exit /b 0

:fail
echo.
echo BUILD FAILED
exit /b 1
