#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_RX  "12345678-1234-1234-1234-1234567890ac"
#define CHARACTERISTIC_TX  "12345678-1234-1234-1234-1234567890ad"

BLECharacteristic *txCharacteristic;

String mensajeRecibido = ""; // buffer global


class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    Serial.println("✅ Cliente conectado");
  }

  void onDisconnect(BLEServer* pServer) {
    Serial.println("❌ Cliente desconectado");
    pServer->startAdvertising();
  }
};


class RecibirMensajeBLE : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    mensajeRecibido = pCharacteristic->getValue();
    Serial.println(mensajeRecibido);
  }
};


void EnviarMensajeBLE(String mensaje) {
    txCharacteristic->setValue(mensaje);
    txCharacteristic->notify();
    Serial.print("📤 Mensaje enviado: ");
    Serial.println(mensaje);
}

void setup() {
  Serial.begin(115200);

  Serial.println("Iniciando BLE...");
  IniciarBLE();

}

void IniciarBLE() {
  BLEDevice::init("ESP32_BLE_TEST");

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);

  // Característica TX (ESP32 → cliente)
  txCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_TX,
    BLECharacteristic::PROPERTY_NOTIFY);
  txCharacteristic->addDescriptor(new BLE2902());

  // Característica RX (cliente → ESP32)
  BLECharacteristic *rxCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_RX,
    BLECharacteristic::PROPERTY_WRITE);
  rxCharacteristic->setCallbacks(new RecibirMensajeBLE());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.println("📡 Advertencia BLE iniciada");
}

void loop() {
  // put your main code here, to run repeatedly:

}
