# Firmware pronto do ESP32

Arquivos já compilados para gravar no ESP32 **sem precisar compilar**. Cada arquivo traz o bootloader, a tabela de partições e o programa, e é gravado no endereço `0x0`.

| Arquivo | Uso |
|---|---|
| `ControleFlexo-ESP32-simulador.bin` | **Simulador do CLP** (`MODO_SIMULADOR 1`). Serve para testar o app e o painel sem máquina. Bluetooth "Controle-Flexo" com PIN `123456`, rede WiFi própria "Controle-Flexo" (senha `flexo1234`). |

## Como gravar (Windows)
1. Ligue o ESP32 no USB e veja a porta no *Gerenciador de Dispositivos → Portas (COM e LPT)*. Se aparecer com erro, instale o driver **CP210x** da Silicon Labs.
2. Dê dois cliques em `gravar_firmware.bat`, ou rode no terminal:
   ```
   gravar_firmware.bat COM4
   ```
3. Se ficar parado em `Connecting...`, segure o botão **BOOT** da placa até a gravação começar.

O script usa o `esptool` que vem com a Arduino IDE (pacote *esp32 by Espressif*). Se não tiver a IDE, instale com `pip install esptool`.

Outros sistemas: `esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x0 ControleFlexo-ESP32-simulador.bin`

## Limites destes arquivos
- O firmware usa as senhas de **exemplo** (`segredos_exemplo.h`), que são públicas. Para uso real na máquina, compile a partir de `ControleRemoto/` com o seu `segredos.h`.
- A versão para a máquina real (`MODO_SIMULADOR 0`, com IP do CLP) depende dos dados da sua instalação e por isso não está aqui.
