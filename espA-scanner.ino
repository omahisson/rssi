#include "esp_wifi.h"
#include <WiFi.h>

#define PINO_UART_RX 16
#define PINO_UART_TX 17
#define VELOCIDADE_UART 115200

// Para versão zero canal fixo
#define CANAL_WIFI_FIXO 6

struct DadosProbe {
  uint32_t tempo_ms;
  uint8_t mac_origem[6];
  int8_t rssi;
  uint8_t canal;
  uint16_t sequencia_frame;
  uint8_t retransmissao;
};

QueueHandle_t filaDePacotes;

bool pacoteEhProbeRequest(const uint8_t* pacote, uint16_t tamanho) {
  if (tamanho < 24) return false;

  uint16_t controleFrame = pacote[0] | (pacote[1] << 8);

  uint8_t tipoFrame = (controleFrame & 0x000C) >> 2;
  uint8_t subtipoFrame = (controleFrame & 0x00F0) >> 4;

  // tipo 0 = Management Frame, subtipo 4 = Probe Request
  return tipoFrame == 0 && subtipoFrame == 4;
}

void capturarPacotesWifi(void* bufferRecebido, wifi_promiscuous_pkt_type_t tipoPacote) {
  if (tipoPacote != WIFI_PKT_MGMT) return;

  wifi_promiscuous_pkt_t* pacoteCapturado = (wifi_promiscuous_pkt_t*) bufferRecebido;

  const uint8_t* dadosPacote = pacoteCapturado->payload;
  uint16_t tamanhoPacote = pacoteCapturado->rx_ctrl.sig_len;

  if (!pacoteEhProbeRequest(dadosPacote, tamanhoPacote)) return;

  DadosProbe dadosExtraidos;

  dadosExtraidos.tempo_ms = millis();

  memcpy(dadosExtraidos.mac_origem, dadosPacote + 10, 6);

  dadosExtraidos.rssi = pacoteCapturado->rx_ctrl.rssi;
  dadosExtraidos.canal = pacoteCapturado->rx_ctrl.channel;

  uint16_t controleFrame = dadosPacote[0] | (dadosPacote[1] << 8);
  dadosExtraidos.retransmissao = (controleFrame & (1 << 11)) ? 1 : 0;

  // Sequence Control nos bytes 22 e 23
  // Os 4 bits baixos são fragment number
  // O sequence number fica dos bits 4 ao 15
  uint16_t controleSequencia = dadosPacote[22] | (dadosPacote[23] << 8);
  dadosExtraidos.sequencia_frame = controleSequencia >> 4;

  xQueueSend(filaDePacotes, &dadosExtraidos, 0);
}

void enviarPacotePelaUART(const DadosProbe& dados) {
  char macFormatado[13];

  snprintf(
    macFormatado,
    sizeof(macFormatado),
    "%02X%02X%02X%02X%02X%02X",
    dados.mac_origem[0],
    dados.mac_origem[1],
    dados.mac_origem[2],
    dados.mac_origem[3],
    dados.mac_origem[4],
    dados.mac_origem[5]
  );

  // Formato
  // timestamp|mac|rssi|canal|seq_frame|retry
  Serial2.printf(
    "%lu|%s|%d|%u|%u|%u\n",
    dados.tempo_ms,
    macFormatado,
    dados.rssi,
    dados.canal,
    dados.sequencia_frame,
    dados.retransmissao
  );

  // Debugar via USB
  Serial.printf(
    "SCAN -> %lu|%s|%d|%u|%u|%u\n",
    dados.tempo_ms,
    macFormatado,
    dados.rssi,
    dados.canal,
    dados.sequencia_frame,
    dados.retransmissao
  );
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(VELOCIDADE_UART, SERIAL_8N1, PINO_UART_RX, PINO_UART_TX);

  filaDePacotes = xQueueCreate(100, sizeof(DadosProbe));

  if (filaDePacotes == NULL) {
    Serial.println("Erro ao criar fila de pacotes.");
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true);

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(CANAL_WIFI_FIXO, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(&capturarPacotesWifi);
  esp_wifi_set_promiscuous(true);

  Serial.println("ESP32 A Scanner iniciado.");
  Serial.print("Canal fixo: ");
  Serial.println(CANAL_WIFI_FIXO);
}

void loop() {
  DadosProbe dadosRecebidos;

  while (xQueueReceive(filaDePacotes, &dadosRecebidos, 0) == pdTRUE) {
    enviarPacotePelaUART(dadosRecebidos);
  }
}