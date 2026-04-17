#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <Preferences.h>

//PIN GPIO15: SEÑAL DE ENCENDIDO (ON/OFF) "PIN TDO"

RTC_DATA_ATTR int bootNum = 0;            //VARIABLE PARA LLEVAR EL CONTEO DE LOS BOOT´S HECHOS
RTC_DATA_ATTR boolean despierto = false;  //VARIABLE PARA EVITAR QUE EL SISTEMA SE DUERMA DE MANERA INFINITA

RTC_DATA_ATTR int varRegistro = 0;

#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP 5 // segundos

Preferences preferences;

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_RX  "12345678-1234-1234-1234-1234567890ac"
#define CHARACTERISTIC_TX  "12345678-1234-1234-1234-1234567890ad"


float medirVoltaje(int pin) {
  long suma = 0;
  int muestras = 10;
  float Vref = 3.3;
  float R1 = 10000; //R15 de la placa, 12V
  float R2 = 1830; //R16 de la placa, 12V
  float R3 = 4800; //R23 de la placa, 3,7V
  float R4 = 4350; //R24 de la placa, 3,7V

  for (int i = 0; i < muestras; i++) {
    suma += analogRead(pin);
    delay(2);
  }

  float lectura = suma / (float)muestras;

  if (pin == 34) {
    float voltajePin = (lectura / 4095.0) * Vref;
    float voltajeReal = voltajePin * ((R1 + R2) / R2);

    return voltajeReal;
  } else if (pin == 13) {
    float voltajePin = (lectura / 4095.0) * Vref;
    float voltajeReal = voltajePin * ((R3 + R4) / R4);

    return voltajeReal;
  } else return 0.00;
}


void dormir() {
  if (despierto == false) {
    bootNum++;  //INCREMENTA CADA VEZ QUE SE DESPIERTA
    Serial.println("numero de boot: " + String(bootNum));

    float voltajeInterno = medirVoltaje(13);

    Serial.print("Voltaje de la motocicleta: ");
    Serial.print(medirVoltaje(34));
    Serial.println(" Voltios");

    Serial.print("Voltaje de la Bateria interna: ");
    Serial.print(voltajeInterno);
    Serial.println(" Voltios");

    int ledEstado = 25;
    pinMode(ledEstado, OUTPUT);

    if (voltajeInterno > 4.10) {
      digitalWrite(ledEstado, HIGH);
      delay(300);
      digitalWrite(ledEstado, LOW);
    } else if (voltajeInterno > 3.80) {
      for (int i = 0; i < 2; i++) {
        digitalWrite(ledEstado, HIGH);
        delay(200);
        digitalWrite(ledEstado, LOW);
        delay(200);
      }
    } else {
      for (int i = 0; i < 3; i++) {
        digitalWrite(ledEstado, HIGH);
        delay(150);
        digitalWrite(ledEstado, LOW);
        delay(150);
      }
    }


    if (bootNum >= 60000) {
      ESP.restart();
    }

    // if (bootNum == varRegistro + 113 ) {
    //   varRegistro = varRegistro + 113;

    //   preferences.begin("Contador", false);
    //   int contador = preferences.getInt("contador", 0); // leer
    //   contador++;
    //   //contador = 0;
    //   preferences.putInt("contador", contador); // guardar

    //   Serial.print("Valor de contador guardado: ");
    //   Serial.println(contador);

    //   preferences.end();
    // }


    esp_sleep_enable_ext0_wakeup(GPIO_NUM_15, 1);  //DETERMINA EL PIN GPIO RTC QUE DESPERTARA EL SISTEMA, 1 = High, 0 = Low
    Serial.println("..........Modo Deep Sleep..........");
    Serial.flush();
    despierto = true;
    esp_deep_sleep_start();
    Serial.println("...............EL SISTEMA NO ENTRO EN MODO DEEP SLEEP...............");  //SI SE DUERME CORRECTAMENTE ESTA LINEA NO DEBERIA IMPRIMIRSE
  }
}

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_TIMER:
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
      despierto = false;
      dormir();
      break;
  }
}

TaskHandle_t comunicacion;

int principal = 27;  //PIN GPIO PARA EL CONTROL DEL ENCENDIDO COMPLETO DE LA MOTOCICLETA
int sirena = 4;
int direccion = 32;  //PIN GPIO PARA LA ACTIVACION DE LAS DIRECCIONALES
int ledEstado = 25;   //PIN GPIO PARA EL CONTROL DE LA LLAVE ROJA DEL TABLERO
int lm2596 = 26;

BLECharacteristic *txCharacteristic;
bool deviceConnected = false;
bool conexionValida = false;
String mensajeRecibido = ""; // buffer global

