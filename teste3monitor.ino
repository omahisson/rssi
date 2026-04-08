#include "esp_wifi.h"
#include <WiFi.h>

int canalAtual = 1;

void capturarPacotesWifi(void* bufferRecebido, wifi_promiscuous_pkt_type_t tipoPacote) {

  if (tipoPacote != WIFI_PKT_MGMT) return;

  wifi_promiscuous_pkt_t *pacoteCapturado = (wifi_promiscuous_pkt_t*)bufferRecebido;
  uint8_t *dadosPacotePayload = pacoteCapturado->payload;

  int intensidadeSinalRSSI = pacoteCapturado->rx_ctrl.rssi;

  uint8_t enderecoMac[6];
  memcpy(enderecoMac, dadosPacotePayload + 10, 6);

  Serial.print("MAC: ");
  for (int indice = 0; indice < 6; indice++) {
    if (enderecoMac[indice] < 16) Serial.print("0");
    Serial.print(enderecoMac[indice], HEX);
    if (indice < 5) Serial.print(":");
  }

  Serial.print("  RSSI: ");
  Serial.print(intensidadeSinalRSSI);

  Serial.print("  CH: ");
  Serial.println(canalAtual);
}

void setup() {

  Serial.begin(115200);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&capturarPacotesWifi);
}

void loop() {

  esp_wifi_set_channel(canalAtual, WIFI_SECOND_CHAN_NONE);

  canalAtual++;
  if (canalAtual > 13) canalAtual = 1;

  delay(400);
}
