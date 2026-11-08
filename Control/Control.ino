void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 INICIADO");
}

void loop() {
  Serial.println("Funcionando correctamente");
  delay(1000);
}