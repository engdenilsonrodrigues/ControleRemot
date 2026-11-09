@echo off
chcp 65001 >nul
REM Grava o firmware pronto do Controle Flexo no ESP32, sem compilar.
REM Uso: gravar_firmware.bat COM4            (grava o firmware simulador)
REM      gravar_firmware.bat COM4 arquivo.bin (grava outro firmware desta pasta)
cd /d "%~dp0"

if "%~1"=="" (
  echo Uso: gravar_firmware.bat COMx [arquivo.bin]
  echo Veja a porta em: Gerenciador de Dispositivos ^> Portas ^(COM e LPT^)
  pause & exit /b 1
)
set PORTA=%~1
set BIN=%~2
if "%BIN%"=="" set BIN=ControleFlexo-ESP32-simulador.bin
if not exist "%BIN%" (echo Arquivo %BIN% nao encontrado. & pause & exit /b 1)

REM Procura o esptool: instalado com o core ESP32 da Arduino IDE, ou no PATH
set ESPTOOL=
for /d %%V in ("%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\*") do (
  if exist "%%V\esptool.exe" set ESPTOOL="%%V\esptool.exe"
)
if not defined ESPTOOL (
  where esptool >nul 2>nul && set ESPTOOL=esptool
)
if not defined ESPTOOL (
  echo esptool nao encontrado.
  echo Instale a Arduino IDE com o pacote "esp32 by Espressif" ou rode: pip install esptool
  pause & exit /b 1
)

echo Gravando %BIN% no ESP32 pela %PORTA%...
%ESPTOOL% --chip esp32 --port %PORTA% --baud 921600 write-flash 0x0 "%BIN%"
if errorlevel 1 (
  echo.
  echo FALHOU. Se ficar em "Connecting...", segure o botao BOOT da placa ate comecar.
  pause & exit /b 1
)
echo.
echo PRONTO: firmware gravado. A placa ja reiniciou.
pause
