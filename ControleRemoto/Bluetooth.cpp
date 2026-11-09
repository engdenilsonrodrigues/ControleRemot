#include "Bluetooth.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLESecurity.h>

#define UUID_SERVICO "6e7a0001-5f1e-4c7b-9a51-0f1e7c0a5e01"
#define UUID_CMD     "6e7a0002-5f1e-4c7b-9a51-0f1e7c0a5e01"
#define UUID_STATUS  "6e7a0003-5f1e-4c7b-9a51-0f1e7c0a5e01"

static BLECharacteristic* chStatus = nullptr;
static volatile uint32_t bits = 0;
static volatile uint32_t tBits = 0;
static volatile bool conectado = false;
static uint8_t ultimoStatus[6] = {0xFF};
static uint32_t tStatus = 0;

class CallbacksServidor : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    conectado = true;
    Serial.println("[BLE] celular conectado");
  }
  void onDisconnect(BLEServer*) override {
    conectado = false;
    bits = 0;                                  // perdeu o celular -> solta tudo
    Serial.println("[BLE] celular desconectado - comandos zerados");
    BLEDevice::startAdvertising();
  }
};

class CallbacksComando : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if (v.length() < 4) return;
    const uint8_t* d = (const uint8_t*)v.c_str();
    uint32_t b = ((uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24)) & 0x7FFFF;
    if (b != bits) Serial.printf("[BLE] comandos 0x%05X\n", (unsigned)b);
    bits = b;
    tBits = millis();
  }
};

void bleIniciar(const char* nome, uint32_t pin) {
  BLEDevice::init(nome);

  BLESecurity::setPassKey(true, pin);
  BLESecurity::setCapability(ESP_IO_CAP_OUT);            // ESP32 "mostra" o PIN -> celular digita
  BLESecurity::setAuthenticationMode(true, true, true);  // bonding, MITM, secure connections

  BLEServer* srv = BLEDevice::createServer();
  srv->setCallbacks(new CallbacksServidor());
  BLEService* svc = srv->createService(UUID_SERVICO);

  BLECharacteristic* chCmd = svc->createCharacteristic(UUID_CMD,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  chCmd->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  chCmd->setCallbacks(new CallbacksComando());

  chStatus = svc->createCharacteristic(UUID_STATUS,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chStatus->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  chStatus->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(UUID_SERVICO);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.printf("[BLE] anunciando \"%s\" (PIN de pareamento configurado em segredos.h)\n", nome);
}

uint32_t bleBits(uint32_t timeoutMs) {
  if (bits && millis() - tBits > timeoutMs) {
    Serial.println("[BLE] app sem resposta - comandos zerados");
    bits = 0;
  }
  return bits;
}

bool bleConectado() { return conectado; }

void bleStatus(bool link, bool rede, uint8_t estacoes, uint8_t flags, int16_t vel) {
  if (!chStatus) return;
  uint8_t s[6] = {(uint8_t)link, (uint8_t)rede, estacoes, flags,
                  (uint8_t)(vel & 0xFF), (uint8_t)((uint16_t)vel >> 8)};
  bool mudou = memcmp(s, ultimoStatus, sizeof(s)) != 0;
  chStatus->setValue(s, sizeof(s));
  if (conectado && (mudou || millis() - tStatus > 1000)) {
    chStatus->notify();
    tStatus = millis();
    memcpy(ultimoStatus, s, sizeof(s));
  }
}