long long claveCifrada = 0LL;
long claveDinamica = 0;
int desplazamiento = 0;
int claveDinamicaDescifrada[5];
int clave[4];

int estadoActual = 0;
int contadorPaquetesPerdidos = 0;

bool procesoParpadeo = true;
bool procesoEncender = false;
bool procesoAlarma = false;
bool estadoMotor = true;

int ArrayA[] = { 6, 2, 5, 1, 4, 9, 8, 7, 3 };  // Desplazar X+Num de la izquierda a la derecha
int ArrayB[] = { 2, 9, 8, 4, 6, 1, 5, 3, 7 };
int ArrayC[] = { 5, 3, 6, 7, 2, 9, 4, 8, 1 };


class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("✅ Cliente conectado");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("❌ Cliente desconectado");
    pServer->startAdvertising();
    procesoApagado(3);
  }
};


class RecibirMensajeBLE : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    mensajeRecibido = pCharacteristic->getValue();
    //Serial.println(mensajeRecibido.length());
    //Serial.println(mensajeRecibido);

    if (mensajeRecibido.length() == 5) {
      if (validarClaveDinamica(claveDinamica, mensajeRecibido, clave[1])) {
        Serial.println("Validación Correcta");
        if (conexionValida == false) {
          conexionValida = true;
          estadoActual = 2;
          procesoEncender = true;
        }
        contadorPaquetesPerdidos = 0;
      } else {
        Serial.println("Validación Fallida");
      }
    } else if (mensajeRecibido.length() == 3) {
      if (mensajeRecibido == "301") {
        if (estadoMotor == true){
          procesoApagado(2);
          estadoMotor = false;
        } else {
          procesoEncender = true;
          estadoMotor = true;
        }
        EnviarMensajeBLE("301Y");
      } else if (mensajeRecibido == "302") {
        alarma();
        EnviarMensajeBLE("302Y");
      }
    }
  }
};


void EnviarMensajeBLE(String mensaje) {
    if (deviceConnected) {
    txCharacteristic->setValue(mensaje);
    txCharacteristic->notify();
    Serial.print("📤 Mensaje enviado: ");
    Serial.println(mensaje);
  } else {
    Serial.print("No hay cliente conectado... Error al enviar mensaje");
  }
}


