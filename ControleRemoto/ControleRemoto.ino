// ============================================================
//  Controle Remoto Flexo (Sleeve) para CLP Siemens S7-1200 / S7-1500 - ESP32
//  Painel no celular + botoes fisicos -> bits de comando num DB (protocolo S7)
//  Status do CLP (LEDs das estacoes, CPC/Anilox, velocidade) -> painel e LEDs
//  Ajuste rede, IP do CLP, DB e pinos em config.h
// ============================================================
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#if USAR_ETHERNET
#include <ETH.h>
#include <SPI.h>
#endif
#include "config.h"
#include "S7Client.h"
#include "painel.h"
#include "Bluetooth.h"

S7Client clp;
WebServer web(80);

struct Botao {
  uint8_t pino;
  bool estado;       // true = pressionado (ja filtrado)
  bool leitura;
  uint32_t tMud;
};
Botao botoes[] = {
  {PINO_BTN_1, false, false, 0},
  {PINO_BTN_2, false, false, 0},
  {PINO_BTN_3, false, false, 0},
  {PINO_BTN_4, false, false, 0},
  {PINO_BTN_5, false, false, 0},
};
const int N_BOTOES = sizeof(botoes) / sizeof(botoes[0]);

uint32_t webBits = 0;         // bits do painel (celular)
uint32_t tWeb = 0;            // ultima atualizacao do celular
uint16_t heartbeat = 0;
uint8_t  stEstacoes = 0, stFlags = 0;
int16_t  velocidade = 0;
uint32_t tCiclo = 0, tTentativa = 0;
uint32_t esperaReconexao = 1000;
bool linkOk = false;

// ---------------- Botoes fisicos ----------------
void lerBotoes() {
  uint32_t agora = millis();
  for (int i = 0; i < N_BOTOES; i++) {
    bool l = digitalRead(botoes[i].pino) == LOW;
    if (l != botoes[i].leitura) { botoes[i].leitura = l; botoes[i].tMud = agora; }
    if (botoes[i].estado != l && agora - botoes[i].tMud >= 30) {
      botoes[i].estado = l;
      Serial.printf("[BTN] fisico %d %s\n", i + 1, l ? "pressionado" : "solto");
    }
  }
}

uint8_t bitsFisicos() {
  uint8_t b = 0;
  for (int i = 0; i < N_BOTOES; i++) if (botoes[i].estado) b |= (1 << i);
  return b;
}

// Comandos do painel valem somente enquanto o celular continua mandando
uint32_t bitsPainel() {
  if (webBits && millis() - tWeb > WEB_TIMEOUT_MS) {
    Serial.println("[WEB] celular sem resposta - comandos zerados");
    webBits = 0;
  }
  return webBits;
}

// ---------------- Servidor web ----------------
void responderStatus() {
  char j[96];
  snprintf(j, sizeof(j), "{\"l\":%d,\"w\":%d,\"s\":%u,\"t\":%u,\"v\":%d}",
           linkOk ? 1 : 0, redeOk() ? 1 : 0,
           stEstacoes, stFlags, velocidade);
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", j);
}

void configurarWeb() {
  web.on("/", []() { web.send_P(200, "text/html", PAINEL_HTML); });
  web.on("/cmd", []() {
    uint32_t b = strtoul(web.arg("b").c_str(), nullptr, 10) & 0x7FFFF;  // 19 bits
    if (b != webBits) Serial.printf("[WEB] comandos 0x%05X\n", (unsigned)b);
    webBits = b;
    tWeb = millis();
    responderStatus();
  });
  web.on("/st", responderStatus);
  web.onNotFound([]() { web.sendHeader("Location", "/"); web.send(302); });
  web.begin();
}

// ---------------- Rede do CLP (WiFi ou cabo) ----------------
// O celular sempre entra pela rede propria do ESP32 (AP); o CLP e alcancado
// pelo WiFi da fabrica ou pelo cabo de rede (W5500), conforme USAR_ETHERNET.
bool redeOk() {
#if USAR_ETHERNET
  return ETH.linkUp() && ETH.hasIP();
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

void conectarRede() {
#if USAR_ETHERNET
  WiFi.mode(WIFI_AP);
#else
  WiFi.mode(WIFI_AP_STA);
#endif
  WiFi.softAP(AP_SSID, AP_SENHA);
  Serial.printf("[AP] rede \"%s\" - painel em http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

#if USAR_ETHERNET
  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_PINO_CS, ETH_PINO_INT, ETH_PINO_RST, SPI2_HOST,
                 ETH_PINO_SCK, ETH_PINO_MISO, ETH_PINO_MOSI)) {
    Serial.println("[ETH] W5500 nao encontrado - confira a ligacao SPI");
  } else {
    ETH.config(IPAddress(ETH_IP), IPAddress(ETH_GATEWAY), IPAddress(ETH_MASCARA));
    Serial.printf("[ETH] W5500 iniciado, IP fixo %s\n", IPAddress(ETH_IP).toString().c_str());
  }
#else
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  Serial.printf("[WiFi] conectando a %s...\n", WIFI_SSID);
#endif
  if (MDNS.begin("flexo")) MDNS.addService("http", "tcp", 80);
}

