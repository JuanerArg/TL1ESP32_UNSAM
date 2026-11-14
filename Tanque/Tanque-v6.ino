
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

// =====================================
// PINES
// =====================================

// Motores
#define ENA 18
#define IN1 19
#define IN2 21

#define ENB 5
#define IN3 22
#define IN4 23

// Bocina y disparo
#define BUZZER 25
#define LED_DISPARO 26

// Sensor ultrasonico
#define TRIG 32
#define ECHO 34

// Motor paso a paso
#define PIN_IN1 13
#define PIN_IN2 14
#define PIN_IN3 27
#define PIN_IN4 33

// =====================================
// ESP-NOW
// =====================================

uint8_t direccionControl[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

struct Datos {
  int16_t motorIzquierdo;
  int16_t motorDerecho;
  uint8_t bocina;
  uint8_t disparo;
};

struct __attribute__((packed)) DatosRadar {
  uint8_t tipo;
  int16_t angulo;
  uint16_t distancia;
};

#define TIPO_RADAR 0xA1

Datos datosRecibidos;

unsigned long ultimoPaquete = 0;
bool hayPaquete = false;

// =====================================
// MOTOR DEL RADAR
// =====================================

const int pasosPorBarrido = 1024;
const int intervaloPaso = 3;
const int intervaloLectura = 32;

int pasosActuales = 0;
int direccionRadar = 1;
int pasoMotor = 0;

unsigned long tiempoPaso = 0;

// Secuencia de los cuatro cables del motor
int secuencia[4][4] = {
  {1, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 1},
  {1, 0, 0, 1}
};

// =====================================
// SENSOR ULTRASONICO
// =====================================

int estadoSensor = 0;

unsigned long tiempoSensor = 0;
unsigned long inicioEco = 0;

int anguloMedicion = 0;

bool sensorOcupado = false;

// =====================================
// RECIBIR DATOS DEL CONTROL
// =====================================

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void recibirDatos(const esp_now_recv_info_t *info,
                  const uint8_t *datos, int longitud) {

#else

void recibirDatos(const uint8_t *mac,
                  const uint8_t *datos, int longitud) {

#endif

  if (longitud == sizeof(Datos)) {

    memcpy(&datosRecibidos, datos, sizeof(Datos));

    ultimoPaquete = millis();
    hayPaquete = true;
  }
}

// =====================================
// CONTROLAR MOTORES
// =====================================

void controlarMotor(int velocidad, int pinA, int pinB, int pinPWM) {

  if (velocidad > 0) {
    digitalWrite(pinA, HIGH);
    digitalWrite(pinB, LOW);
  }
  else if (velocidad < 0) {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, HIGH);
  }
  else {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
  }

  analogWrite(pinPWM, abs(velocidad));
}

void actualizarMotores() {

  if (hayPaquete && millis() - ultimoPaquete < 500) {

    controlarMotor(
      datosRecibidos.motorIzquierdo,
      IN1, IN2, ENA
    );

    controlarMotor(
      datosRecibidos.motorDerecho,
      IN3, IN4, ENB
    );

    digitalWrite(BUZZER, datosRecibidos.bocina);
    digitalWrite(LED_DISPARO, datosRecibidos.disparo);
  }
  else {

    controlarMotor(0, IN1, IN2, ENA);
    controlarMotor(0, IN3, IN4, ENB);

    digitalWrite(BUZZER, LOW);
    digitalWrite(LED_DISPARO, LOW);
  }
}

// =====================================
// MOVER MOTOR DEL RADAR
// =====================================

void moverMotorRadar() {

  digitalWrite(PIN_IN1, secuencia[pasoMotor][0]);
  digitalWrite(PIN_IN2, secuencia[pasoMotor][1]);
  digitalWrite(PIN_IN3, secuencia[pasoMotor][2]);
  digitalWrite(PIN_IN4, secuencia[pasoMotor][3]);

  pasoMotor = pasoMotor + direccionRadar;

  if (pasoMotor > 3) {
    pasoMotor = 0;
  }

  if (pasoMotor < 0) {
    pasoMotor = 3;
  }

  pasosActuales = pasosActuales + direccionRadar;

  if (pasosActuales >= pasosPorBarrido) {
    pasosActuales = pasosPorBarrido;
    direccionRadar = -1;
  }

  if (pasosActuales <= 0) {
    pasosActuales = 0;
    direccionRadar = 1;
  }
}

// =====================================
// INICIAR MEDICION
// =====================================

void iniciarMedicion(int angulo) {

  anguloMedicion = angulo;

  digitalWrite(TRIG, HIGH);

  tiempoSensor = micros();
  estadoSensor = 1;

  sensorOcupado = true;
}

// =====================================
// ENVIAR MEDICION
// =====================================

void enviarMedicion(uint16_t distancia) {

  DatosRadar medicion;

  medicion.tipo = TIPO_RADAR;
  medicion.angulo = anguloMedicion;
  medicion.distancia = distancia;

  esp_now_send(
    direccionControl,
    (uint8_t *)&medicion,
    sizeof(medicion)
  );
}

// =====================================
// ACTUALIZAR SENSOR SIN BLOQUEAR
// =====================================

void actualizarSensor() {

  unsigned long ahora = micros();

  // TERMINAR EL PULSO DEL TRIG

  if (estadoSensor == 1) {

    if (ahora - tiempoSensor >= 10) {

      digitalWrite(TRIG, LOW);

      tiempoSensor = ahora;
      estadoSensor = 2;
    }
  }

  // ESPERAR QUE LLEGUE EL ECO

  else if (estadoSensor == 2) {

    if (digitalRead(ECHO) == HIGH) {

      inicioEco = ahora;
      estadoSensor = 3;
    }

    else if (ahora - tiempoSensor >= 30000) {

      enviarMedicion(0xFFFF);

      estadoSensor = 0;
      sensorOcupado = false;
    }
  }

  // ESPERAR QUE TERMINE EL ECO

  else if (estadoSensor == 3) {

    if (digitalRead(ECHO) == LOW) {

      unsigned long duracion = ahora - inicioEco;

      float distancia = duracion * 0.0343 / 2.0;

      if (distancia > 0 && distancia <= 6553.4) {

        uint16_t valor = distancia * 10;

        enviarMedicion(valor);
      }
      else {
        enviarMedicion(0xFFFF);
      }

      estadoSensor = 0;
      sensorOcupado = false;
    }

    else if (ahora - inicioEco >= 30000) {

      enviarMedicion(0xFFFF);

      estadoSensor = 0;
      sensorOcupado = false;
    }
  }
}

// =====================================
// ACTUALIZAR RADAR
// =====================================

void actualizarRadar() {

  // Primero se termina la medicion actual

  actualizarSensor();

  // No mover el motor mientras mide

  if (!sensorOcupado) {

    if (millis() - tiempoPaso >= intervaloPaso) {

      tiempoPaso = millis();

      moverMotorRadar();

      // Cada cierta cantidad de pasos mide

      if (pasosActuales % intervaloLectura == 0) {

        int angulo = map(
          pasosActuales,
          0,
          pasosPorBarrido,
          0,
          180
        );

        iniciarMedicion(angulo);
      }
    }
  }
}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(BUZZER, OUTPUT);
  pinMode(LED_DISPARO, OUTPUT);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);

  digitalWrite(TRIG, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al iniciar ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(recibirDatos);

  esp_now_peer_info_t peerInfo = {};

  memcpy(peerInfo.peer_addr, direccionControl, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Error al agregar CONTROL");
    return;
  }

  Serial.println("TANQUE V6 LISTO");
}

// =====================================
// LOOP
// =====================================

void loop() {

  actualizarMotores();
  actualizarRadar();
}
