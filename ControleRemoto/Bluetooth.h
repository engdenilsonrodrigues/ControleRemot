#pragma once
// Servidor BLE para o app Android "Controle Flexo".
//  Servico  6e7a0001-5f1e-4c7b-9a51-0f1e7c0a5e01
//  Comando  ...0002  escrita, 4 bytes (uint32 little-endian, mesmos bits do painel web)
//  Status   ...0003  leitura/notificacao, 6 bytes:
//           [0] link CLP  [1] rede ok  [2] LEDs estacoes  [3] flags  [4..5] velocidade int16 LE
// Acesso protegido por PIN (pareamento com MITM).
#include <Arduino.h>

void     bleIniciar(const char* nome, uint32_t pin);
uint32_t bleBits(uint32_t timeoutMs);     // comandos atuais do app (zera se o app parar de enviar)
bool     bleConectado();
void     bleStatus(bool link, bool rede, uint8_t estacoes, uint8_t flags, int16_t vel);
