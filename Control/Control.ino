#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <esp_idf_version.h>
#include <esp_system.h>

const int PIN_EJE_X = 32;
const int PIN_EJE_Y = 33;
const int CANTIDAD_BOTONES = 5;
// Orden: disparo, recarga, bocina, auxiliar y clic del joystick.
const int pinesBotones[CANTIDAD_BOTONES] = {27, 18, 19, 21, 26};
const char* nombresBotones[CANTIDAD_BOTONES] = {
  "DISPARO", "RECARGA", "BOCINA", "AUXILIAR", "CAMBIO DE MODO"
};
const int CANAL_RADIO = 1;
const int CENTRO_X = 1888;
const int CENTRO_Y = 1888;
const int ZONA_MUERTA = 100;
const bool INVERTIR_EJE_X = false;
const bool INVERTIR_EJE_Y = false;
const unsigned long ANTIRREBOTE_MS = 30;
const unsigned long INTERVALO_ENVIO_MS = 20;

// Esta estructura debe ser IDENTICA en el mando y en el tanque.
// int16_t guarda velocidades negativas y positivas. uint8_t guarda 0 o 1.
// uint32_t guarda los contadores de pulsaciones y el identificador de arranque.
struct DatosDelMando {
  uint32_t identificadorDeArranque;
  uint32_t pulsacionesDisparo;
  uint32_t pulsacionesRecarga;
  uint32_t pulsacionesAuxiliar;
  uint32_t pulsacionesCambioModo;
  int16_t motorIzquierdo;
  int16_t motorDerecho;
  uint8_t bocinaApretada;
  uint8_t disparoApretado;
  uint8_t recargaApretada;
  uint8_t auxiliarApretado;
  uint8_t joystickApretado;
};

static_assert(sizeof(DatosDelMando) == 32, "Revisar formato compartido");

DatosDelMando datosParaEnviar = {};
int ultimaLecturaDelPin[CANTIDAD_BOTONES];
int estadoConfirmado[CANTIDAD_BOTONES];
unsigned long momentoUltimoCambio[CANTIDAD_BOTONES] = {};
unsigned long momentoUltimoEnvio = 0;
unsigned long momentoUltimoDiagnostico = 0;
bool radioLista = false;
volatile bool envioEnCurso = false;
esp_err_t resultadoUltimoEnvio = ESP_OK;

