#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ==========================================
// PINES DEL MANDO (Adaptados para ESP32)
// ==========================================
// Podés cambiar estos números según cómo conectes los cables en tu ESP32 física
const int PIN_JOY_X       = 32; 
const int PIN_JOY_Y       = 33; 
const int PIN_BTN_TORRETA = 25;
const int PIN_BTN_BOCINA  = 26;
const int PIN_BTN_DISPARO = 27;
const int PIN_BTN_LUCES   = 14;
const int PIN_BTN_RECARGA = 12;

// ==========================================
// DIRECCIÓN MAC DEL RECEPTOR (El tanque)
// ==========================================
// Más adelante reemplazaremos estas 'FF' por la dirección real de la otra placa
uint8_t macTanque[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// ==========================================
// ESTRUCTURA DEL MENSAJE 
// ==========================================
typedef struct EstructuraMensaje {
  int ejeX;
  int ejeY;
  bool modoTorreta; 
  bool btnBocina;
  bool btnDisparo;   
  bool btnLuces;
  bool btnRecarga;
} EstructuraMensaje;

EstructuraMensaje mensajeMando;

// ==========================================
// VARIABLES DE TIEMPO Y ESTADOS
// ==========================================
unsigned long ultimaTransmision = 0;
const unsigned long INTERVALO_ENVIO = 50; 

int estadoTorreta = HIGH;
int ultimoFisicoTorreta = HIGH;
unsigned long reboteTorreta = 0;
bool modoTorretaActivo = false;

int estadoDisparo = HIGH;
int ultimoFisicoDisparo = HIGH;
unsigned long reboteDisparo = 0;
bool disparoPendienteEnvio = false;

// Callback opcional: avisa internamente si el paquete salió bien de la antena
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Enviado con éxito" : "Fallo en envío");
}

void setup() {
  Serial.begin(115200);

  // Configuramos los pines de los botones con resistencias internas
  pinMode(PIN_BTN_TORRETA, INPUT_PULLUP);
  pinMode(PIN_BTN_BOCINA,  INPUT_PULLUP);
  pinMode(PIN_BTN_DISPARO, INPUT_PULLUP);
  pinMode(PIN_BTN_LUCES,   INPUT_PULLUP);
  pinMode(PIN_BTN_RECARGA, INPUT_PULLUP);
  
  // 1. Encender el Wi-Fi de la ESP32 en modo Estación
  WiFi.mode(WIFI_STA);

  // 2. Inicializar el protocolo ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error inicializando ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  
  // 3. Vincular con la dirección MAC de la otra placa
  memcpy(peerInfo.peer_addr, macTanque, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Error al registrar el dispositivo receptor");
    return;
  }

  Serial.println("--- MANDO ESP32 LISTO PARA TRANSMITIR ---");
}

void loop() {
  unsigned long actual = millis();

  // 1. DETECCIÓN LIMPIA DEL BOTÓN TORRETA (Cambio de modo)
  int lecTorreta = digitalRead(PIN_BTN_TORRETA);
  if (lecTorreta != ultimoFisicoTorreta) reboteTorreta = actual;
  if ((actual - reboteTorreta) > 50) {
    if (lecTorreta != estadoTorreta) {
      estadoTorreta = lecTorreta;
      if (estadoTorreta == LOW) { 
        modoTorretaActivo = !modoTorretaActivo;
        Serial.println(modoTorretaActivo ? ">>> MODO TORRETA <<<" : ">>> MODO CONDUCCION <<<");
      }
    }
  }
  ultimoFisicoTorreta = lecTorreta;

  // 2. DETECCIÓN LIMPIA DEL BOTÓN DISPARO (Un solo evento)
  int lecDisparo = digitalRead(PIN_BTN_DISPARO);
  if (lecDisparo != ultimoFisicoDisparo) reboteDisparo = actual;
  if ((actual - reboteDisparo) > 50) {
    if (lecDisparo != estadoDisparo) {
      estadoDisparo = lecDisparo;
      if (estadoDisparo == LOW) {
        Serial.println("[!] ¡DISPARO!");
        disparoPendienteEnvio = true; 
      }
    }
  }
  ultimoFisicoDisparo = lecDisparo;

  // 3. EMPAQUETADO Y TRANSMISIÓN WI-FI (ESP-NOW)
  if (actual - ultimaTransmision >= INTERVALO_ENVIO) {
    ultimaTransmision = actual;

    // Leer estado de la palanca y los demás botones
    mensajeMando.ejeX = analogRead(PIN_JOY_X);
    mensajeMando.ejeY = analogRead(PIN_JOY_Y);
    mensajeMando.modoTorreta = modoTorretaActivo;
    mensajeMando.btnBocina = !digitalRead(PIN_BTN_BOCINA);
    mensajeMando.btnLuces = !digitalRead(PIN_BTN_LUCES);
    mensajeMando.btnRecarga = !digitalRead(PIN_BTN_RECARGA);
    
    // Cargar el disparo y disparar el mensaje por el aire
    mensajeMando.btnDisparo = disparoPendienteEnvio;
    esp_now_send(macTanque, (uint8_t *) &mensajeMando, sizeof(mensajeMando));
    
    // Limpiar el estado de disparo para no enviar ráfagas infinitas
    if (disparoPendienteEnvio) disparoPendienteEnvio = false; 
  }
}