void setup() {
  Serial.begin(115200);

  // preferences.begin("Contador", false);
  // int contador = preferences.getInt("contador", 0); // leer
  // Serial.print("Ultimo registro del contador: ");
  // Serial.println(contador);

  analogReadResolution(12); // 0 - 4095
  analogSetPinAttenuation(34, ADC_11db); // GPIO34
  analogSetPinAttenuation(13, ADC_11db); // GPIO13

  print_wakeup_reason();
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  dormir();
  Serial.println();
  Serial.println("............BIENVENIDO............");
  Serial.println("CONTROL DE ENCENDIDO KTM 390");

  pinMode(principal, OUTPUT);
  pinMode(direccion, OUTPUT);
  pinMode(ledEstado, OUTPUT);
  pinMode(lm2596, OUTPUT);
  pinMode(sirena, OUTPUT);
  pinMode(15, INPUT);  //SEÑAL DE ARRANQUE
  digitalWrite(principal, LOW);
  digitalWrite(direccion, LOW);
  digitalWrite(ledEstado, LOW);
  digitalWrite(lm2596, LOW);
  digitalWrite(sirena, LOW);

  generarClaveInicial();

  Serial.println("Iniciando BLE...");
  IniciarBLE();

  xTaskCreatePinnedToCore(
    loop_comunicacion,
    "comunicacion",
    10000,
    NULL,
    0,
    &comunicacion,
    0);
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


unsigned long T = 0;
unsigned long T_Led = 0;
unsigned long T_Direccion = 0;
unsigned long T_Sirena = 0;
unsigned long T_Alarma = 0;

bool estadoled = false;
bool estadodireccion = true;
bool estadosirena = true;

int ciclosdireccion = 0;
int ciclossirena = 0;

void loop() {
  //if (analogRead(15) < 400) {
  if (digitalRead(15) == LOW) {
    procesoApagado(1);
  }

  if (procesoParpadeo == true){
    if (millis() >= T_Led + 150) {
      if (estadoled == false) {
        digitalWrite(ledEstado, HIGH);
        estadoled = true;
      } else {
        digitalWrite(ledEstado, LOW);
        estadoled = false;
      }
      T_Led = millis();
    }
  }

  if (procesoEncender == true) {
    if (millis() >= millis() + T) {
      Serial.println("Activando Sistema");
      T = millis();
      procesoParpadeo = false;
      digitalWrite(ledEstado, LOW);
      digitalWrite(lm2596, HIGH);
      digitalWrite(principal, HIGH);
      digitalWrite(sirena, HIGH);
      digitalWrite(direccion, HIGH);
    }

    if (ciclosdireccion <= 3) {
      if (millis() >= T + T_Direccion) {
        if (estadodireccion == true) {
          digitalWrite(direccion, LOW);
          estadodireccion = false;
          ciclosdireccion += 1;
        } else {
          digitalWrite(direccion, HIGH);
          estadodireccion = true;
        }
        T_Direccion += 300;
      }
    }

    if (ciclossirena <= 2) {
      if (millis() >= T + T_Sirena) {
        if (estadosirena == true) {
          digitalWrite(sirena, LOW);
          estadosirena = false;
          ciclossirena += 1;
        } else {
          digitalWrite(sirena, HIGH);
          estadosirena = true;
        }
        T_Sirena += 200;
      }
    }
    
    if (millis() >= T + 2000) {
      Serial.println("Encendiendo led");
      digitalWrite(ledEstado, HIGH);
      procesoEncender = false;
      ciclosdireccion = 0;
      ciclossirena = 0;
      T_Direccion = 0;
      T_Sirena = 0;
      T = 0;
    }
  }

  if (procesoAlarma == true) {
    if (millis() >= T_Alarma + 15000) {
      digitalWrite(principal, LOW);
      for (int i = 0; i < 40; i++) {
        if (i == 20)
          digitalWrite(sirena, HIGH);
        digitalWrite(ledEstado, HIGH);
        delay(250);
        digitalWrite(ledEstado, LOW);
        delay(250);
      }
      digitalWrite(sirena, LOW);
      ESP.restart();
    }
  } else T_Alarma = millis();

  delay(10);
}


void loop_comunicacion(void *pvParameters) {
  unsigned long tiempoAnterior = 0;
  unsigned long frecuencia = 1000;
  int contador = 0;

  while (1) {
    if (deviceConnected == true && estadoActual == 0) {
      delay(1000);
      EnviarMensajeBLE(String(claveCifrada));
      estadoActual = 1;
    }

    if (estadoActual == 1 && mensajeRecibido == "200") {
      delay(100);
      GenerarClaveDinamica();
      estadoActual = 2;
    } else if (estadoActual == 2) {
      if (millis() - tiempoAnterior >= frecuencia) {
        tiempoAnterior = millis();
        contador++;
      }
      if (contador >= 5) {
        Serial.println("\n\n\n\n");
        Serial.print("ContadorPerdidos: ");
        Serial.println(contadorPaquetesPerdidos);
        if (contadorPaquetesPerdidos >= 3) {
          procesoApagado(3);
        }
        GenerarClaveDinamica();
        contador = 0;
        contadorPaquetesPerdidos += 1;
      }
    }
    delay(20);
  }
}


bool generarClaveInicial() {
  int digitos = 0;
  long long copiaClaveCifrada = 0LL;
  int arrayclaveCifrada[15];

  while (digitos != 15) {
    digitos = 0;
    claveCifrada = 0;

    clave[0] = 0;

    for (int i = 0; i < 15; i++) {
      int digito = random(1, 10);  // Dígito aleatorio entre 1 y 9
      claveCifrada = claveCifrada * 10 + digito;
      if (digito != 0) {
        digitos++;
      }
    }
  }

  int i = 14;
  copiaClaveCifrada = claveCifrada;
  while (copiaClaveCifrada > 0) {
    arrayclaveCifrada[i] = copiaClaveCifrada % 10;
    copiaClaveCifrada /= 10;
    i--;
  }

  if (arrayclaveCifrada[1] != 0 && digitos == 15) {
    clave[0] = ((arrayclaveCifrada[0] * 10) + arrayclaveCifrada[1]);

    if (clave[0] <= 13) {
      clave[1] = arrayclaveCifrada[clave[0] - 1];
      clave[2] = arrayclaveCifrada[clave[0]];
      clave[3] = arrayclaveCifrada[clave[0] + 1];
      clave[0] = ((clave[1] * 100) + (clave[2] * 10) + (clave[3]));
    } else if (clave[0] >= 14) {
      clave[0] = arrayclaveCifrada[1];
      clave[1] = arrayclaveCifrada[clave[0] - 1];
      clave[2] = arrayclaveCifrada[clave[0]];
      clave[3] = arrayclaveCifrada[clave[0] + 1];
      clave[0] = ((clave[1] * 100) + (clave[2] * 10) + (clave[3]));
    }

    if (clave[0] >= 100) {
      Serial.print("Mensaje generado: ");
      Serial.println(claveCifrada);
      Serial.print("Clave: ");
      Serial.println(clave[0]);
      return true;
    } else {
      digitos = 0;
    }
  } else {
    digitos = 0;
  }
}


void GenerarClaveDinamica() {
  int digitos = 0;
  claveDinamica = 0;

  while (digitos != 5) {
    for (int i = 0; i < 5; i++) {
      int digito = random(1, 10);  // Dígito aleatorio entre 1 y 9
      claveDinamica = claveDinamica * 10 + digito;
      digitos++;
    }
  }
  EnviarMensajeBLE(String(claveDinamica));
}


bool validarClaveDinamica(long claveDinamica, String mensajeRecibido, int claveA) {
  Serial.print("Clave Dinamica: ");
  Serial.println(claveDinamica);

  int i = 4;
  while (claveDinamica > 0) {
    claveDinamicaDescifrada[i] = claveDinamica % 10;
    claveDinamica /= 10;
    i--;
  }

  //claveDinamica = 79328;
  //claveA = 7;
  // claveDinamica  = { 7, 9, 3, 2, 8 }
  // ArrayA[] = { 6, 2, 5, 1, 4, 9, 8, 7, 3 }; // Desplazar a la derecha la cantidad de veces del numero a la izquierda + la clave
  // claveDinamicaF = { 9, 2, 8, 5, 8 }  

  int arrayMensajeOriginal[5];  // Para guardar el mensaje original

  for (int i = 0; i < 5; i++) {
    arrayMensajeOriginal[i] = claveDinamicaDescifrada[i];
  }

  for (int i = 0; i <= 4; i++) {
    for (int j = 0; j <= 8; j++) {
      if (arrayMensajeOriginal[i] == ArrayA[j]) {
        if (i == 0) {
          desplazamiento = j + claveA;
        } else {
          desplazamiento = j + arrayMensajeOriginal[i - 1] + claveA;
        }
        if (desplazamiento > 8) {
          desplazamiento -= 9;
        }
        if (desplazamiento > 8) {
          desplazamiento -= 9;
        }
        claveDinamicaDescifrada[i] = ArrayA[desplazamiento];
        break;
      }
    }
  }

  for (int i = 1; i < 5; i++) {
    claveDinamicaDescifrada[0] = claveDinamicaDescifrada[0] * 10 + claveDinamicaDescifrada[i];
  }
  Serial.print("Comparación: "); Serial.print(claveDinamicaDescifrada[0]); Serial.print(" - "); Serial.println(mensajeRecibido);
  if (claveDinamicaDescifrada[0] == mensajeRecibido.toInt()) return true;
  else return false;
}


void procesoApagado(int tipo) {
  int contadorApagado = 0;
  int T_LedApagdo = 0;
  int estadoledApagado = false;
  if (tipo == 1) {
    Serial.println("Motivo De Apagado: 1 - Switch");
    digitalWrite(principal, LOW);
    while (contadorApagado < 3) {
      if (millis() >= T_LedApagdo + 200) {
        if (estadoledApagado == false) {
          digitalWrite(ledEstado, HIGH);
          digitalWrite(direccion, HIGH);
          if (contadorApagado < 2)
            digitalWrite(sirena, HIGH);
          estadoledApagado = true;
        } else {
          digitalWrite(ledEstado, LOW);
          digitalWrite(direccion, LOW);
          digitalWrite(sirena, LOW);
          estadoledApagado = false;
          contadorApagado += 1;
        }
        T_LedApagdo = millis();
      }
    }
    digitalWrite(lm2596, LOW);
    delay(200);
    despierto = false;
    dormir();
  } else if (tipo == 2) {
    Serial.println("Motivo De Apagado: 2 - Señal de apagado remoto");
    digitalWrite(principal, LOW);
    while (contadorApagado < 2) {
      if (millis() >= T_LedApagdo + 200) {
        if (estadoledApagado == false) {
          digitalWrite(ledEstado, HIGH);
          digitalWrite(direccion, HIGH);
          if (contadorApagado < 1)
            digitalWrite(sirena, HIGH);
          estadoledApagado = true;
        } else {
          digitalWrite(ledEstado, LOW);
          digitalWrite(direccion, LOW);
          digitalWrite(sirena, LOW);
          estadoledApagado = false;
          contadorApagado += 1;
        }
        T_LedApagdo = millis();
      }
    }
  } else if (tipo == 3) {
    Serial.println("Motivo De Apagado: 3 - Desconexión, Perdida de paquetes");
    alarma();
  }
}


void alarma() {
  if (procesoAlarma == false) {
    Serial.println("Iniciando Proceso de Alarma");
    procesoAlarma = true;
  } else Serial.println("Alarma Ya Iniciada...");
}