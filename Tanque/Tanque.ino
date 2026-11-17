// VERSION 7 - TANQUE

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <math.h>
#include <string.h>

// ==================================================
// COMPONENTES
// ==================================================

#define BUZZER 25
#define LED_DISPARO 26

// L298N
#define ENA 18
#define IN1 19
#define IN2 21

#define ENB 5
#define IN3 22
#define IN4 23

// HC-SR04
#define TRIG 32
#define ECHO 34

// Motor paso a paso 28BYJ-48
#define PIN_IN1 13
#define PIN_IN2 14
#define PIN_IN3 27
#define PIN_IN4 33

// Servo de torreta
#define SERVO_TORRETA 15

// ==================================================
// CONFIGURACION
// ==================================================

#define CANAL 1
#define TIPO_RADAR 0xA1

uint8_t destino[] = {
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF
};

// ==================================================
// COMUNICACION ESP-NOW
// ==================================================

struct __attribute__((packed)) Datos {
  int16_t motorIzquierdo;
  int16_t motorDerecho;
  uint8_t bocina;
  uint8_t disparo;
  int16_t velocidadTorreta;
};

struct __attribute__((packed)) DatosRadar {
  uint8_t tipo;
  int16_t angulo;
  uint16_t distancia;
};

// Datos recibidos
volatile int16_t motorIzquierdo = 0;
volatile int16_t motorDerecho = 0;
volatile uint8_t bocina = 0;
volatile uint8_t disparo = 0;
volatile int16_t velocidadTorreta = 0;

volatile unsigned long ultimoPaquete = 0;
volatile bool recibioDatos = false;

// ==================================================
// PWM
// ==================================================

#define FRECUENCIA_MOTOR 1000
#define RESOLUCION_MOTOR 8

#define FRECUENCIA_SERVO 50
#define RESOLUCION_SERVO 16

#define CANAL_IZQUIERDO 0
#define CANAL_DERECHO 1
#define CANAL_SERVO 2

#define SERVO_MINIMO 1000
#define SERVO_CENTRO 1500
#define SERVO_MAXIMO 2000

// ==================================================
// RADAR
// ==================================================

const int pasosPorBarrido = 1024;
const int intervaloLectura = 32;

int pasosActuales = 0;
int pasosDesdeLectura = 0;

int direccionRadar = 1;
int pasoMotor = 0;

unsigned long ultimoPasoRadar = 0;

const unsigned long intervaloPaso = 8;

// Secuencia del motor paso a paso
int secuencia[4][4] = {
  {1, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 1},
  {1, 0, 0, 1}
};

// ==================================================
// SENSOR ULTRASONICO
// ==================================================

int estadoSensor = 0;

unsigned long tiempoSensor = 0;
unsigned long inicioEco = 0;

int anguloMedicion = 0;

bool sensorOcupado = false;

uint16_t ultimaDistanciaRadar = 0xFFFF;
int ultimoAnguloRadar = 0;

// ==================================================
// RECEPCION DE DATOS
// ==================================================

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void recibirDatos(
  const esp_now_recv_info_t *info,
  const uint8_t *datosRecibidos,
  int longitud
) {

#else

void recibirDatos(
  const uint8_t *mac,
  const uint8_t *datosRecibidos,
  int longitud
) {

#endif

  if (longitud != sizeof(Datos)) {
    return;
  }

  Datos recibido;

  memcpy(
    &recibido,
    datosRecibidos,
    sizeof(Datos)
  );

  motorIzquierdo = recibido.motorIzquierdo;
  motorDerecho = recibido.motorDerecho;

  bocina = recibido.bocina;
  disparo = recibido.disparo;

  velocidadTorreta = recibido.velocidadTorreta;

  ultimoPaquete = millis();
  recibioDatos = true;
}

// ==================================================
// CONFIGURAR PWM
// ==================================================

void configurarPWM() {

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcAttach(
    ENA,
    FRECUENCIA_MOTOR,
    RESOLUCION_MOTOR
  );

  ledcAttach(
    ENB,
    FRECUENCIA_MOTOR,
    RESOLUCION_MOTOR
  );

  ledcAttach(
    SERVO_TORRETA,
    FRECUENCIA_SERVO,
    RESOLUCION_SERVO
  );

#else

  ledcSetup(
    CANAL_IZQUIERDO,
    FRECUENCIA_MOTOR,
    RESOLUCION_MOTOR
  );

  ledcSetup(
    CANAL_DERECHO,
    FRECUENCIA_MOTOR,
    RESOLUCION_MOTOR
  );

  ledcSetup(
    CANAL_SERVO,
    FRECUENCIA_SERVO,
    RESOLUCION_SERVO
  );

  ledcAttachPin(ENA, CANAL_IZQUIERDO);
  ledcAttachPin(ENB, CANAL_DERECHO);

  ledcAttachPin(SERVO_TORRETA, CANAL_SERVO);

#endif
}

// ==================================================
// ESCRIBIR PWM DE LOS MOTORES
// ==================================================

void escribirMotorIzquierdo(int velocidad) {

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcWrite(ENA, velocidad);

#else

  ledcWrite(CANAL_IZQUIERDO, velocidad);

#endif
}

void escribirMotorDerecho(int velocidad) {

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcWrite(ENB, velocidad);

#else

  ledcWrite(CANAL_DERECHO, velocidad);

#endif
}

// ==================================================
// ESCRIBIR PWM DEL SERVO
// ==================================================

void escribirServo(int microsegundos) {

  microsegundos = constrain(
    microsegundos,
    SERVO_MINIMO,
    SERVO_MAXIMO
  );

  // Periodo de 20.000 us a 50 Hz
  uint32_t duty = (
    (uint32_t)microsegundos * 65535
  ) / 20000;

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcWrite(SERVO_TORRETA, duty);

#else

  ledcWrite(CANAL_SERVO, duty);

#endif
}

// ==================================================
// CONTROL DEL MOTOR IZQUIERDO
// ==================================================

void controlarIzquierdo(int velocidad) {

  velocidad = constrain(velocidad, -255, 255);

  if (velocidad > 0) {

    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);

  } else if (velocidad < 0) {

    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);

  } else {

    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
  }

  escribirMotorIzquierdo(abs(velocidad));
}

