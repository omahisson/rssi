#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_timer.h>

HardwareSerial uartComunicacao(2);

constexpr int PINO_RX = 4;
constexpr int PINO_TX = 2;
constexpr uint32_t BAUD_UART = 115200;

const char* NOME_REDE = "ESP_ABERTO";

constexpr size_t MAXIMO_DISPOSITIVOS = 10;

// Constante global
const unsigned long INTERVALO_LOOP_MS = 1000;

struct RegistroFrame {
  uint32_t tempoMs;
  uint8_t mac[6];
  int8_t rssi;
  uint8_t canal;
  uint16_t sequencia;
  uint8_t retransmissao;
};

uint8_t dispositivosConectados[MAXIMO_DISPOSITIVOS][6];
size_t quantidadeDispositivosConectados = 0;

portMUX_TYPE mutexDispositivos = portMUX_INITIALIZER_UNLOCKED;

unsigned long ultimaAtualizacaoDispositivos = 0;

void atualizarDispositivosConectados() {
  wifi_sta_list_t listaEstacoes;

  if (esp_wifi_ap_get_sta_list(&listaEstacoes) != ESP_OK) {
    return;
  }

  size_t quantidade = listaEstacoes.num;

  if (quantidade > MAXIMO_DISPOSITIVOS) {
    quantidade = MAXIMO_DISPOSITIVOS;
  }

  portENTER_CRITICAL(&mutexDispositivos);

  quantidadeDispositivosConectados = quantidade;

  for (size_t i = 0; i < quantidade; i++) {
    memcpy(
      dispositivosConectados[i],
      listaEstacoes.sta[i].mac,
      6
    );
  }

  portEXIT_CRITICAL(&mutexDispositivos);
}

bool dispositivoEstaConectado(const uint8_t* mac) {
  bool encontrado = false;

  portENTER_CRITICAL(&mutexDispositivos);

  for (size_t i = 0; i < quantidadeDispositivosConectados; i++) {
    if (memcmp(dispositivosConectados[i], mac, 6) == 0) {
      encontrado = true;
      break;
    }
  }

  portEXIT_CRITICAL(&mutexDispositivos);

  return encontrado;
}

void enviarFramePelaUART(const RegistroFrame& registro) {
  uartComunicacao.printf(
    "FRAME|%lu|"
    "%02X:%02X:%02X:%02X:%02X:%02X|"
    "%d|%u|%u|%u\n",

    static_cast<unsigned long>(registro.tempoMs),

    registro.mac[0],
    registro.mac[1],
    registro.mac[2],
    registro.mac[3],
    registro.mac[4],
    registro.mac[5],

    registro.rssi,
    registro.canal,
    registro.sequencia,
    registro.retransmissao
  );
}

void aoReceberFrame(
  void* buffer,
  wifi_promiscuous_pkt_type_t tipoPacote
) {
  if (
    tipoPacote != WIFI_PKT_MGMT &&
    tipoPacote != WIFI_PKT_DATA
  ) {
    return;
  }

  const wifi_promiscuous_pkt_t* pacote =
    static_cast<const wifi_promiscuous_pkt_t*>(buffer);

  const uint8_t* quadro = pacote->payload;
  const uint16_t tamanhoQuadro = pacote->rx_ctrl.sig_len;

  if (tamanhoQuadro < 24) {
    return;
  }

  const uint16_t controleFrame =
    static_cast<uint16_t>(quadro[0]) |
    (static_cast<uint16_t>(quadro[1]) << 8);

  const uint8_t tipoFrame = (controleFrame >> 2) & 0x03;

  if (tipoFrame == 1) {
    return;
  }

  const uint8_t* macTransmissor = &quadro[10];

  if (!dispositivoEstaConectado(macTransmissor)) {
    return;
  }

  const uint8_t retransmissao =
    (controleFrame & 0x0800) != 0 ? 1 : 0;

  const uint16_t controleSequencia =
    static_cast<uint16_t>(quadro[22]) |
    (static_cast<uint16_t>(quadro[23]) << 8);

  RegistroFrame registro;

  registro.tempoMs = static_cast<uint32_t>(
    esp_timer_get_time() / 1000ULL
  );

  memcpy(registro.mac, macTransmissor, 6);

  registro.rssi = pacote->rx_ctrl.rssi;
  registro.canal = pacote->rx_ctrl.channel;
  registro.sequencia = controleSequencia >> 4;
  registro.retransmissao = retransmissao;

  // Fila removida agora envia imediatamente
  enviarFramePelaUART(registro);
}

void configurarCapturaWiFi() {
  wifi_promiscuous_filter_t filtro = {};

  filtro.filter_mask =
    WIFI_PROMIS_FILTER_MASK_MGMT |
    WIFI_PROMIS_FILTER_MASK_DATA;

  esp_wifi_set_promiscuous_filter(&filtro);
  esp_wifi_set_promiscuous_rx_cb(aoReceberFrame);
  esp_wifi_set_promiscuous(true);
}

void setup() {
  Serial.begin(115200);

  uartComunicacao.begin(
    BAUD_UART,
    SERIAL_8N1,
    PINO_RX,
    PINO_TX
  );

  WiFi.mode(WIFI_AP);

  const bool accessPointCriado = WiFi.softAP(NOME_REDE);

  if (!accessPointCriado) {
    uartComunicacao.println("ERRO|ACCESS_POINT");

    while (true) {
      delay(1000);
    }
  }

  delay(500);

  atualizarDispositivosConectados();
  configurarCapturaWiFi();

  uartComunicacao.printf(
    "STATUS|AP_CRIADO|%s|%s\n",
    NOME_REDE,
    WiFi.softAPIP().toString().c_str()
  );
}

void loop() {
  const unsigned long agora = millis();

  if (
    agora - ultimaAtualizacaoDispositivos >=
    INTERVALO_LOOP_MS
  ) {
    ultimaAtualizacaoDispositivos = agora;
    atualizarDispositivosConectados();
  }

  delay(1);
}
