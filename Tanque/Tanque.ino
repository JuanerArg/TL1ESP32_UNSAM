
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

// Pin del buzzer
#define BUZZER 25

// Canal de comunicacion
#define CANAL 1

// Datos que recibimos del mando
struct Datos {
  int bocina;
};

Datos datos;

unsigned long ultimoPaquete = 0;

// Funcion que recibe los datos
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

  // Comprobar que los datos tengan el tamaño correcto
  if (longitud == sizeof(Datos)) {

    memcpy(&datos, datosRecibidos, sizeof(Datos));

    ultimoPaquete = millis();

    // Encender o apagar la bocina
    if (datos.bocina == 1) {
      digitalWrite(BUZZER, HIGH);
    } else {
      digitalWrite(BUZZER, LOW);
    }
  }
}

void setup() {

  Serial.begin(115200);

  // Configurar buzzer
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  // Iniciar WiFi
  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(CANAL, WIFI_SECOND_CHAN_NONE);

  // Iniciar ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al iniciar ESP-NOW");
    return;
  }

  // Configurar la recepcion de datos
  esp_now_register_recv_cb(recibirDatos);

  Serial.println("TANQUE LISTO");
}

void loop() {

  // Si pasan 500 ms sin recibir datos, apagar la bocina
  if (millis() - ultimoPaquete >= 500) {
    digitalWrite(BUZZER, LOW);
  }
}
