@echo off
rem ============================================================
rem  vcrack - first-time setup
rem  Builds vcrack.exe, prepares config files, checks the system.
rem  Run this once. For a plain rebuild later, use build.bat.
rem ============================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"
echo(
echo === vcrack setup ===
echo(

rem --- 1) config files ------------------------------------------------
if not exist rules.txt (
  if exist rules.example.txt ( copy /y rules.example.txt rules.txt >nul & echo [+] rules.txt created from rules.example.txt - edit it for your words )
) else ( echo [=] rules.txt already present )
if not exist list.txt ( type nul > list.txt & echo [+] empty list.txt created - add one word per line ) else ( echo [=] list.txt already present )

rem --- 2) make sure the MSVC compiler is available --------------------
where cl >nul 2>nul
if %errorlevel%==0 goto build
echo [*] Looking for Visual Studio C++ tools ...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [-] Visual Studio C++ build tools not found.
  echo     Install "Desktop development with C++" from the Visual Studio Installer,
  echo     or open the "x64 Native Tools Command Prompt for VS" and run build.bat there.
  echo(
  pause & exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" ( echo [-] VC tools not found. & pause & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:build
echo [*] Building vcrack.exe ...
cl /nologo /O2 /EHsc /std:c++17 /Fe:vcrack.exe vcrack.cpp
if exist vcrack.obj del vcrack.obj
if not exist vcrack.exe ( echo [-] Build failed - see messages above. & pause & exit /b 1 )
echo [+] vcrack.exe built.
echo(

rem --- 3) environment checks -----------------------------------------
if exist hashcat.exe (
  echo [=] hashcat.exe found. Checking GPU ...
  .\hashcat.exe -I 2>nul | findstr /C:"Device ID" >nul && ( echo [+] A compute device is visible to hashcat. ) || ( echo [!] hashcat sees NO device yet - check GPU driver/CUDA. )
) else (
  echo [!] hashcat.exe is NOT in this folder. Put vcrack next to hashcat.exe ^(hashcat 7.x^).
)
where py >nul 2>nul || where python >nul 2>nul
if %errorlevel%==0 ( echo [=] Python found - the GUI ^(vcrack-gui.bat^) will work. ) else ( echo [!] Python 3 not found - needed only for the GUI. )

echo(
echo === Setup done ===
echo   1) Put your words in   list.txt   ^(one per line^)
echo   2) Adjust the rules in rules.txt
echo   3) Make sure the VeraCrypt header is here ^(e.g. Fvc_header.tc^)
echo   Then:   vcrack estimate      or   vcrack-gui.bat
echo(
pause
