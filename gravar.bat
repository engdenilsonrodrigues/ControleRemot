@echo off
REM Uso: gravar.bat COM5
if "%~1"=="" (
  echo Uso: gravar.bat COMx
  echo Portas encontradas:
  "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" board list
  exit /b 1
)
set CLI="C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
%CLI% compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app "%~dp0ControleRemoto" || exit /b 1
%CLI% upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p %1 "%~dp0ControleRemoto" || exit /b 1
echo Gravado com sucesso. Abrindo monitor serial (Ctrl+C para sair)...
%CLI% monitor -p %1 -c baudrate=115200