void monitorarRede() {
  static bool ok = false;
  static uint32_t tAviso = 0;
  bool r = redeOk();
  if (r != ok) {
    ok = r;
#if USAR_ETHERNET
    if (r) Serial.printf("[ETH] cabo conectado, IP %s\n", ETH.localIP().toString().c_str());
    else { Serial.println("[ETH] cabo desconectado"); clp.disconnect(); }
#else
    if (r) Serial.printf("[WiFi] conectado, IP %s, sinal %d dBm\n",
                         WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else { Serial.println("[WiFi] desconectado"); clp.disconnect(); }
#endif
  }
  if (!r && millis() - tAviso > 5000) {
    tAviso = millis();
#if USAR_ETHERNET
    Serial.println("[ETH] aguardando cabo de rede / link");
#else
    wl_status_t s = WiFi.status();
    Serial.printf("[WiFi] aguardando rede \"%s\" (%s)\n", WIFI_SSID,
      s == WL_NO_SSID_AVAIL ? "rede nao encontrada" :
      s == WL_CONNECT_FAILED ? "senha incorreta?" : "tentando...");
#endif
  }
}

// ---------------- CLP ----------------
void cicloComunicacao() {
  if (!redeOk()) { linkOk = false; return; }

  if (!clp.connected()) {
    linkOk = false;
    if (millis() - tTentativa < esperaReconexao) return;
    tTentativa = millis();
    Serial.printf("[S7] conectando %s rack %d slot %d...\n", CLP_IP, CLP_RACK, CLP_SLOT);
    if (!clp.connect(CLP_IP, CLP_RACK, CLP_SLOT)) {
      Serial.printf("[S7] falha: %s\n", clp.lastError());
      esperaReconexao = min<uint32_t>(esperaReconexao * 2, 10000);
      return;
    }
    Serial.println("[S7] conectado");
    esperaReconexao = 1000;
  }

  uint32_t p = bitsPainel() | bleBits(WEB_TIMEOUT_MS);   // painel web + app Bluetooth
  heartbeat++;
  uint8_t cmd[6] = {
    (uint8_t)(p & 0xFF),           // DBB0
    (uint8_t)((p >> 8) & 0xFF),    // DBB1
    (uint8_t)((p >> 16) & 0xFF),   // DBB2
    bitsFisicos(),                 // DBB3
    (uint8_t)(heartbeat >> 8),     // DBW4 (big-endian, formato Siemens)
    (uint8_t)(heartbeat & 0xFF)
  };
  if (!clp.writeDB(CLP_DB, DB_OFS_CMD, cmd, sizeof(cmd))) {
    Serial.printf("[S7] escrita: %s\n", clp.lastError());
    linkOk = false;
    return;
  }

  uint8_t st[4];
  if (!clp.readDB(CLP_DB, DB_OFS_STATUS, st, sizeof(st))) {
    Serial.printf("[S7] leitura: %s\n", clp.lastError());
    linkOk = false;
    return;
  }
  linkOk = true;
  stEstacoes = st[0];
  stFlags = st[1];
  velocidade = (int16_t)((st[2] << 8) | st[3]);
  digitalWrite(PINO_LED_OPERACAO, (stFlags & 0x04) ? HIGH : LOW);
  digitalWrite(PINO_LED_FALHA,    (stFlags & 0x08) ? HIGH : LOW);
}

void atualizarLedLink() {
  if (linkOk) { digitalWrite(PINO_LED_LINK, HIGH); return; }
  digitalWrite(PINO_LED_OPERACAO, LOW);
  digitalWrite(PINO_LED_FALHA, LOW);
  uint32_t periodo = redeOk() ? 250 : 1000;  // rapido = rede ok, sem CLP
  digitalWrite(PINO_LED_LINK, (millis() / periodo) % 2);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Controle Remoto Flexo S7 - ESP32 ===");
  for (int i = 0; i < N_BOTOES; i++) pinMode(botoes[i].pino, INPUT_PULLUP);
  pinMode(PINO_LED_LINK, OUTPUT);
  pinMode(PINO_LED_OPERACAO, OUTPUT);
  pinMode(PINO_LED_FALHA, OUTPUT);
  conectarRede();
  configurarWeb();
  bleIniciar(BLE_NOME, BLE_PIN);
}

void loop() {
  web.handleClient();
  monitorarRede();
  lerBotoes();
  if (millis() - tCiclo >= CICLO_MS) {
    tCiclo = millis();
    cicloComunicacao();
    bleStatus(linkOk, redeOk(), stEstacoes, stFlags, velocidade);
  }
  atualizarLedLink();
}
