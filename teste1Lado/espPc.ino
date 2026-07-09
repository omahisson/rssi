#include <Arduino.h>

HardwareSerial uartTeste(2);

constexpr int PINO_RX = 4;
constexpr int PINO_TX = 2; // Não será usado

void setup() {
  Serial.begin(115200);

  uartTeste.begin(9600, SERIAL_8N1, PINO_RX, PINO_TX);

  Serial.println("Receptor UART iniciado");
}

void loop() {
  while (uartTeste.available()) {
    Serial.write(uartTeste.read());
  }
}