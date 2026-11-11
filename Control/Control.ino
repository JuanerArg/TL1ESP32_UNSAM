
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Pin del boton
#define BOTON_BOCINA 26

// Canal de comunicacion
#define CANAL 1

// Direccion del otro ESP32 (broadcast)
uint8_t direccion[] = {
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF
};

// Dato que vamos a enviar
struct Datos {
  int bocina;
};

Datos datos;

void setup() {

  Serial.begin(115200);

  // Configurar boton
  pinMode(BOTON_BOCINA, INPUT_PULLUP);

  // Iniciar WiFi
  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(CANAL, WIFI_SECOND_CHAN_NONE);

  // Iniciar ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al iniciar ESP-NOW");
    return;
  }

  // Agregar el receptor
  esp_now_peer_info_t receptor = {};

  memcpy(receptor.peer_addr, direccion, 6);

  receptor.channel = CANAL;
  receptor.encrypt = false;

  if (esp_now_add_peer(&receptor) != ESP_OK) {
    Serial.println("Error al agregar receptor");
    return;
  }

  Serial.println("CONTROL LISTO");
}

void loop() {

  // Leer el boton
  if (digitalRead(BOTON_BOCINA) == LOW) {
    datos.bocina = 1;
  } else {
    datos.bocina = 0;
  }

  // Enviar los datos cada 20 milisegundos
  static unsigned long tiempoAnterior = 0;

  if (millis() - tiempoAnterior >= 20) {

    esp_now_send(
      direccion,
      (uint8_t *)&datos,
      sizeof(datos)
    );

    tiempoAnterior = millis();
  }
}