// ==================================================
// CONTROL DEL MOTOR DERECHO
// ==================================================

void controlarDerecho(int velocidad) {

  velocidad = constrain(velocidad, -255, 255);

  if (velocidad > 0) {

    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);

  } else if (velocidad < 0) {

    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);

  } else {

    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
  }

  escribirMotorDerecho(abs(velocidad));
}

// ==================================================
// CONTROL DE LA TORRETA
// ==================================================

void controlarTorreta(int velocidad) {

  velocidad = constrain(velocidad, -255, 255);

  int pulso = SERVO_CENTRO;

  if (velocidad != 0) {

    pulso = map(
      velocidad,
      -255,
      255,
      SERVO_MINIMO,
      SERVO_MAXIMO
    );
  }

  escribirServo(pulso);
}

// ==================================================
// MOVER MOTOR DEL RADAR
// ==================================================

void moverPasoRadar() {

  digitalWrite(
    PIN_IN1,
    secuencia[pasoMotor][0]
  );

  digitalWrite(
    PIN_IN2,
    secuencia[pasoMotor][1]
  );

  digitalWrite(
    PIN_IN3,
    secuencia[pasoMotor][2]
  );

  digitalWrite(
    PIN_IN4,
    secuencia[pasoMotor][3]
  );

  pasoMotor += direccionRadar;

  if (pasoMotor > 3) {
    pasoMotor = 0;
  }

  if (pasoMotor < 0) {
    pasoMotor = 3;
  }

  pasosActuales += direccionRadar;

  pasosDesdeLectura++;

  // Limites del barrido
  if (pasosActuales >= pasosPorBarrido) {

    pasosActuales = pasosPorBarrido;
    direccionRadar = -1;

  } else if (pasosActuales <= 0) {

    pasosActuales = 0;
    direccionRadar = 1;
  }
}

// ==================================================
// ENVIAR MEDICION DEL RADAR
// ==================================================

void enviarMedicionRadar(uint16_t distancia, int angulo) {

  DatosRadar datosRadar;

  datosRadar.tipo = TIPO_RADAR;
  datosRadar.angulo = angulo;
  datosRadar.distancia = distancia;

  esp_now_send(
    destino,
    (uint8_t *)&datosRadar,
    sizeof(datosRadar)
  );

  ultimaDistanciaRadar = distancia;
  ultimoAnguloRadar = angulo;
}

// ==================================================
// INICIAR MEDICION ULTRASONICA
// ==================================================

void iniciarMedicion(int angulo) {

  if (sensorOcupado) {
    return;
  }

  anguloMedicion = angulo;

  digitalWrite(TRIG, LOW);

  tiempoSensor = micros();

  estadoSensor = 1;
  sensorOcupado = true;
}

// ==================================================
// FINALIZAR MEDICION
// ==================================================

void finalizarMedicion(uint16_t distancia) {

  enviarMedicionRadar(
    distancia,
    anguloMedicion
  );

  estadoSensor = 0;
  sensorOcupado = false;
}

// ==================================================
// ACTUALIZAR SENSOR SIN BLOQUEAR
// ==================================================

