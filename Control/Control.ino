
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pines
#define JOY_X 32
#define JOY_Y 33

#define BOTON_BOCINA 26
#define BOTON_DISPARO 27
#define BOTON_RECARGA 14

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

// Municion
int municion = 10;

// Variables para el boton de disparo
int lecturaDisparoAnterior = HIGH;
int estadoDisparo = HIGH;
unsigned long tiempoDisparo = 0;

// Variables para el boton de recarga
int lecturaRecargaAnterior = HIGH;
int estadoRecarga = HIGH;
unsigned long tiempoRecarga = 0;

// Control del disparo
bool disparoAutorizado = false;

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

// Actualizar la pantalla
void actualizarLCD() {

  lcd.setCursor(0, 0);
  lcd.print("MUNICION: ");
  lcd.print(municion);
  lcd.print("/10   ");

  lcd.setCursor(0, 1);

  if (municion > 0) {
    lcd.print("ESTADO: LISTO   ");
  }

  else {
    lcd.print("SIN MUNICION    ");
  }
}

void setup() {

  Serial.begin(115200);

  // Configurar botones
  pinMode(BOTON_BOCINA, INPUT_PULLUP);
  pinMode(BOTON_DISPARO, INPUT_PULLUP);
  pinMode(BOTON_RECARGA, INPUT_PULLUP);

  analogReadResolution(12);

  // Iniciar LCD
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("INICIANDO...");
  lcd.setCursor(0, 1);
  lcd.print("CONTROL V4");

  // Iniciar WiFi
  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(CANAL, WIFI_SECOND_CHAN_NONE);

  // Iniciar ESP-NOW
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

  actualizarLCD();

  Serial.println("CONTROL V4 LISTO");
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

  // Limitar los valores
  if (izquierdo > 255) izquierdo = 255;
  if (izquierdo < -255) izquierdo = -255;

  if (derecho > 255) derecho = 255;
  if (derecho < -255) derecho = -255;

  datos.motorIzquierdo = izquierdo;
  datos.motorDerecho = derecho;

  // Leer bocina
  if (digitalRead(BOTON_BOCINA) == LOW) {
    datos.bocina = 1;
  } else {
    datos.bocina = 0;
  }

  // Leer boton de disparo
  int lecturaDisparo = digitalRead(BOTON_DISPARO);
  bool pulsacionDisparo = false;

  if (lecturaDisparo != lecturaDisparoAnterior) {
    tiempoDisparo = millis();
    lecturaDisparoAnterior = lecturaDisparo;
  }

  if (millis() - tiempoDisparo > 25) {

    if (lecturaDisparo != estadoDisparo) {

      estadoDisparo = lecturaDisparo;

      if (estadoDisparo == LOW) {
        pulsacionDisparo = true;
      }
    }
  }

  // Leer boton de recarga
  int lecturaRecarga = digitalRead(BOTON_RECARGA);
  bool pulsacionRecarga = false;

  if (lecturaRecarga != lecturaRecargaAnterior) {
    tiempoRecarga = millis();
    lecturaRecargaAnterior = lecturaRecarga;
  }

  if (millis() - tiempoRecarga > 25) {

    if (lecturaRecarga != estadoRecarga) {

      estadoRecarga = lecturaRecarga;

      if (estadoRecarga == LOW) {
        pulsacionRecarga = true;
      }
    }
  }

  // Recargar municion
  if (pulsacionRecarga) {

    municion = 10;
    disparoAutorizado = false;

    Serial.println("MUNICION RECARGADA");
  }

  // Descontar municion al apretar disparo
  if (pulsacionDisparo) {

    if (municion > 0) {

      municion--;
      disparoAutorizado = true;

    } else {

      disparoAutorizado = false;
    }
  }

  // Al soltar el boton, dejar de disparar
  if (estadoDisparo == HIGH) {
    disparoAutorizado = false;
  }

  // Comprobar si podemos disparar
  if (estadoDisparo == LOW && disparoAutorizado) {
    datos.disparo = 1;
  } else {
    datos.disparo = 0;
  }

  // Actualizar LCD
  static int municionAnterior = -1;

  if (municion != municionAnterior) {

    actualizarLCD();

    municionAnterior = municion;
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

    Serial.print(" | Municion: ");
    Serial.print(municion);

    Serial.print(" | Disparo: ");
    Serial.println(datos.disparo);

    tiempoSerial = millis();
  }
}
