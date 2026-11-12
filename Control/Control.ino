#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Pines
#define JOY_X 32
#define JOY_Y 33
#define BOTON_BOCINA 26
#define BOTON_DISPARO 27

// Centro del joystick
#define CENTRO_X 1892
#define CENTRO_Y 1892

// Zona muerta
#define ZONA_MUERTA 100

// Canal de comunicacion
#define CANAL 1

// Direccion del otro ESP32
uint8_t direccion[] = {
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF
};

// Datos que vamos a enviar
struct Datos {
  int16_t motorIzquierdo;
  int16_t motorDerecho;
  uint8_t bocina;
  uint8_t disparo;
};

Datos datos;

// Funcion para leer el joystick
int leerEje(int valor, int centro) {

  if (valor > centro + ZONA_MUERTA) {
    return map(valor, centro + ZONA_MUERTA, 4095, 0, 255);
  }

  else if (valor < centro - ZONA_MUERTA) {
    return map(valor, 0, centro - ZONA_MUERTA, -255, 0);
  }

  else {
    return 0;
  }
}

void setup() {

  Serial.begin(115200);

  pinMode(BOTON_BOCINA, INPUT_PULLUP);
  pinMode(BOTON_DISPARO, INPUT_PULLUP);

  analogReadResolution(12);

  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(CANAL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al iniciar ESP-NOW");
    return;
  }

  // Agregar receptor
  esp_now_peer_info_t receptor = {};

  memcpy(receptor.peer_addr, direccion, 6);

  receptor.channel = CANAL;
  receptor.encrypt = false;

  if (esp_now_add_peer(&receptor) != ESP_OK) {
    Serial.println("Error al agregar receptor");
    return;
  }

  Serial.println("CONTROL V3 LISTO");
}

void loop() {

  // Leer joystick
  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);

  // Convertir los valores
  int giro = leerEje(x, CENTRO_X);
  int avance = -leerEje(y, CENTRO_Y);

  // Calcular velocidad de los motores
  int izquierdo = avance + giro;
  int derecho = avance - giro;

  // Evitar valores mayores a 255
  if (izquierdo > 255) izquierdo = 255;
  if (izquierdo < -255) izquierdo = -255;

  if (derecho > 255) derecho = 255;
  if (derecho < -255) derecho = -255;

  datos.motorIzquierdo = izquierdo;
  datos.motorDerecho = derecho;

  // Leer boton de bocina
  if (digitalRead(BOTON_BOCINA) == LOW) {
    datos.bocina = 1;
  } else {
    datos.bocina = 0;
  }

  // Leer boton de disparo
  if (digitalRead(BOTON_DISPARO) == LOW) {
    datos.disparo = 1;
  } else {
    datos.disparo = 0;
  }

  // Enviar datos cada 20 milisegundos
  static unsigned long tiempoAnterior = 0;

  if (millis() - tiempoAnterior >= 20) {

    esp_now_send(
      direccion,
      (uint8_t *)&datos,
      sizeof(datos)
    );

    tiempoAnterior = millis();
  }

  // Mostrar valores en el monitor serial
  static unsigned long tiempoSerial = 0;

  if (millis() - tiempoSerial >= 300) {

    Serial.print("Motor izquierdo: ");
    Serial.print(datos.motorIzquierdo);

    Serial.print(" | Motor derecho: ");
    Serial.print(datos.motorDerecho);

    Serial.print(" | Bocina: ");
    Serial.print(datos.bocina);

    Serial.print(" | Disparo: ");
    Serial.println(datos.disparo);

    tiempoSerial = millis();
  }
}
