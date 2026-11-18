# Historial de cambios

Acá registramos los cambios relevantes del proyecto y las validaciones realizadas.

## Primera versión (V1) — Rama `dev` — 2026-11-10

### Agregado

- Primera implementación de comunicación inalámbrica entre dos ESP32 mediante ESP-NOW.
- Configuración de la comunicación en el canal 1, utilizando la dirección de difusión `FF:FF:FF:FF:FF:FF`.
- Lectura de un botón físico en el mando, conectado al GPIO26 y configurado con `INPUT_PULLUP`.
- Envío del estado de la bocina desde el mando hacia el tanque, con transmisiones periódicas cada 20 ms.
- Recepción de los paquetes en el ESP32 del tanque.
- Control de una bocina conectada al GPIO25 del tanque.
- Activación de la bocina mientras se mantiene presionado el botón y desactivación al soltarlo.
- Incorporación de un mecanismo de seguridad que apaga la bocina después de 500 ms sin recibir paquetes.

### Funcionamiento inicial

- El mando lee el estado del botón y transmite la información mediante ESP-NOW.
- El tanque recibe el estado y activa o desactiva la salida correspondiente.
- La comunicación funciona directamente entre las dos placas, sin router ni conexión a Internet.

### Alcance de la versión

- Esta versión solamente contempla el control de la bocina.
- No incluye joystick, motores, disparo, pantallas, sensores ni radar.
- La estructura inicial de comunicación se utiliza como base para las versiones posteriores.

### Pendiente

- Validar el funcionamiento con el montaje físico definitivo.
- Comprobar la estabilidad de la comunicación y el apagado de seguridad ante una pérdida de conexión.
- Incorporar nuevas entradas de control y actuadores.

## Rama `dev` — 2026-11-08

### Agregado

- Primera versión digital funcional del mando en `Control/Control.ino` y versión básica del tanque en `Tanque/Tanque.ino`.
- Integración del joystick analógico, cuatro botones físicos y el clic del joystick como solicitud de cambio de modo.
- Comunicación ESP-NOW en el canal 1, con un intervalo de envío de 20 ms cuando finaliza la transmisión anterior.
- Paquete compartido `DatosDelMando` de 32 bytes con velocidades, estados de botones, contadores de pulsaciones e identificador de arranque.
- Lectura de botones con resistencias internas `INPUT_PULLUP`, antirrebote de 30 ms y detección de nuevas pulsaciones por flanco de bajada.
- Lectura proporcional del joystick con zona muerta, inversión configurable de ejes y mezcla diferencial para los dos motores.
- Control del L298N mediante PWM de 1000 Hz y 8 bits, con velocidades entre -255 y 255.
- Bocina activa mientras se mantiene presionado su botón.
- Indicación de disparo mediante un pulso de 150 ms en un LED.
- Recepción de eventos de recarga, auxiliar y cambio de modo, informados por puerto serie.
- Supervisión de recepción para detener los motores y apagar la bocina y el LED después de 500 ms sin datos válidos.
- Temporización con `millis()` y diagnóstico por puerto serie a 115200 baudios.

### Ajustado

- El cambio de modo se asignó al clic del joystick y el mando quedó con cuatro botones físicos.
- Los contadores de pulsaciones permiten distinguir eventos nuevos de paquetes repetidos. Al establecer comunicación o detectar un nuevo arranque, el tanque toma una referencia inicial para descartar eventos anteriores.
- El receptor copia los datos de forma protegida para mantener coherentes los campos del paquete.
- Los motores actualizan sus salidas cuando cambia la velocidad solicitada.
- Los programas incorporan compatibilidad de código con distintas versiones de las funciones de PWM y ESP-NOW.

### Validado en simulación

- Compilación y ejecución de los dos programas en Mokxi.
- Recepción continua de controles con velocidades cero en reposo.
- Detección y recepción de disparo, recarga, auxiliar y clic del joystick.
- Activación de la bocina mientras está presionada y apagado al liberarla.

### Pendiente

- Validar el recorrido completo de los motores y el corte por pérdida de comunicación.
- Probar pérdidas de paquetes, reinicios y reconexión con eventos acumulados.
- Implementar las acciones finales de recarga, auxiliar y cambio de modo.
- Integrar munición, display de siete segmentos, torreta, sensores y faros.
- Validar la carga, alimentación y funcionamiento con los componentes físicos.

## Rama `feat/control` — 2026-11-05

### Agregado

- Se incorporó la lógica del control principal en la simulación.
- Se conectó el panel de botones para enviar señales al microcontrolador.
- Se documentó el comportamiento esperado del circuito en Tinkercad.
