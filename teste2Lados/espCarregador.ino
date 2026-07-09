#include <Arduino.h>

HardwareSerial uartTeste(2);

constexpr int PINO_RX = 4;
constexpr int PINO_TX = 2;

const char* NOME_PLACA = "placaCarregador";

unsigned long ultimoEnvio = 0;
unsigned long contador = 0;

void processarMensagem(const String& mensagem) {
  Serial.printf("[%s] recebeu: %s\n", NOME_PLACA, mensagem.c_str());

  if (mensagem.startsWith("PING|")) {
    uartTeste.print("ACK|");
    uartTeste.print(NOME_PLACA);
    uartTeste.print("|");
    uartTeste.println(mensagem);

    Serial.printf("[%s] enviou ACK\n", NOME_PLACA);
  }
}

void setup() {
  Serial.begin(115200);

  uartTeste.begin(
    9600,
    SERIAL_8N1,
    PINO_RX,
    PINO_TX
  );

  uartTeste.setTimeout(100);

  Serial.println("placaCarregador iniciada");
}

void loop() {
  while (uartTeste.available()) {
    String mensagem = uartTeste.readStringUntil('\n');
    mensagem.trim();

    if (!mensagem.isEmpty()) {
      processarMensagem(mensagem);
    }
  }

  if (millis() - ultimoEnvio >= 3000) {
    ultimoEnvio = millis();

    uartTeste.print("PING|");
    uartTeste.print(NOME_PLACA);
    uartTeste.print("|");
    uartTeste.println(contador);

    Serial.printf(
      "[%s] enviou PING %lu\n",
      NOME_PLACA,
      contador
    );

    contador++;
  }
}