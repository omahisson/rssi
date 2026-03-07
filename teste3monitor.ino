#include "esp_wifi.h"
#include <WiFi.h>

int channel = 1;

void sniffer(void* buf, wifi_promiscuous_pkt_type_t type) {

  if (type != WIFI_PKT_MGMT) return;

  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t*)buf;
  uint8_t *payload = pkt->payload;

  int rssi = pkt->rx_ctrl.rssi;

  uint8_t mac[6];
  memcpy(mac, payload + 10, 6);

  Serial.print("MAC: ");
  for(int i=0;i<6;i++){
    if(mac[i]<16) Serial.print("0");
    Serial.print(mac[i],HEX);
    if(i<5) Serial.print(":");
  }

  Serial.print("  RSSI: ");
  Serial.print(rssi);

  Serial.print("  CH: ");
  Serial.println(channel);
}

void setup() {

  Serial.begin(115200);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&sniffer);
}

void loop() {

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  channel++;
  if(channel>13) channel=1;

  delay(400);
}
