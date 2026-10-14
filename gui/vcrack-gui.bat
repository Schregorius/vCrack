@echo off
rem Startet die vcrack-Oberflaeche in einem eigenstaendigen Fenster.
rem Diese Datei liegt im hashcat-Ordner (neben vcrack.exe); der Server in .\gui\
setlocal
cd /d "%~dp0"

set "PY="
where py >nul 2>nul && set "PY=py"
if not defined PY ( where python >nul 2>nul && set "PY=python" )
if not defined PY ( echo [-] Python nicht gefunden. Bitte Python installieren. & pause & exit /b 1 )

set "VCRACK_DIR=%~dp0"
set "VCRACK_PORT=8765"
set "URL=http://127.0.0.1:%VCRACK_PORT%/"

echo [*] Starte Server ...
start "vcrack-server" /min "%PY%" "%~dp0gui\gui_server.py"

rem kurz warten, bis der Server lauscht
powershell -NoProfile -Command "for($i=0;$i -lt 30;$i++){try{(New-Object Net.Sockets.TcpClient).Connect('127.0.0.1',%VCRACK_PORT%);exit}catch{Start-Sleep -Milliseconds 200}}" >nul 2>nul

rem Browser im App-Modus = eigenstaendiges Fenster (Chrome bevorzugt, sonst Edge)
set "CHROME1=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
set "CHROME2=%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe"
set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"

if exist "%CHROME1%" ( start "" "%CHROME1%" --app=%URL% --window-size=1200,840 & goto done )
if exist "%CHROME2%" ( start "" "%CHROME2%" --app=%URL% --window-size=1200,840 & goto done )
if exist "%EDGE%"   ( start "" "%EDGE%"   --app=%URL% --window-size=1200,840 & goto done )
start "" "%URL%"
:done

echo.
echo [i] Oberflaeche geoeffnet. Dieses Fenster offen lassen - schliessen beendet den Server.
echo     (Ein laufender Angriff wird dann gestoppt; der Fortschritt bleibt gespeichert.)
echo.
pause >nul
