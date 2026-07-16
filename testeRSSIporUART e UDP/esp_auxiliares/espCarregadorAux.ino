#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

HardwareSerial uartComunicacao(2);

constexpr int PINO_RX = 4;
constexpr int PINO_TX = 2;
constexpr uint32_t BAUD_UART = 115200;

constexpr size_t MAXIMO_MACS_INTERESSE = 10;
constexpr size_t TAMANHO_FILA_FRAMES = 128;
constexpr size_t TAMANHO_BUFFER_UART = 512;

struct RegistroFrame {
  uint32_t tempoMs;
  uint8_t mac[6];
  int8_t rssi;
  uint8_t canal;
  uint16_t sequencia;
  uint8_t retransmissao;
};

QueueHandle_t filaFrames;

uint8_t macsInteresse[MAXIMO_MACS_INTERESSE][6];
size_t quantidadeMacsInteresse = 0;
uint8_t canalAtual = 1;
uint32_t versaoConfiguracaoAtual = 0;

portMUX_TYPE mutexConfiguracao = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t framesDescartados = 0;

char bufferLinhaUART[TAMANHO_BUFFER_UART];
size_t tamanhoLinhaUART = 0;
bool descartandoLinhaUART = false;

unsigned long ultimoRelatorio = 0;

int converterHexadecimal(char caractere) {
  if (caractere >= '0' && caractere <= '9') {
    return caractere - '0';
  }

  if (caractere >= 'A' && caractere <= 'F') {
    return caractere - 'A' + 10;
  }

  if (caractere >= 'a' && caractere <= 'f') {
    return caractere - 'a' + 10;
  }

  return -1;
}

bool converterMacTexto(
  const char* macTexto,
  uint8_t macConvertido[6]
) {
  char caracteresHexadecimais[13];
  size_t quantidadeCaracteres = 0;

  for (
    const char* atual = macTexto;
    *atual != '\0';
    atual++
  ) {
    if (*atual == ':' || *atual == '-' || *atual == '.') {
      continue;
    }

    if (
      converterHexadecimal(*atual) < 0 ||
      quantidadeCaracteres >= 12
    ) {
      return false;
    }

    caracteresHexadecimais[quantidadeCaracteres++] = *atual;
  }

  if (quantidadeCaracteres != 12) {
    return false;
  }

  caracteresHexadecimais[12] = '\0';

  for (size_t i = 0; i < 6; i++) {
    const int parteAlta = converterHexadecimal(
      caracteresHexadecimais[i * 2]
    );

    const int parteBaixa = converterHexadecimal(
      caracteresHexadecimais[i * 2 + 1]
    );

    if (parteAlta < 0 || parteBaixa < 0) {
      return false;
    }

    macConvertido[i] = static_cast<uint8_t>(
      (parteAlta << 4) | parteBaixa
    );
  }

  return true;
}

