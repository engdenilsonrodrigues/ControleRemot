#include "S7Client.h"

static const uint32_t TIMEOUT_MS = 1500;

bool S7Client::sendRaw(const uint8_t* p, size_t n) {
  return _tcp.write(p, n) == n;
}

// Le um pacote TPKT completo para _buf
int S7Client::recvPacket() {
  uint32_t t0 = millis();
  size_t got = 0, need = 4;
  while (got < need) {
    if (!_tcp.connected()) return -1;
    if (millis() - t0 > TIMEOUT_MS) return -1;
    int a = _tcp.available();
    if (a <= 0) { delay(1); continue; }
    int r = _tcp.read(_buf + got, min((size_t)a, need - got));
    if (r > 0) got += r;
    if (got == 4 && need == 4) {
      if (_buf[0] != 0x03) return -1;
      need = ((size_t)_buf[2] << 8) | _buf[3];
      if (need < 7 || need > sizeof(_buf)) return -1;
    }
  }
  return (int)need;
}

bool S7Client::connect(const char* ip, uint8_t rack, uint8_t slot) {
  disconnect();
  _tcp.setTimeout(TIMEOUT_MS / 1000 + 1);
  if (!_tcp.connect(ip, 102, TIMEOUT_MS)) return fail("TCP 102 recusado/sem resposta");
  _tcp.setNoDelay(true);

  // 1) COTP Connection Request
  uint8_t remoteTsap = (rack << 5) | slot;
  uint8_t cr[22] = {
    0x03, 0x00, 0x00, 0x16,
    0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
    0xC0, 0x01, 0x0A,              // TPDU 1024
    0xC1, 0x02, 0x01, 0x00,        // TSAP local
    0xC2, 0x02, 0x01, remoteTsap   // TSAP remoto (PG, rack/slot)
  };
  if (!sendRaw(cr, sizeof(cr))) return fail("falha envio COTP");
  int n = recvPacket();
  if (n < 6 || _buf[5] != 0xD0) return fail("COTP recusado (rack/slot?)");

  // 2) S7 Setup Communication
  uint8_t sc[25] = {
    0x03, 0x00, 0x00, 0x19,
    0x02, 0xF0, 0x80,
    0x32, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00,
    0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0xE0   // PDU 480
  };
  if (!sendRaw(sc, sizeof(sc))) return fail("falha envio setup");
  n = recvPacket();
  if (n < 27 || _buf[7] != 0x32 || _buf[17] != 0 || _buf[18] != 0)
    return fail("setup S7 recusado");
  _pdu = ((uint16_t)_buf[25] << 8) | _buf[26];
  _err = "";
  return true;
}

void S7Client::disconnect() {
  if (_tcp.connected()) _tcp.stop();
}

bool S7Client::connected() {
  return _tcp.connected();
}

// Especificacao de endereco (12 bytes apos 0x12 0x0A): tipo BYTE, qtd, DB, area 0x84, endereco de bit
void S7Client::fillAddr(uint8_t* p, uint16_t db, uint16_t start, uint16_t len) {
  uint32_t bitAddr = (uint32_t)start * 8;
  p[0] = 0x12; p[1] = 0x0A; p[2] = 0x10; p[3] = 0x02;
  p[4] = len >> 8; p[5] = len & 0xFF;
  p[6] = db >> 8;  p[7] = db & 0xFF;
  p[8] = 0x84;
  p[9] = (bitAddr >> 16) & 0xFF; p[10] = (bitAddr >> 8) & 0xFF; p[11] = bitAddr & 0xFF;
}

bool S7Client::writeDB(uint16_t db, uint16_t start, const uint8_t* data, uint16_t len) {
  if (!connected()) return fail("desconectado");
  if (len + 35 > _pdu || len + 35 > sizeof(_buf)) return fail("bloco grande demais");
  uint16_t total = 35 + len;
  uint8_t* p = _buf;
  _seq++;
  p[0] = 0x03; p[1] = 0x00; p[2] = total >> 8; p[3] = total & 0xFF;
  p[4] = 0x02; p[5] = 0xF0; p[6] = 0x80;
  p[7] = 0x32; p[8] = 0x01; p[9] = 0; p[10] = 0;
  p[11] = _seq >> 8; p[12] = _seq & 0xFF;
  p[13] = 0x00; p[14] = 0x0E;                       // param len
  p[15] = (len + 4) >> 8; p[16] = (len + 4) & 0xFF; // data len
  p[17] = 0x05; p[18] = 0x01;                       // write var, 1 item
  fillAddr(p + 19, db, start, len);
  p[31] = 0x00; p[32] = 0x04;                       // transporte: bytes
  p[33] = (len * 8) >> 8; p[34] = (len * 8) & 0xFF;
  memcpy(p + 35, data, len);
  if (!sendRaw(p, total)) return fail("falha envio escrita");

  int n = recvPacket();
  if (n < 22 || _buf[8] != 0x03) return fail("resposta de escrita invalida");
  if (_buf[17] != 0 || _buf[18] != 0) return fail("erro S7 na escrita (PUT/GET liberado?)");
  if (_buf[21] != 0xFF) return fail("escrita recusada (DB/otimizado/tamanho?)");
  return true;
}

bool S7Client::readDB(uint16_t db, uint16_t start, uint8_t* data, uint16_t len) {
  if (!connected()) return fail("desconectado");
  if (len + 25 > _pdu || len + 25 > sizeof(_buf)) return fail("bloco grande demais");
  uint8_t p[31];
  _seq++;
  p[0] = 0x03; p[1] = 0x00; p[2] = 0x00; p[3] = 31;
  p[4] = 0x02; p[5] = 0xF0; p[6] = 0x80;
  p[7] = 0x32; p[8] = 0x01; p[9] = 0; p[10] = 0;
  p[11] = _seq >> 8; p[12] = _seq & 0xFF;
  p[13] = 0x00; p[14] = 0x0E; p[15] = 0x00; p[16] = 0x00;
  p[17] = 0x04; p[18] = 0x01;                       // read var, 1 item
  fillAddr(p + 19, db, start, len);
  if (!sendRaw(p, sizeof(p))) return fail("falha envio leitura");

  int n = recvPacket();
  if (n < 25 || _buf[8] != 0x03) return fail("resposta de leitura invalida");
  if (_buf[17] != 0 || _buf[18] != 0) return fail("erro S7 na leitura (PUT/GET liberado?)");
  if (_buf[21] != 0xFF) return fail("leitura recusada (DB/otimizado/tamanho?)");
  if (n < 25 + len) return fail("resposta curta");
  memcpy(data, _buf + 25, len);
  return true;
}