// Conservamos la difusion del proyecto original. Todos los receptores del
// mismo canal reciben el paquete. Para el prototipo final se puede fijar una MAC.
uint8_t direccionDeDestino[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void mostrarPorPuertoSerie(const char* texto) {
  size_t cantidadDeBytes = strlen(texto);
  if (Serial.availableForWrite() >= (int)cantidadDeBytes) {
    Serial.write((const uint8_t*)texto, cantidadDeBytes);
  }
}

// La biblioteca llama a esta funcion cuando termina el envio anterior.
// La firma cambio en ESP-IDF 5.5, usado por Arduino-ESP32 3.3.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void alTerminarEnvio(const wifi_tx_info_t* informacion,
                    esp_now_send_status_t estado) {
#else
void alTerminarEnvio(const uint8_t* direccion,
                    esp_now_send_status_t estado) {
#endif
  envioEnCurso = false;
}

void registrarPulsacion(int indiceBoton) {
  if (indiceBoton == 0) datosParaEnviar.pulsacionesDisparo++;
  if (indiceBoton == 1) datosParaEnviar.pulsacionesRecarga++;
  if (indiceBoton == 3) datosParaEnviar.pulsacionesAuxiliar++;
  if (indiceBoton == 4) datosParaEnviar.pulsacionesCambioModo++;

  char mensaje[48];
  snprintf(mensaje, sizeof(mensaje), "Pulsacion: %s\n",
           nombresBotones[indiceBoton]);
  mostrarPorPuertoSerie(mensaje);
}

void leerBotones(unsigned long tiempoActual) {
  for (int indiceBoton = 0; indiceBoton < CANTIDAD_BOTONES; indiceBoton++) {
    int lectura = digitalRead(pinesBotones[indiceBoton]);
    if (lectura != ultimaLecturaDelPin[indiceBoton]) {
      ultimaLecturaDelPin[indiceBoton] = lectura;
      momentoUltimoCambio[indiceBoton] = tiempoActual;
    }

    bool pasoElAntirrebote =
      tiempoActual - momentoUltimoCambio[indiceBoton] >= ANTIRREBOTE_MS;
    bool cambioElEstado = lectura != estadoConfirmado[indiceBoton];
    if (pasoElAntirrebote && cambioElEstado) {
      estadoConfirmado[indiceBoton] = lectura;
      // HIGH a LOW significa una nueva pulsacion. Se cuenta una sola vez.
      if (lectura == LOW) registrarPulsacion(indiceBoton);
    }
  }

  // Tambien enviamos el estado sostenido, incluida la liberacion del boton.
  datosParaEnviar.disparoApretado = estadoConfirmado[0] == LOW;
  datosParaEnviar.recargaApretada = estadoConfirmado[1] == LOW;
  datosParaEnviar.bocinaApretada = estadoConfirmado[2] == LOW;
  datosParaEnviar.auxiliarApretado = estadoConfirmado[3] == LOW;
  datosParaEnviar.joystickApretado = estadoConfirmado[4] == LOW;
}

int convertirEjeEnVelocidad(int lectura, int centro) {
  int limiteInferior = centro - ZONA_MUERTA;
  int limiteSuperior = centro + ZONA_MUERTA;
  if (lectura >= limiteInferior && lectura <= limiteSuperior) return 0;
  if (lectura > limiteSuperior) {
    return constrain(map(lectura, limiteSuperior, 4095, 0, 255), 0, 255);
  }
  return constrain(map(lectura, 0, limiteInferior, -255, 0), -255, 0);
}

void prepararMovimiento() {
  int giro = convertirEjeEnVelocidad(analogRead(PIN_EJE_X), CENTRO_X);
  int avance = convertirEjeEnVelocidad(analogRead(PIN_EJE_Y), CENTRO_Y);
  if (INVERTIR_EJE_X) giro = -giro;
  if (INVERTIR_EJE_Y) avance = -avance;

  // Mezcla diferencial: para girar, un lado avanza mas que el otro.
  int velocidadIzquierda = avance + giro;
  int velocidadDerecha = avance - giro;
  int mayorVelocidad = max(abs(velocidadIzquierda), abs(velocidadDerecha));
  if (mayorVelocidad > 255) {
    velocidadIzquierda = velocidadIzquierda * 255 / mayorVelocidad;
    velocidadDerecha = velocidadDerecha * 255 / mayorVelocidad;
  }
  datosParaEnviar.motorIzquierdo = velocidadIzquierda;
  datosParaEnviar.motorDerecho = velocidadDerecha;
}

void enviarControles(unsigned long tiempoActual) {
  if (!radioLista || envioEnCurso) return;
  if (tiempoActual - momentoUltimoEnvio < INTERVALO_ENVIO_MS) return;
  momentoUltimoEnvio = tiempoActual;
  prepararMovimiento();

  // Los contadores se repiten en cada paquete. Perder un paquete aislado
  // no borra una pulsacion y recibir varios no vuelve a ejecutarla.
  envioEnCurso = true;
  resultadoUltimoEnvio = esp_now_send(direccionDeDestino,
    (const uint8_t*)&datosParaEnviar, sizeof(datosParaEnviar));
  if (resultadoUltimoEnvio != ESP_OK) envioEnCurso = false;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  datosParaEnviar.identificadorDeArranque = esp_random();
  for (int indiceBoton = 0; indiceBoton < CANTIDAD_BOTONES; indiceBoton++) {
    pinMode(pinesBotones[indiceBoton], INPUT_PULLUP);
    ultimaLecturaDelPin[indiceBoton] = digitalRead(pinesBotones[indiceBoton]);
    estadoConfirmado[indiceBoton] = ultimaLecturaDelPin[indiceBoton];
    momentoUltimoCambio[indiceBoton] = millis();
  }

  WiFi.mode(WIFI_STA);
  if (esp_wifi_set_channel(CANAL_RADIO, WIFI_SECOND_CHAN_NONE) != ESP_OK) return;
  if (esp_now_init() != ESP_OK) return;
  if (esp_now_register_send_cb(alTerminarEnvio) != ESP_OK) return;

  // peer significa el otro equipo. La variable propia tiene nombre en espanol.
  esp_now_peer_info_t receptor = {};
  memcpy(receptor.peer_addr, direccionDeDestino, sizeof(direccionDeDestino));
  receptor.channel = CANAL_RADIO;
  receptor.ifidx = WIFI_IF_STA;
  receptor.encrypt = false;
  if (esp_now_add_peer(&receptor) != ESP_OK) return;
  radioLista = true;
}

void loop() {
  unsigned long tiempoActual = millis();
  leerBotones(tiempoActual);
  enviarControles(tiempoActual);

  // Diagnostico lento para que el puerto serie no se llene con cada envio.
  if (tiempoActual - momentoUltimoDiagnostico >= 300) {
    momentoUltimoDiagnostico = tiempoActual;
    char mensaje[80];
    if (radioLista) {
      snprintf(mensaje, sizeof(mensaje), "Mando izq=%d der=%d radio=%d\n",
        datosParaEnviar.motorIzquierdo, datosParaEnviar.motorDerecho,
        (int)resultadoUltimoEnvio);
    } else {
      snprintf(mensaje, sizeof(mensaje), "Error al preparar ESP-NOW\n");
    }
    mostrarPorPuertoSerie(mensaje);
  }
}
