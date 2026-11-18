// VERSION 7 - CONTROL

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================================================
// COMPONENTES
// ==================================================

// LCD 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);

// OLED 128x64
#define ANCHO 128
#define ALTO 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 oled(ANCHO, ALTO, &Wire, OLED_RESET);

bool oledDisponible = false;
bool radarActivo = false;

// ==================================================
// JOYSTICKS
// ==================================================

// Joystick principal
#define JOY_X 32
#define JOY_Y 33

// Joystick de la torreta
#define JOY_TORRETA_X 34

// Calibracion
#define CENTRO_X 1892
#define CENTRO_Y 1892
#define CENTRO_TORRETA 1892

#define ZONA_MUERTA 100

// ==================================================
// BOTONES
// ==================================================

#define BOTON_BOCINA 26
#define BOTON_DISPARO 27
#define BOTON_RECARGA 14
#define BOTON_RADAR 25

// ==================================================
// MUNICION
// ==================================================

#define MUNICION_MAXIMA 10

int municion = MUNICION_MAXIMA;

// ==================================================
// COMUNICACION ESP-NOW
// ==================================================

#define CANAL 1

uint8_t destino[] = {
  0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF
};

// Datos enviados al tanque
struct __attribute__((packed)) Datos {
  int16_t motorIzquierdo;
  int16_t motorDerecho;
  uint8_t bocina;
  uint8_t disparo;
  int16_t velocidadTorreta;
};

// Datos recibidos del radar
struct __attribute__((packed)) DatosRadar {
  uint8_t tipo;
  int16_t angulo;
  uint16_t distancia;
};

#define TIPO_RADAR 0xA1

Datos datos;

// ==================================================
// DATOS DEL RADAR
// ==================================================

volatile uint16_t distancias[181] = {};
volatile bool lecturasValidas[181] = {};

volatile int anguloRadar = 0;
volatile uint16_t ultimaDistancia = 0xFFFF;

// ==================================================
// ESTADOS DE LOS BOTONES
// ==================================================

// Disparo
bool estadoDisparo = HIGH;
bool ultimaLecturaDisparo = HIGH;
unsigned long cambioDisparo = 0;

// Recarga
bool estadoRecarga = HIGH;
bool ultimaLecturaRecarga = HIGH;
unsigned long cambioRecarga = 0;

// Radar
bool estadoRadar = HIGH;
bool ultimaLecturaRadar = HIGH;
unsigned long cambioRadar = 0;

