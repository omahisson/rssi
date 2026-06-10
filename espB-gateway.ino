#include <WiFi.h>
#include <WiFiUdp.h>

#define PINO_UART_RX 16
#define PINO_UART_TX 17
#define VELOCIDADE_UART 115200

// Mudar isso em cada gateway
#define IDENTIFICADOR_ANCORA "A01"

// Ajustar
const char* NOME_REDE_WIFI = "NOME_REDE";
const char* SENHA_REDE_WIFI = "SENHA_REDE";

IPAddress IP_SERVIDOR_LOCAL(192, 168, 0, 100);
const uint16_t PORTA_SERVIDOR_LOCAL = 5005;

WiFiUDP comunicacaoUDP;

char linhaRecebidaUART[128];
uint8_t indiceLinhaUART = 0;

void conectarNaRedeWifi() {
  Serial.print("Conectando no Wi-Fi: ");
  Serial.println(NOME_REDE_WIFI);

  WiFi.mode(WIFI_STA);
  WiFi.begin(NOME_REDE_WIFI, SENHA_REDE_WIFI);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi conectado.");
  Serial.print("IP do Gateway: ");
  Serial.println(WiFi.localIP());
}

bool linhaRecebidaEhValida(const char* linha) {
  // Formato esperado: timestamp|mac|rssi|canal|seq_frame|retry
  // Exemplo: 1739234|A4C3F0112233|-67|6|281|0

  int quantidadeSeparadores = 0;

  for (int i = 0; linha[i] != '\0'; i++) {
    if (linha[i] == '|') quantidadeSeparadores++;
  }

  return quantidadeSeparadores == 5;
}

void enviarPacoteUDP(const char* linhaRecebida) {
  if (WiFi.status() != WL_CONNECTED) {
    conectarNaRedeWifi();
  }

  char mensagemFinal[180];

  // Formato final: anchor_id|timestamp|mac|rssi|canal|seq_frame|retry
  snprintf(
    mensagemFinal,
    sizeof(mensagemFinal),
    "%s|%s",
    IDENTIFICADOR_ANCORA,
    linhaRecebida
  );

  comunicacaoUDP.beginPacket(IP_SERVIDOR_LOCAL, PORTA_SERVIDOR_LOCAL);
  comunicacaoUDP.write((const uint8_t*)mensagemFinal, strlen(mensagemFinal));
  comunicacaoUDP.endPacket();

  Serial.print("UDP -> ");
  Serial.println(mensagemFinal);
}

void processarCaractereRecebidoUART(char caractereRecebido) {
  if (caractereRecebido == '\r') return;

  if (caractereRecebido == '\n') {
    linhaRecebidaUART[indiceLinhaUART] = '\0';

    if (indiceLinhaUART > 0) {
      if (linhaRecebidaEhValida(linhaRecebidaUART)) {
        enviarPacoteUDP(linhaRecebidaUART);
      } else {
        Serial.print("Linha UART inválida: ");
        Serial.println(linhaRecebidaUART);
      }
    }

    indiceLinhaUART = 0;
    return;
  }

  if (indiceLinhaUART < sizeof(linhaRecebidaUART) - 1) {
    linhaRecebidaUART[indiceLinhaUART++] = caractereRecebido;
  } else {
    indiceLinhaUART = 0;
    Serial.println("Buffer UART estourou. Linha descartada.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(VELOCIDADE_UART, SERIAL_8N1, PINO_UART_RX, PINO_UART_TX);

  conectarNaRedeWifi();

  Serial.println("ESP32 B Gateway UDP iniciado.");
  Serial.print("Identificador da âncora: ");
  Serial.println(IDENTIFICADOR_ANCORA);
  Serial.print("Servidor UDP: ");
  Serial.print(IP_SERVIDOR_LOCAL);
  Serial.print(":");
  Serial.println(PORTA_SERVIDOR_LOCAL);
}

void loop() {
  while (Serial2.available()) {
    char caractereRecebido = Serial2.read();
    processarCaractereRecebidoUART(caractereRecebido);
  }
}