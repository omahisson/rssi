#include <Arduino.h>

HardwareSerial uartComunicacao(2);

constexpr int PINO_RX = 4;
constexpr int PINO_TX = 2;

void setup() {
  Serial.begin(115200);

  uartComunicacao.begin(
    115200,
    SERIAL_8N1,
    PINO_RX,
    PINO_TX
  );

  Serial.println();
  Serial.println("espPC iniciado");
  Serial.println("Aguardando dados do espCarregador");
}

void loop() {
  while (uartComunicacao.available()) {
    char caractere = uartComunicacao.read();
    Serial.write(caractere);
  }
}