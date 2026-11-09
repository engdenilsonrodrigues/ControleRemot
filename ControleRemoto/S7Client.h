#pragma once
// Cliente S7 minimo (ISO-on-TCP, porta 102) para leitura/escrita de bytes em DB.
// Requer no CLP: "Permitir acesso PUT/GET" e DB com acesso nao otimizado.
#include <Arduino.h>
#include <WiFi.h>

class S7Client {
public:
  bool connect(const char* ip, uint8_t rack, uint8_t slot);
  void disconnect();
  bool connected();
  bool writeDB(uint16_t db, uint16_t start, const uint8_t* data, uint16_t len);
  bool readDB(uint16_t db, uint16_t start, uint8_t* data, uint16_t len);
  const char* lastError() const { return _err; }

private:
  WiFiClient _tcp;
  uint16_t _seq = 0;
  uint16_t _pdu = 240;
  const char* _err = "";
  uint8_t _buf[512];

  bool sendRaw(const uint8_t* p, size_t n);
  int  recvPacket();                     // retorna tamanho total (TPKT) ou -1
  bool fail(const char* e) { _err = e; disconnect(); return false; }
  void fillAddr(uint8_t* p, uint16_t db, uint16_t start, uint16_t len);
};