void actualizarSensor() {

  unsigned long ahora = micros();

  switch (estadoSensor) {

    // Esperar antes de activar TRIG
    case 1:

      if (ahora - tiempoSensor >= 3) {

        digitalWrite(TRIG, HIGH);

        tiempoSensor = micros();

        estadoSensor = 2;
      }

      break;

    // Pulso de 10 microsegundos
    case 2:

      if (ahora - tiempoSensor >= 10) {

        digitalWrite(TRIG, LOW);

        tiempoSensor = micros();

        estadoSensor = 3;
      }

      break;

    // Esperar que llegue el eco
    case 3:

      if (digitalRead(ECHO) == HIGH) {

        inicioEco = micros();

        estadoSensor = 4;

      } else if (ahora - tiempoSensor >= 30000) {

        finalizarMedicion(0xFFFF);
      }

      break;

    // Medir duracion del eco
    case 4:

      if (digitalRead(ECHO) == LOW) {

        unsigned long duracion = micros() - inicioEco;

        // Convertir a decimas de centimetro
        uint16_t distancia = (
          duracion * 1715UL
        ) / 10000UL;

        finalizarMedicion(distancia);

      } else if (ahora - inicioEco >= 30000) {

        finalizarMedicion(0xFFFF);
      }

      break;
  }
}

// ==================================================
// ACTUALIZAR RADAR
// ==================================================

void actualizarMotorRadar() {

  if (sensorOcupado) {
    return;
  }

  if (millis() - ultimoPasoRadar < intervaloPaso) {
    return;
  }

  ultimoPasoRadar = millis();

  moverPasoRadar();

  // Tomar una medicion cada cierta cantidad de pasos
  if (pasosDesdeLectura >= intervaloLectura) {

    pasosDesdeLectura = 0;

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

// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);

  // ----------------------------------------------
  // CONFIGURAR SALIDAS
  // ----------------------------------------------

  pinMode(BUZZER, OUTPUT);
  pinMode(LED_DISPARO, OUTPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);

  digitalWrite(TRIG, LOW);

  digitalWrite(BUZZER, LOW);
  digitalWrite(LED_DISPARO, LOW);

  // ----------------------------------------------
  // CONFIGURAR PWM
  // ----------------------------------------------

  configurarPWM();

  escribirMotorIzquierdo(0);
  escribirMotorDerecho(0);

  escribirServo(SERVO_CENTRO);

  // ----------------------------------------------
  // INICIAR WIFI
  // ----------------------------------------------

  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(
    CANAL,
    WIFI_SECOND_CHAN_NONE
  );

  // ----------------------------------------------
  // INICIAR ESP-NOW
  // ----------------------------------------------

  if (esp_now_init() != ESP_OK) {

    Serial.println("Error iniciando ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(recibirDatos);

  // ----------------------------------------------
  // AGREGAR DISPOSITIVO BROADCAST
  // ----------------------------------------------

  esp_now_peer_info_t peer = {};

  memcpy(peer.peer_addr, destino, 6);

  peer.channel = CANAL;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {

    Serial.println("Error agregando receptor");
    return;
  }

  Serial.println("TANQUE V7 LISTO");

  // Iniciar primera medicion
  iniciarMedicion(0);
}

// ==================================================
// LOOP PRINCIPAL
// ==================================================

void loop() {

  // ----------------------------------------------
  // COMPROBAR COMUNICACION
  // ----------------------------------------------

  bool conectado = (
    recibioDatos &&
    millis() - ultimoPaquete < 500
  );

  // ----------------------------------------------
  // CONTROL DE MOTORES
  // ----------------------------------------------

  if (conectado) {

    controlarIzquierdo(motorIzquierdo);
    controlarDerecho(motorDerecho);

  } else {

    controlarIzquierdo(0);
    controlarDerecho(0);
  }

  // ----------------------------------------------
  // CONTROL DE LA TORRETA
  // ----------------------------------------------

  if (conectado) {

    controlarTorreta(velocidadTorreta);

  } else {

    controlarTorreta(0);
  }

  // ----------------------------------------------
  // BOCINA Y DISPARO
  // ----------------------------------------------

  if (conectado && bocina == 1) {

    digitalWrite(BUZZER, HIGH);

  } else {

    digitalWrite(BUZZER, LOW);
  }

  if (conectado && disparo == 1) {

    digitalWrite(LED_DISPARO, HIGH);

  } else {

    digitalWrite(LED_DISPARO, LOW);
  }

  // ----------------------------------------------
  // ACTUALIZAR SENSOR Y RADAR
  // ----------------------------------------------

  actualizarSensor();

  actualizarMotorRadar();

  // ----------------------------------------------
  // MONITOR SERIE
  // ----------------------------------------------

  static unsigned long anterior = 0;

  if (millis() - anterior >= 300) {

    Serial.print("Motor izq: ");
    Serial.print(motorIzquierdo);

    Serial.print(" | Motor der: ");
    Serial.print(motorDerecho);

    Serial.print(" | Torreta: ");
    Serial.print(velocidadTorreta);

    Serial.print(" | Angulo radar: ");
    Serial.print(ultimoAnguloRadar);

    Serial.print(" | Distancia: ");

    if (ultimaDistanciaRadar == 0xFFFF) {
      Serial.println("Sin lectura");
    } else {
      Serial.print(ultimaDistanciaRadar / 10.0);
      Serial.println(" cm");
    }

    anterior = millis();
  }
}