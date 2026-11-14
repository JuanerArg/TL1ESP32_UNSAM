
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <string.h>

// =====================================
// PANTALLAS
// =====================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define ANCHO 128
#define ALTO 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 oled(ANCHO, ALTO, &Wire, OLED_RESET);

// =====================================
// JOYSTICK
// =====================================

#define JOY_X 32
#define JOY_Y 33

const int CENTRO_X = 1892;
const int CENTRO_Y = 1892;
const int ZONA_MUERTA = 100;

// =====================================
// BOTONES
// =====================================

#define BOTON_BUZZER 26
#define BOTON_DISPARO 27
#define BOTON_RECARGA 14
#define BOTON_RADAR 25

// =====================================
// ESP-NOW
// =====================================

uint8_t direccionTanque[] = {
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

Datos datos;

// =====================================
// MUNICION
// =====================================

int municion = 10;

bool disparoPermitido = false;
bool disparoAnterior = HIGH;
bool estadoDisparo = HIGH;

bool recargaAnterior = HIGH;
bool radarAnterior = HIGH;

bool radarActivo = false;

// =====================================
// TIEMPOS
// =====================================

unsigned long tiempoDisparo = 0;
unsigned long tiempoRecarga = 0;
unsigned long tiempoBotonRadar = 0;

unsigned long tiempoLCD = 0;
unsigned long tiempoOLED = 0;
unsigned long ultimoEnvio = 0;

// =====================================
// DATOS DEL RADAR
// =====================================

volatile uint16_t distancias[181] = {};
volatile bool lecturasValidas[181] = {};

volatile int anguloRadar = 0;

// =====================================
// RECIBIR MEDICIONES DEL TANQUE
// =====================================

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void recibirDatos(const esp_now_recv_info_t *info,
                  const uint8_t *datosRecibidos, int longitud) {

#else

void recibirDatos(const uint8_t *mac,
                  const uint8_t *datosRecibidos, int longitud) {

#endif

  if (longitud == sizeof(DatosRadar)) {

    DatosRadar medicion;

    memcpy(&medicion, datosRecibidos, sizeof(medicion));

    if (medicion.tipo == TIPO_RADAR) {

      if (medicion.angulo >= 0 && medicion.angulo <= 180) {

        anguloRadar = medicion.angulo;

        if (medicion.distancia != 0xFFFF) {

          distancias[medicion.angulo] = medicion.distancia;
          lecturasValidas[medicion.angulo] = true;
        }
        else {
          lecturasValidas[medicion.angulo] = false;
        }
      }
    }
  }
}

// =====================================
// LEER JOYSTICK
// =====================================

int leerEje(int valor, int centro) {

  int salida = map(valor, 0, 4095, -255, 255);

  if (abs(valor - centro) < ZONA_MUERTA) {
    salida = 0;
  }

  salida = constrain(salida, -255, 255);

  return salida;
}

// =====================================
// ACTUALIZAR MOVIMIENTO
// =====================================

void actualizarMovimiento() {

  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);

  int giro = leerEje(x, CENTRO_X);
  int avance = -leerEje(y, CENTRO_Y);

  datos.motorIzquierdo = avance + giro;
  datos.motorDerecho = avance - giro;

  datos.motorIzquierdo = constrain(datos.motorIzquierdo, -255, 255);
  datos.motorDerecho = constrain(datos.motorDerecho, -255, 255);
}

// =====================================
// ACTUALIZAR BOTONES
// =====================================

void actualizarBotones() {

  bool bocina = digitalRead(BOTON_BUZZER) == LOW;
  int lecturaDisparo = digitalRead(BOTON_DISPARO);
  int lecturaRecarga = digitalRead(BOTON_RECARGA);
  bool lecturaRadar = digitalRead(BOTON_RADAR) == LOW;

  datos.bocina = bocina;

  // DISPARO

  if (lecturaDisparo != disparoAnterior) {

    tiempoDisparo = millis();
    disparoAnterior = lecturaDisparo;
  }

  if (millis() - tiempoDisparo >= 25) {

    if (lecturaDisparo != estadoDisparo) {

      estadoDisparo = lecturaDisparo;

      if (estadoDisparo == LOW) {

        if (municion > 0) {
          municion--;
          disparoPermitido = true;
        }
        else {
          disparoPermitido = false;
        }
      }
      else {
        disparoPermitido = false;
      }
    }
  }

  datos.disparo = (estadoDisparo == LOW && disparoPermitido);

  // RECARGA

  if (lecturaRecarga != recargaAnterior) {

    tiempoRecarga = millis();
    recargaAnterior = lecturaRecarga;
  }

  static int estadoRecarga = HIGH;

  if (millis() - tiempoRecarga >= 25) {

    if (lecturaRecarga != estadoRecarga) {

      estadoRecarga = lecturaRecarga;

      if (estadoRecarga == LOW) {
        municion = 10;
      }
    }
  }

  // RADAR

  if (lecturaRadar && !radarAnterior) {

    if (millis() - tiempoBotonRadar >= 200) {

      radarActivo = !radarActivo;
      tiempoBotonRadar = millis();
    }
  }

  radarAnterior = lecturaRadar;
}

// =====================================
// ACTUALIZAR LCD
// =====================================

void actualizarLCD() {

  if (millis() - tiempoLCD >= 200) {

    tiempoLCD = millis();

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
}

// =====================================
// DIBUJAR RADAR EN OLED
// =====================================

void dibujarRadar() {

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  if (!radarActivo) {

    oled.setTextSize(1);
    oled.setCursor(25, 25);
    oled.println("RADAR PAUSADO");

    oled.display();
    return;
  }

  int centroX = 64;
  int centroY = 61;
  int radioMaximo = 55;

  // ARCOS

  for (int radio = 15; radio <= 55; radio += 20) {

    for (int angulo = 0; angulo <= 180; angulo++) {

      float radianes = angulo * PI / 180.0;

      int x = centroX + radio * cos(radianes);
      int y = centroY - radio * sin(radianes);

      oled.drawPixel(x, y, SSD1306_WHITE);
    }
  }

  // LINEAS

  for (int angulo = 0; angulo <= 180; angulo += 30) {

    float radianes = angulo * PI / 180.0;

    int x = centroX + radioMaximo * cos(radianes);
    int y = centroY - radioMaximo * sin(radianes);

    oled.drawLine(centroX, centroY, x, y, SSD1306_WHITE);
  }

  // OBJETOS DETECTADOS

  for (int i = 0; i <= 180; i++) {

    if (lecturasValidas[i]) {

      float distancia = distancias[i] / 10.0;

      if (distancia <= 200) {

        int radio = map(distancia, 0, 200, 0, radioMaximo);

        float radianes = i * PI / 180.0;

        int x = centroX + radio * cos(radianes);
        int y = centroY - radio * sin(radianes);

        oled.fillCircle(x, y, 2, SSD1306_WHITE);
      }
    }
  }

  // POSICION ACTUAL DEL SENSOR

  float radianes = anguloRadar * PI / 180.0;

  int x = centroX + radioMaximo * cos(radianes);
  int y = centroY - radioMaximo * sin(radianes);

  oled.drawLine(centroX, centroY, x, y, SSD1306_WHITE);

  oled.display();
}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  pinMode(BOTON_BUZZER, INPUT_PULLUP);
  pinMode(BOTON_DISPARO, INPUT_PULLUP);
  pinMode(BOTON_RECARGA, INPUT_PULLUP);
  pinMode(BOTON_RADAR, INPUT_PULLUP);

  analogReadResolution(12);

  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("Error al iniciar OLED");
  }

  lcd.setCursor(0, 0);
  lcd.print("CONTROL V6");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error al iniciar ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(recibirDatos);

  esp_now_peer_info_t peerInfo = {};

  memcpy(peerInfo.peer_addr, direccionTanque, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Error al agregar TANQUE");
    return;
  }

  Serial.println("CONTROL V6 LISTO");
}

// =====================================
// LOOP
// =====================================

void loop() {

  actualizarMovimiento();
  actualizarBotones();

  // ENVIAR CONTROLES CADA 20 MS

  if (millis() - ultimoEnvio >= 20) {

    ultimoEnvio = millis();

    esp_now_send(
      direccionTanque,
      (uint8_t *)&datos,
      sizeof(datos)
    );
  }

  actualizarLCD();

  // ACTUALIZAR OLED CADA 100 MS

  if (millis() - tiempoOLED >= 100) {

    tiempoOLED = millis();

    dibujarRadar();
  }
}
