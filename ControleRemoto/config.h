#pragma once
// ============================================================
//  CONFIGURACAO - ajuste para a sua instalacao
// ============================================================

// --- SIMULADOR: 1 = o ESP32 simula o CLP (teste do controle sem maquina) ---
//     0 = comunica com o CLP de verdade. Desligue antes de usar na maquina!
#define MODO_SIMULADOR 1

// --- Ligacao com o CLP: 0 = WiFi da fabrica | 1 = cabo de rede (modulo W5500) ---
#define USAR_ETHERNET  0

// Cabo de rede (W5500): IP fixo na mesma faixa do CLP
#define ETH_IP         192,168,0,50
#define ETH_GATEWAY    192,168,0,1
#define ETH_MASCARA    255,255,255,0
// Ligacao do W5500 no ESP32 DevKit
#define ETH_PINO_SCK   18
#define ETH_PINO_MISO  19
#define ETH_PINO_MOSI  23
#define ETH_PINO_CS    5
#define ETH_PINO_INT   4
#define ETH_PINO_RST   14

// --- Senhas: WIFI_SSID, WIFI_SENHA, AP_SENHA, BLE_PIN ---
// Ficam em segredos.h (fora do GitHub). Modelo: segredos_exemplo.h
#if __has_include("segredos.h")
#include "segredos.h"
#else
#include "segredos_exemplo.h"
#endif

// --- Rede propria do ESP32 (painel web): http://192.168.4.1 ---
#define AP_SSID        "Controle-Flexo"

// --- Bluetooth (app Android) ---
#define BLE_NOME       "Controle-Flexo"

// --- CLP Siemens S7-1200 / S7-1500 ---
#define CLP_IP         "192.168.0.1"
#define CLP_RACK       0
#define CLP_SLOT       1      // S7-1200: 1 | S7-1500: 1 (ou 0)

// --- Data Block no CLP (acesso NAO otimizado) - ver README ---
//  DBB0..DBB2  comandos do painel (celular)   escrito pelo ESP32
//  DBB3        botoes fisicos do ESP32        escrito pelo ESP32
//  DBW4        heartbeat (Int)                escrito pelo ESP32
//  DBB6        LEDs das estacoes 1..8         lido do CLP
//  DBB7        .0 LED CPC .1 LED Anilox .2 Em operacao .3 Falha   lido do CLP
//  DBW8        velocidade atual (Int, m/min)  lido do CLP
#define CLP_DB         10
#define DB_OFS_CMD     0      // 6 bytes: DBB0..DBB5
#define DB_OFS_STATUS  6      // 4 bytes: DBB6..DBB9

#define CICLO_MS       100    // intervalo de comunicacao com o CLP
#define WEB_TIMEOUT_MS 500    // sem atualizacao do celular -> comandos do painel zeram

// --- Pinos (ESP32 DevKit / WROOM) ---
// Botoes fisicos: ligar entre o pino e GND (pull-up interno) -> DBB3.0 .. DBB3.4
#define PINO_BTN_1  25
#define PINO_BTN_2  26
#define PINO_BTN_3  27
#define PINO_BTN_4  32
#define PINO_BTN_5  33

// LEDs (com resistor de ~330 ohm para GND)
#define PINO_LED_LINK      2   // LED azul da placa: pisca = sem CLP, aceso = conectado
#define PINO_LED_OPERACAO  16   // (18/19 ficam livres para o W5500)
#define PINO_LED_FALHA     17
