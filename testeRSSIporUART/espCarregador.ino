#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

HardwareSerial uartComunicacao(2);

constexpr int PINO_RX = 4;
constexpr int PINO_TX = 2;

const char* NOME_REDE = "ESP_ABERTO";

void enviarInformacoesAccessPoint() {
  IPAddress ip = WiFi.softAPIP();

  uartComunicacao.printf(
    "ACCESS_POINT|%s|%s\n",
    NOME_REDE,
    ip.toString().c_str()
  );
}

void setup() {
  uartComunicacao.begin(
    115200,
    SERIAL_8N1,
    PINO_RX,
    PINO_TX
  );

  WiFi.mode(WIFI_AP);

  bool accessPointCriado = WiFi.softAP(NOME_REDE);

  delay(500);

  if (accessPointCriado) {
    uartComunicacao.println("STATUS|ACCESS_POINT_CRIADO");
    enviarInformacoesAccessPoint();
  } else {
    uartComunicacao.println("ERRO|FALHA_AO_CRIAR_ACCESS_POINT");
  }
}

void loop() {
  wifi_sta_list_t listaDispositivos;

  esp_err_t resultado = esp_wifi_ap_get_sta_list(
    &listaDispositivos
  );

  if (resultado != ESP_OK) {
    uartComunicacao.printf(
      "ERRO|LISTA_DISPOSITIVOS|%d\n",
      resultado
    );

    delay(3000);
    return;
  }

  uartComunicacao.printf(
    "INICIO|%d\n",
    listaDispositivos.num
  );

  for (int i = 0; i < listaDispositivos.num; i++) {
    wifi_sta_info_t dispositivo =
      listaDispositivos.sta[i];

    uartComunicacao.printf(
      "DISPOSITIVO|%d|"
      "%02X:%02X:%02X:%02X:%02X:%02X|"
      "%d\n",

      i + 1,

      dispositivo.mac[0],
      dispositivo.mac[1],
      dispositivo.mac[2],
      dispositivo.mac[3],
      dispositivo.mac[4],
      dispositivo.mac[5],

      dispositivo.rssi
    );
  }

  uartComunicacao.println("FIM");

  delay(3000);
}