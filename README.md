# Controle Remoto Flexo (Sleeve) para CLP Siemens, com ESP32

O ESP32 serve no celular uma tela que replica o controle remoto Sleeve da impressora flexográfica. Cada botão da tela, e cada botão físico ligado ao ESP32, vira um bit num DB do CLP S7-1200/S7-1500 pelo WiFi (protocolo S7, porta TCP 102). O status do CLP (LEDs das estações, CPC/Anilox, velocidade) volta para a tela.

## Como usar no celular
1. Conecte o celular à rede WiFi **Controle-Flexo** (senha `flexo1234`, troque em `config.h`).
2. Abra **http://192.168.4.1** no navegador. Para virar "app": menu do navegador → *Adicionar à tela inicial*.
3. Se o celular estiver na mesma rede da fábrica, também dá para usar o IP que o ESP32 mostra no monitor serial.

Indicador no topo da tela: 🟢 CLP conectado · 🟡 WiFi da fábrica ok, sem CLP · 🔴 sem WiFi da fábrica.
A tarja vermelha "Sem comunicação" aparece quando o celular perde o ESP32.

## Mapa do DB10 (acesso NÃO otimizado)

### Comandos: escritos pelo ESP32 (1 = botão pressionado)
| Endereço | Botão da tela | Endereço | Botão da tela |
|---|---|---|---|
| DBX0.0 | COR (seleciona estação) | DBX1.0 | Coluna esquerda ▲ |
| DBX0.1 | Direcional ▲ | DBX1.1 | Coluna esquerda ▼ |
| DBX0.2 | Direcional ▼ | DBX1.2 | Coluna do meio ▲▲ |
| DBX0.3 | Direcional ◀ | DBX1.3 | Coluna do meio ▼▼ |
| DBX0.4 | Direcional ▶ | DBX1.4 | Coluna direita ▲ |
| DBX0.5 | CPC / ANILOX | DBX1.5 | Coluna direita ▼ |
| DBX0.6 | IMPULSO | DBX1.6 | BATEÇÃO (gira o sistema de tinta) |
| DBX0.7 | ENCOSTA / AVANÇA | DBX1.7 | AFASTA / RECUA |
| DBX2.0 | LIMPEZA | DBX3.0..3.4 | Botões físicos 1..5 do ESP32 (GPIO 25, 26, 27, 32, 33) |
| DBX2.1 | AUMENTAR VELOCIDADE | DBW4 | Heartbeat (Int, +1 a cada 100 ms) |
| DBX2.2 | DIMINUIR VELOCIDADE | | |

### Status: escrito pelo CLP, mostrado na tela
| Endereço | Tipo | Significado |
|---|---|---|
| DBX6.0 .. 6.7 | Bool | LEDs das estações 1..8 |
| DBX7.0 | Bool | LED CPC |
| DBX7.1 | Bool | LED ANILOX |
| DBX7.2 | Bool | Em operação (também acende o LED no GPIO16) |
| DBX7.3 | Bool | Falha (também acende o LED no GPIO17) |
| DBW8 | Int | Velocidade atual (m/min) |

Monte o DB no TIA Portal com variáveis nessa ordem e confira a coluna *Offset*. O DB precisa ter no mínimo 10 bytes.

## Configuração no TIA Portal
1. **CPU → Propriedades → Proteção e segurança → Mecanismos de conexão**: marque **"Permitir acesso via comunicação PUT/GET por parceiro remoto"**.
2. DB10 → Propriedades → **desmarque "Acesso otimizado ao bloco"**.
3. Carregue hardware **e** software na CPU.

### ⚠ Segurança: lógica obrigatória no CLP
- Todos os botões da tela são **"segurar para acionar"**: o bit fica em 1 só enquanto o dedo está na tela. Se o celular parar de responder, o ESP32 zera os comandos em 0,5 s.
- Se o **ESP32** perder a comunicação, o último valor fica travado no DB. Por isso o CLP **precisa** vigiar o heartbeat:

```
// SCL: no OB1 (ou num OB cíclico)
IF "DB10".Heartbeat <> #HB_Anterior THEN
    #HB_Anterior := "DB10".Heartbeat;
    #T_Watchdog(IN := FALSE, PT := T#1S);
END_IF;
#T_Watchdog(IN := TRUE, PT := T#1S);
#Remoto_OK := NOT #T_Watchdog.Q;            // FALSE se 1 s sem heartbeat

// Use os comandos SOMENTE com #Remoto_OK, por exemplo:
#Impulso_Remoto := #Remoto_OK AND "DB10".Cmd_Impulso;
// Botões de seleção (COR, CPC/ANILOX): use borda de subida (R_TRIG)
```

- A **emergência não está no celular**. Ela continua no botão físico da máquina, ligado por fio (NR-12).
- Movimentos (impulso, encosta/avança, ajustes) devem ter os mesmos intertravamentos do controle original.

## Ligação no ESP32 (DevKit / WROOM)
| Função | GPIO | Ligação |
|---|---|---|
| Botões físicos 1..5 | 25, 26, 27, 32, 33 | botão NA entre o pino e GND |
| LED Em operação | 16 | LED + resistor 330 Ω para GND |
| LED Falha | 17 | LED + resistor 330 Ω para GND |
| LED Link | 2 | LED azul da placa: pisca devagar = sem WiFi · rápido = sem CLP · aceso = ok |

## Ligação com o CLP por cabo de rede (opcional)
O ESP32 DevKit não tem porta de rede. Use um **módulo W5500** (SPI) e, em `config.h`, coloque `USAR_ETHERNET 1` e um IP fixo livre na faixa do CLP (`ETH_IP`, `ETH_GATEWAY`, `ETH_MASCARA`).

| W5500 | ESP32 |
|---|---|
| SCK / SCLK | GPIO18 |
| MISO / SO | GPIO19 |
| MOSI / SI | GPIO23 |
| CS / SCS | GPIO5 |
| INT | GPIO4 |
| RST | GPIO14 |
| 3V3 / GND | 3V3 / GND |

Com cabo, o celular continua usando a rede **Controle-Flexo** do ESP32. A faixa do CLP não pode ser 192.168.4.x, que é a faixa usada pela rede do ESP32.

## Configurar e gravar
1. Edite `ControleRemoto/config.h`: `WIFI_SSID`, `WIFI_SENHA` (rede da fábrica, **2,4 GHz**), `CLP_IP`, e a senha do `AP_SENHA`.
2. Gravar: `gravar.bat COM4`, ou pela Arduino IDE (placa **ESP32 Dev Module**).
3. Monitor serial em **115200** mostra a conexão, os comandos da tela (`[WEB] comandos 0x...`) e os botões físicos.

## Problemas comuns
| Mensagem no Serial | Causa provável |
|---|---|
| `TCP 102 recusado/sem resposta` | IP errado, CLP em outra rede, firewall |
| `COTP recusado (rack/slot?)` | rack/slot errado (S7-1200: 0/1) |
| `erro S7 ... (PUT/GET liberado?)` | PUT/GET não habilitado ou não carregado na CPU |
| `escrita/leitura recusada (DB/otimizado/tamanho?)` | DB não existe, está com acesso otimizado ou tem menos de 10 bytes |