bool macEstaNaLista(const uint8_t* mac) {
  bool encontrado = false;

  portENTER_CRITICAL(&mutexConfiguracao);

  for (size_t i = 0; i < quantidadeMacsInteresse; i++) {
    if (memcmp(macsInteresse[i], mac, 6) == 0) {
      encontrado = true;
      break;
    }
  }

  portEXIT_CRITICAL(&mutexConfiguracao);

  return encontrado;
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

  const uint8_t tipoFrame =
    (controleFrame >> 2) & 0x03;

  if (tipoFrame == 1) {
    return;
  }

  const uint8_t* macTransmissor = &quadro[10];

  if (!macEstaNaLista(macTransmissor)) {
    return;
  }

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
  registro.retransmissao =
    (controleFrame & 0x0800) != 0 ? 1 : 0;

  if (xQueueSend(filaFrames, &registro, 0) != pdTRUE) {
    framesDescartados++;
  }
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

void configurarModoPromiscuo() {
  wifi_promiscuous_filter_t filtro = {};

  filtro.filter_mask =
    WIFI_PROMIS_FILTER_MASK_MGMT |
    WIFI_PROMIS_FILTER_MASK_DATA;

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_filter(&filtro);
  esp_wifi_set_promiscuous_rx_cb(aoReceberFrame);
}

bool aplicarConfiguracao(const char* linhaRecebida) {
  char copiaLinha[TAMANHO_BUFFER_UART];

  strncpy(
    copiaLinha,
    linhaRecebida,
    sizeof(copiaLinha) - 1
  );
  copiaLinha[sizeof(copiaLinha) - 1] = '\0';

  char* contexto = nullptr;
  char* tipo = strtok_r(copiaLinha, "|", &contexto);
  char* versaoTexto = strtok_r(nullptr, "|", &contexto);
  char* canalTexto = strtok_r(nullptr, "|", &contexto);
  char* quantidadeTexto = strtok_r(nullptr, "|", &contexto);

  if (
    tipo == nullptr ||
    versaoTexto == nullptr ||
    canalTexto == nullptr ||
    quantidadeTexto == nullptr ||
    strcmp(tipo, "CONFIG") != 0
  ) {
    return false;
  }

  char* fimConversao = nullptr;

  const unsigned long versao = strtoul(
    versaoTexto,
    &fimConversao,
    10
  );

  if (*versaoTexto == '\0' || *fimConversao != '\0') {
    return false;
  }

  const long canal = strtol(canalTexto, &fimConversao, 10);

  if (
    *canalTexto == '\0' ||
    *fimConversao != '\0' ||
    canal < 1 ||
    canal > 13
  ) {
    return false;
  }

  const long quantidade = strtol(
    quantidadeTexto,
    &fimConversao,
    10
  );

  if (
    *quantidadeTexto == '\0' ||
    *fimConversao != '\0' ||
    quantidade < 0 ||
    quantidade > static_cast<long>(MAXIMO_MACS_INTERESSE)
  ) {
    return false;
  }

  uint8_t novosMacs[MAXIMO_MACS_INTERESSE][6] = {};

  for (long i = 0; i < quantidade; i++) {
    char* macTexto = strtok_r(nullptr, "|", &contexto);

    if (
      macTexto == nullptr ||
      !converterMacTexto(macTexto, novosMacs[i])
    ) {
      return false;
    }
  }

  if (strtok_r(nullptr, "|", &contexto) != nullptr) {
    return false;
  }

  esp_wifi_set_promiscuous(false);

  const esp_err_t resultadoCanal = esp_wifi_set_channel(
    static_cast<uint8_t>(canal),
    WIFI_SECOND_CHAN_NONE
  );

  if (resultadoCanal != ESP_OK) {
    Serial.printf(
      "[CONFIG] Falha ao mudar canal: %d\n",
      resultadoCanal
    );

    esp_wifi_set_promiscuous(true);
    return false;
  }

  portENTER_CRITICAL(&mutexConfiguracao);

  quantidadeMacsInteresse = static_cast<size_t>(quantidade);
  canalAtual = static_cast<uint8_t>(canal);
  versaoConfiguracaoAtual = static_cast<uint32_t>(versao);

  for (size_t i = 0; i < quantidadeMacsInteresse; i++) {
    memcpy(macsInteresse[i], novosMacs[i], 6);
  }

  portEXIT_CRITICAL(&mutexConfiguracao);

  esp_wifi_set_promiscuous(true);

  uartComunicacao.printf(
    "ACK_CONFIG|%lu|%u|%u\n",
    versao,
    canalAtual,
    static_cast<unsigned int>(quantidadeMacsInteresse)
  );

  Serial.printf(
    "[CONFIG] Aplicada versão %lu, canal %u, %u MAC(s)\n",
    versao,
    canalAtual,
    static_cast<unsigned int>(quantidadeMacsInteresse)
  );

  return true;
}

void processarLinhaUART(const char* linhaRecebida) {
  Serial.printf("[UART] Recebido: %s\n", linhaRecebida);

  if (strncmp(linhaRecebida, "CONFIG|", 7) != 0) {
    Serial.println("[UART] Comando desconhecido");
    return;
  }

  if (!aplicarConfiguracao(linhaRecebida)) {
    Serial.println("[CONFIG] Configuração inválida");
    uartComunicacao.println("ERRO_CONFIG|FORMATO_INVALIDO");
  }
}

void receberDadosUART() {
  while (uartComunicacao.available() > 0) {
    const int valorRecebido = uartComunicacao.read();

    if (valorRecebido < 0) {
      continue;
    }

    const char caractere = static_cast<char>(valorRecebido);

    if (descartandoLinhaUART) {
      if (caractere == '\n') {
        descartandoLinhaUART = false;
        tamanhoLinhaUART = 0;
      }

      continue;
    }

    if (caractere == '\r') {
      continue;
    }

    if (caractere == '\n') {
      if (tamanhoLinhaUART == 0) {
        continue;
      }

      bufferLinhaUART[tamanhoLinhaUART] = '\0';
      processarLinhaUART(bufferLinhaUART);
      tamanhoLinhaUART = 0;
      continue;
    }

    if (
      tamanhoLinhaUART <
      sizeof(bufferLinhaUART) - 1
    ) {
      bufferLinhaUART[tamanhoLinhaUART++] = caractere;
      continue;
    }

    Serial.println("[UART] Linha excedeu o buffer e foi descartada");
    tamanhoLinhaUART = 0;
    descartandoLinhaUART = true;
  }
}

void relatarEstado() {
  if (millis() - ultimoRelatorio < 5000) {
    return;
  }

  ultimoRelatorio = millis();

  uint32_t versao;
  uint8_t canal;
  size_t quantidade;

  portENTER_CRITICAL(&mutexConfiguracao);
  versao = versaoConfiguracaoAtual;
  canal = canalAtual;
  quantidade = quantidadeMacsInteresse;
  portEXIT_CRITICAL(&mutexConfiguracao);

  Serial.printf(
    "[STATUS] Configuração %lu, canal %u, %u MAC(s)\n",
    static_cast<unsigned long>(versao),
    canal,
    static_cast<unsigned int>(quantidade)
  );

  if (framesDescartados > 0) {
    Serial.printf(
      "[STATUS] Frames descartados por fila cheia: %lu\n",
      static_cast<unsigned long>(framesDescartados)
    );

    framesDescartados = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("==============================");
  Serial.println("espCarregador auxiliar iniciado");
  Serial.println("==============================");

  uartComunicacao.begin(
    BAUD_UART,
    SERIAL_8N1,
    PINO_RX,
    PINO_TX
  );

  filaFrames = xQueueCreate(
    TAMANHO_FILA_FRAMES,
    sizeof(RegistroFrame)
  );

  if (filaFrames == nullptr) {
    Serial.println("[ERRO] Não foi possível criar a fila");

    while (true) {
      delay(1000);
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  configurarModoPromiscuo();

  Serial.println(
    "Aguardando canal e MACs de interesse enviados pelo espPc..."
  );
}

void loop() {
  receberDadosUART();

  RegistroFrame registro;

  while (xQueueReceive(filaFrames, &registro, 0) == pdTRUE) {
    enviarFramePelaUART(registro);
  }

  relatarEstado();
  delay(1);
}