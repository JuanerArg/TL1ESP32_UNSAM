#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

// Pines
#define BUZZER 25
#define LED_DISPARO 26

// Pines del L298N
#define ENA 18
#define IN1 19
#define IN2 21

#define ENB 5
#define IN3 22
#define IN4 23

// Canal de comunicacion
#define CANAL 1

// Datos que recibimos
struct Datos {
  int16_t motorIzquierdo;
  int16_t motorDerecho;
  uint8_t bocina;
  uint8_t disparo;
};

Datos datos;

unsigned long ultimoPaquete = 0;

// Funcion para recibir datos
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

  if (longitud == sizeof(Datos)) {

    memcpy(&datos, datosRecibidos, sizeof(Datos));

    ultimoPaquete = millis();
  }
}

// Controlar motor izquierdo
void controlarIzquierdo(int velocidad) {

  if (velocidad > 255) velocidad = 255;
  if (velocidad < -255) velocidad = -255;

  if (velocidad > 0) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  }

  else if (velocidad < 0) {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
  }

  else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
  }

  analogWrite(ENA, abs(velocidad));
}

// Controlar motor derecho
void controlarDerecho(int velocidad) {

  if (velocidad > 255) velocidad = 255;
  if (velocidad < -255) velocidad = -255;

  if (velocidad > 0) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
  }

  else if (velocidad < 0) {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);
  }

  else {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
  }

  analogWrite(ENB, abs(velocidad));
}

void setup() {

  Serial.begin(115200);

  // Configurar bocina y LED
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_DISPARO, OUTPUT);

  digitalWrite(BUZZER, LOW);
  digitalWrite(LED_DISPARO, LOW);

  // Configurar motores
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  analogWrite(ENA, 0);
  analogWrite(ENB, 0);

  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(CANAL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al iniciar ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(recibirDatos);

  Serial.println("TANQUE V3 LISTO");
}

void loop() {

  // Comprobar si seguimos recibiendo datos
  if (millis() - ultimoPaquete < 500) {

    controlarIzquierdo(datos.motorIzquierdo);
    controlarDerecho(datos.motorDerecho);

    // Bocina
    if (datos.bocina == 1) {
      digitalWrite(BUZZER, HIGH);
    } else {
      digitalWrite(BUZZER, LOW);
    }

    // Disparo
    if (datos.disparo == 1) {
      digitalWrite(LED_DISPARO, HIGH);
    } else {
      digitalWrite(LED_DISPARO, LOW);
    }

  }

  else {

    // Si se pierde la comunicacion, detener todo
    controlarIzquierdo(0);
    controlarDerecho(0);

    digitalWrite(BUZZER, LOW);
    digitalWrite(LED_DISPARO, LOW);
  }

  // Mostrar valores
  static unsigned long tiempoSerial = 0;

  if (millis() - tiempoSerial >= 300) {

    Serial.print("Motor izquierdo: ");
    Serial.print(datos.motorIzquierdo);

    Serial.print(" | Motor derecho: ");
    Serial.print(datos.motorDerecho);

    Serial.print(" | Disparo: ");
    Serial.println(datos.disparo);

    tiempoSerial = millis();
  }
}
