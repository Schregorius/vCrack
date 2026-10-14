@echo off
rem Baut vcrack.exe mit MSVC. Sucht die VS-Umgebung selbst, falls cl fehlt.
setlocal
where cl >nul 2>nul
if %errorlevel%==0 goto build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [-] vswhere.exe nicht gefunden.
    echo     Bitte die "x64 Native Tools Command Prompt for VS 2026" oeffnen und dort:
    echo     cl /nologo /O2 /EHsc /std:c++17 /Fe:vcrack.exe vcrack.cpp
    pause
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" ( echo [-] Keine VC-Tools gefunden. & pause & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:build
echo.
echo [*] Kompiliere vcrack.cpp ...
echo.
cl /nologo /O2 /EHsc /std:c++17 /Fe:vcrack.exe vcrack.cpp
if exist vcrack.obj del vcrack.obj
echo.
if exist vcrack.exe ( echo [+] Fertig: vcrack.exe ) else ( echo [-] Build fehlgeschlagen - siehe Meldungen oben. )
echo.
pause
