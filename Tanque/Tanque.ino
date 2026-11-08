#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

const int PIN_BOCINA = 25;
const int PIN_LED_DISPARO = 26;
const int ENA = 18;
const int IN1 = 19;
const int IN2 = 21;
const int ENB = 5;
const int IN3 = 22;
const int IN4 = 23;
const int CANAL_RADIO = 1;
const unsigned long LIMITE_SIN_DATOS_MS = 500;
const unsigned long DURACION_LED_DISPARO_MS = 150;

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

// El callback de recepcion puede ejecutarse mientras loop lee los datos.
// Esta proteccion permite copiar un paquete completo sin mezclar dos paquetes.
portMUX_TYPE proteccionDeDatos = portMUX_INITIALIZER_UNLOCKED;
DatosDelMando ultimoMensaje = {};
unsigned long momentoUltimoMensaje = 0;
bool hayMensaje = false;
bool radioLista = false;
bool comunicacionAnteriorActiva = false;
DatosDelMando eventosYaAtendidos = {};
unsigned long momentoDisparo = 0;
bool ledDisparoActivo = false;
unsigned long momentoUltimoDiagnostico = 0;

void mostrarPorPuertoSerie(const char* texto) {
  size_t cantidadDeBytes = strlen(texto);
  if (Serial.availableForWrite() >= (int)cantidadDeBytes) {
    Serial.write((const uint8_t*)texto, cantidadDeBytes);
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void recibirDatos(const esp_now_recv_info_t* informacion,
                  const uint8_t* bytesRecibidos, int cantidadDeBytes) {
#else
void recibirDatos(const uint8_t* direccion,
                  const uint8_t* bytesRecibidos, int cantidadDeBytes) {
#endif
  if (cantidadDeBytes != sizeof(DatosDelMando)) return;
  DatosDelMando recibido;
  memcpy(&recibido, bytesRecibidos, sizeof(recibido));
  if (recibido.motorIzquierdo < -255 || recibido.motorIzquierdo > 255
      || recibido.motorDerecho < -255 || recibido.motorDerecho > 255) return;

  unsigned long tiempoDeRecepcion = millis();
  portENTER_CRITICAL(&proteccionDeDatos);
  ultimoMensaje = recibido;
  momentoUltimoMensaje = tiempoDeRecepcion;
  hayMensaje = true;
  portEXIT_CRITICAL(&proteccionDeDatos);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
bool iniciarPWM() {
  bool izquierdoListo = ledcAttach(ENA, 1000, 8);
  bool derechoListo = ledcAttach(ENB, 1000, 8);
  return izquierdoListo && derechoListo;
}
void escribirPWM(int pin, int velocidad) {
  ledcWrite(pin, velocidad);
}
#else
const int CANAL_PWM_IZQUIERDO = 0;
const int CANAL_PWM_DERECHO = 1;
bool iniciarPWM() {
  double frecuenciaIzquierda = ledcSetup(CANAL_PWM_IZQUIERDO, 1000, 8);
  double frecuenciaDerecha = ledcSetup(CANAL_PWM_DERECHO, 1000, 8);
  ledcAttachPin(ENA, CANAL_PWM_IZQUIERDO);
  ledcAttachPin(ENB, CANAL_PWM_DERECHO);
  return frecuenciaIzquierda > 0 && frecuenciaDerecha > 0;
}
void escribirPWM(int pin, int velocidad) {
  if (pin == ENA) ledcWrite(CANAL_PWM_IZQUIERDO, velocidad);
  else ledcWrite(CANAL_PWM_DERECHO, velocidad);
}
#endif

void controlarMotorIzquierdo(int velocidad) {
  velocidad = constrain(velocidad, -255, 255);
  static int velocidadAnterior = 1000;
  if (velocidad == velocidadAnterior) return;
  velocidadAnterior = velocidad;
  // Quitamos PWM antes de cambiar el sentido de las entradas.
  escribirPWM(ENA, 0);
  digitalWrite(IN1, velocidad > 0);
  digitalWrite(IN2, velocidad < 0);
  escribirPWM(ENA, abs(velocidad));
}

void controlarMotorDerecho(int velocidad) {
  velocidad = constrain(velocidad, -255, 255);
  static int velocidadAnterior = 1000;
  if (velocidad == velocidadAnterior) return;
  velocidadAnterior = velocidad;
  escribirPWM(ENB, 0);
  digitalWrite(IN3, velocidad > 0);
  digitalWrite(IN4, velocidad < 0);
  escribirPWM(ENB, abs(velocidad));
}

void atenderPulsaciones(const DatosDelMando& datos, unsigned long tiempoActual) {
  // Cada contador cambia una vez por pulsacion. Los paquetes repetidos
  // conservan el mismo numero y no disparan otra vez esta accion.
  if (datos.pulsacionesDisparo != eventosYaAtendidos.pulsacionesDisparo) {
    ledDisparoActivo = true;
    momentoDisparo = tiempoActual;
    mostrarPorPuertoSerie("Tanque: DISPARO\n");
  }
  if (datos.pulsacionesRecarga != eventosYaAtendidos.pulsacionesRecarga) {
    mostrarPorPuertoSerie("Tanque: RECARGA recibida\n");
  }
  if (datos.pulsacionesAuxiliar != eventosYaAtendidos.pulsacionesAuxiliar) {
    mostrarPorPuertoSerie("Tanque: AUXILIAR recibido\n");
  }
  if (datos.pulsacionesCambioModo != eventosYaAtendidos.pulsacionesCambioModo) {
    mostrarPorPuertoSerie("Tanque: CAMBIO DE MODO recibido\n");
  }
  eventosYaAtendidos = datos;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOCINA, OUTPUT);
  pinMode(PIN_LED_DISPARO, OUTPUT);
  digitalWrite(PIN_BOCINA, LOW);
  digitalWrite(PIN_LED_DISPARO, LOW);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  // ENA y ENB quedan apagados tambien si la configuracion de PWM falla.
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  digitalWrite(ENA, LOW);
  digitalWrite(ENB, LOW);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  if (!iniciarPWM()) return;
  controlarMotorIzquierdo(0);
  controlarMotorDerecho(0);

  WiFi.mode(WIFI_STA);
  if (esp_wifi_set_channel(CANAL_RADIO, WIFI_SECOND_CHAN_NONE) != ESP_OK) return;
  if (esp_now_init() != ESP_OK) return;
  if (esp_now_register_recv_cb(recibirDatos) != ESP_OK) return;
  radioLista = true;
}

void loop() {
  DatosDelMando datos = {};
  unsigned long tiempoDelMensaje;
  bool seRecibioMensaje;
  portENTER_CRITICAL(&proteccionDeDatos);
  datos = ultimoMensaje;
  tiempoDelMensaje = momentoUltimoMensaje;
  seRecibioMensaje = hayMensaje;
  portEXIT_CRITICAL(&proteccionDeDatos);

  // Tomamos el tiempo despues de copiar el paquete para que no sea anterior
  // a un mensaje que acaba de llegar mientras empezaba esta vuelta.
  unsigned long tiempoActual = millis();
  bool comunicacionActiva = radioLista && seRecibioMensaje
    && tiempoActual - tiempoDelMensaje < LIMITE_SIN_DATOS_MS;

  if (comunicacionActiva) {
    bool nuevoArranque = datos.identificadorDeArranque
      != eventosYaAtendidos.identificadorDeArranque;
    if (!comunicacionAnteriorActiva || nuevoArranque) {
      // Al conectar o reiniciar, tomamos los contadores como punto inicial.
      // Esto evita ejecutar pulsaciones viejas ocurridas durante un corte.
      eventosYaAtendidos = datos;
      ledDisparoActivo = false;
    } else {
      atenderPulsaciones(datos, tiempoActual);
    }
    controlarMotorIzquierdo(datos.motorIzquierdo);
    controlarMotorDerecho(datos.motorDerecho);
    digitalWrite(PIN_BOCINA, datos.bocinaApretada != 0);
  } else {
    controlarMotorIzquierdo(0);
    controlarMotorDerecho(0);
    digitalWrite(PIN_BOCINA, LOW);
    ledDisparoActivo = false;
  }
  comunicacionAnteriorActiva = comunicacionActiva;

  // Pulso visible por cada disparo. El tiempo se consulta sin usar delay.
  if (ledDisparoActivo
      && tiempoActual - momentoDisparo >= DURACION_LED_DISPARO_MS) {
    ledDisparoActivo = false;
  }
  digitalWrite(PIN_LED_DISPARO, ledDisparoActivo);

  if (tiempoActual - momentoUltimoDiagnostico >= 300) {
    momentoUltimoDiagnostico = tiempoActual;
    char mensaje[80];
    if (comunicacionActiva) {
      snprintf(mensaje, sizeof(mensaje), "Tanque izq=%d der=%d bocina=%d\n",
        datos.motorIzquierdo, datos.motorDerecho, datos.bocinaApretada);
    } else if (!radioLista) {
      snprintf(mensaje, sizeof(mensaje), "Error al preparar radio o PWM\n");
    } else {
      snprintf(mensaje, sizeof(mensaje), "Tanque: esperando mando\n");
    }
    mostrarPorPuertoSerie(mensaje);
  }
}