// ==================================================
// RECEPCION DE DATOS DEL RADAR
// ==================================================

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void recibirRadar(
  const esp_now_recv_info_t *info,
  const uint8_t *datosRecibidos,
  int longitud
) {

#else

void recibirRadar(
  const uint8_t *mac,
  const uint8_t *datosRecibidos,
  int longitud
) {

#endif

  if (longitud != sizeof(DatosRadar)) {
    return;
  }

  DatosRadar recibido;

  memcpy(
    &recibido,
    datosRecibidos,
    sizeof(DatosRadar)
  );

  if (recibido.tipo != TIPO_RADAR) {
    return;
  }

  int angulo = constrain(recibido.angulo, 0, 180);

  anguloRadar = angulo;
  ultimaDistancia = recibido.distancia;

  if (recibido.distancia == 0xFFFF) {

    lecturasValidas[angulo] = false;

  } else {

    distancias[angulo] = recibido.distancia;
    lecturasValidas[angulo] = true;
  }
}

// ==================================================
// DETECCION DE PULSACIONES CON ANTIRREBOTE
// ==================================================

bool detectarPulsacion(
  int pin,
  bool &estadoEstable,
  bool &ultimaLectura,
  unsigned long &ultimoCambio
) {

  bool lectura = digitalRead(pin);

  if (lectura != ultimaLectura) {
    ultimaLectura = lectura;
    ultimoCambio = millis();
  }

  if (
    millis() - ultimoCambio > 25 &&
    lectura != estadoEstable
  ) {

    estadoEstable = lectura;

    if (estadoEstable == LOW) {
      return true;
    }
  }

  return false;
}

// ==================================================
// CONVERTIR JOYSTICK EN VELOCIDAD
// ==================================================

int leerEje(int lectura, int centro) {

  if (abs(lectura - centro) <= ZONA_MUERTA) {
    return 0;
  }

  int velocidad;

  if (lectura > centro) {

    velocidad = map(
      lectura,
      centro,
      4095,
      0,
      255
    );

  } else {

    velocidad = map(
      lectura,
      0,
      centro,
      -255,
      0
    );
  }

  return constrain(velocidad, -255, 255);
}

// ==================================================
// ACTUALIZAR LCD
// ==================================================

void actualizarLCD() {

  lcd.setCursor(0, 0);

  lcd.print("MUNICION: ");
  lcd.print(municion);
  lcd.print("/10      ");

  lcd.setCursor(0, 1);

  if (municion > 0) {
    lcd.print("ESTADO: LISTO   ");
  } else {
    lcd.print("SIN MUNICION    ");
  }
}

// ==================================================
// DIBUJAR RADAR OLED
// ==================================================

void actualizarRadar() {

  oled.clearDisplay();

  if (!radarActivo) {

    oled.setTextSize(1);

    oled.setCursor(25, 20);
    oled.print("RADAR");

    oled.setCursor(25, 35);
    oled.print("PAUSADO");

    oled.display();

    return;
  }

  int cx = 64;
  int cy = 63;
  int radio = 51;

  // Informacion superior
  oled.setTextSize(1);
  oled.setCursor(0, 0);

  oled.print("A:");
  oled.print(anguloRadar);

  oled.print(" D:");

  if (ultimaDistancia == 0xFFFF) {
    oled.print("--");
  } else {
    oled.print(ultimaDistancia / 10);
  }

  oled.print("cm");

  // Circulos
  oled.drawCircle(cx, cy, radio, SSD1306_WHITE);
  oled.drawCircle(cx, cy, 34, SSD1306_WHITE);
  oled.drawCircle(cx, cy, 17, SSD1306_WHITE);

  // Lineas de referencia
  oled.drawLine(cx, cy, cx, cy - radio, SSD1306_WHITE);
  oled.drawLine(cx, cy, 13, cy, SSD1306_WHITE);
  oled.drawLine(cx, cy, 115, cy, SSD1306_WHITE);

  // Obstaculos
  for (int a = 0; a <= 180; a++) {

    if (!lecturasValidas[a]) {
      continue;
    }

    float distanciaCm = distancias[a] / 10.0;

    float distanciaLimitada = constrain(
      distanciaCm,
      0,
      200
    );

    int r = (int)(
      distanciaLimitada * radio / 200.0
    );

    float rad = a * PI / 180.0;

    int x = cx + r * cos(rad);
    int y = cy - r * sin(rad);

    oled.fillCircle(x, y, 2, SSD1306_WHITE);
  }

  // Linea de barrido
  float rad = anguloRadar * PI / 180.0;

  int x = cx + radio * cos(rad);
  int y = cy - radio * sin(rad);

  oled.drawLine(cx, cy, x, y, SSD1306_WHITE);

  oled.display();
}

// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);

  // Botones
  pinMode(BOTON_BOCINA, INPUT_PULLUP);
  pinMode(BOTON_DISPARO, INPUT_PULLUP);
  pinMode(BOTON_RECARGA, INPUT_PULLUP);
  pinMode(BOTON_RADAR, INPUT_PULLUP);

  analogReadResolution(12);

  // I2C
  Wire.begin(21, 22);

  // LCD
  lcd.init();
  lcd.backlight();

  actualizarLCD();

  // OLED
  if (oled.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  )) {

    oledDisponible = true;

    actualizarRadar();

    Serial.println("OLED iniciado correctamente");

  } else {

    Serial.println("Error iniciando OLED");
  }

  // ESP-NOW
  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(
    CANAL,
    WIFI_SECOND_CHAN_NONE
  );

  if (esp_now_init() != ESP_OK) {

    Serial.println("Error iniciando ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(recibirRadar);

  // Dispositivo broadcast
  esp_now_peer_info_t peer = {};

  memcpy(peer.peer_addr, destino, 6);

  peer.channel = CANAL;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {

    Serial.println("Error agregando receptor");
    return;
  }

  Serial.println("MANDO V7 LISTO");
}

// ==================================================
// LOOP PRINCIPAL
// ==================================================

void loop() {

  // ----------------------------------------------
  // LEER JOYSTICK PRINCIPAL
  // ----------------------------------------------

  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);

  int giro = leerEje(x, CENTRO_X);
  int avance = -leerEje(y, CENTRO_Y);

  // Mezcla diferencial
  int izquierdo = avance + giro;
  int derecho = avance - giro;

  int maximo = max(
    abs(izquierdo),
    abs(derecho)
  );

  if (maximo > 255) {

    izquierdo = izquierdo * 255 / maximo;
    derecho = derecho * 255 / maximo;
  }

  // ----------------------------------------------
  // LEER JOYSTICK DE LA TORRETA
  // ----------------------------------------------

  int lecturaTorreta = analogRead(JOY_TORRETA_X);

  int velocidadTorreta = leerEje(
    lecturaTorreta,
    CENTRO_TORRETA
  );

  // ----------------------------------------------
  // LEER BOTONES
  // ----------------------------------------------

  bool pulsacionDisparo = detectarPulsacion(
    BOTON_DISPARO,
    estadoDisparo,
    ultimaLecturaDisparo,
    cambioDisparo
  );

  bool pulsacionRecarga = detectarPulsacion(
    BOTON_RECARGA,
    estadoRecarga,
    ultimaLecturaRecarga,
    cambioRecarga
  );

  bool pulsacionRadar = detectarPulsacion(
    BOTON_RADAR,
    estadoRadar,
    ultimaLecturaRadar,
    cambioRadar
  );

  // ----------------------------------------------
  // ACTIVAR / DESACTIVAR RADAR
  // ----------------------------------------------

  if (pulsacionRadar && oledDisponible) {

    radarActivo = !radarActivo;

    actualizarRadar();

    Serial.println(
      radarActivo
        ? "RADAR ACTIVADO"
        : "RADAR PAUSADO"
    );
  }

  // ----------------------------------------------
  // MUNICION Y RECARGA
  // ----------------------------------------------

  if (pulsacionRecarga) {

    municion = MUNICION_MAXIMA;

    actualizarLCD();

    Serial.println("MUNICION RECARGADA");

  }
  else if (pulsacionDisparo && municion > 0) {

    municion--;

    actualizarLCD();

    Serial.println("DISPARO");
  }

  // ----------------------------------------------
  // PREPARAR DATOS PARA EL TANQUE
  // ----------------------------------------------

  datos.motorIzquierdo = izquierdo;
  datos.motorDerecho = derecho;

  datos.bocina = (
    digitalRead(BOTON_BOCINA) == LOW
  ) ? 1 : 0;

  datos.disparo = (
    digitalRead(BOTON_DISPARO) == LOW &&
    municion > 0
  ) ? 1 : 0;

  datos.velocidadTorreta = velocidadTorreta;

  // ----------------------------------------------
  // ENVIAR CONTROL CADA 20 MS
  // ----------------------------------------------

  static unsigned long ultimoEnvio = 0;

  if (millis() - ultimoEnvio >= 20) {

    esp_now_send(
      destino,
      (uint8_t *)&datos,
      sizeof(datos)
    );

    ultimoEnvio = millis();
  }

  // ----------------------------------------------
  // ACTUALIZAR OLED
  // ----------------------------------------------

  static unsigned long anteriorRadar = 0;

  if (
    oledDisponible &&
    radarActivo &&
    millis() - anteriorRadar >= 200
  ) {

    actualizarRadar();

    anteriorRadar = millis();
  }

  // ----------------------------------------------
  // MONITOR SERIE
  // ----------------------------------------------

  static unsigned long anterior = 0;

  if (millis() - anterior >= 300) {

    Serial.print("Municion: ");
    Serial.print(municion);

    Serial.print(" | Motor izq: ");
    Serial.print(izquierdo);

    Serial.print(" | Motor der: ");
    Serial.print(derecho);

    Serial.print(" | Torreta: ");
    Serial.println(velocidadTorreta);

    anterior = millis();
  }
}