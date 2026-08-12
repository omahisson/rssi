#include <Arduino.h>

HardwareSerial uartTeste(2);

constexpr int PINO_RX = 4; // Não será usado
constexpr int PINO_TX = 2;

unsigned long contador = 0;

void setup() {
  uartTeste.begin(9600, SERIAL_8N1, PINO_RX, PINO_TX);
}

void loop() {
  uartTeste.print("TESTE UART: ");
  uartTeste.println(contador++);

  delay(1000);
}