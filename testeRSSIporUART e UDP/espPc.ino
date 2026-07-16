#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

HardwareSerial uartComunicacao(2);

constexpr int PINO_RX = 4;
constexpr int PINO_TX = 2;
constexpr uint32_t BAUD_UART = 115200;

//alterar
const char* NOME_WIFI = "NOME_DA_SUA_REDE";
const char* SENHA_WIFI = "SENHA_DA_SUA_REDE";

//alterar
IPAddress IP_SERVIDOR(172, 20, 10, 2);

constexpr uint16_t PORTA_SERVIDOR = 5005;
constexpr uint16_t PORTA_LOCAL_UDP = 5006;

//alterar em cada um
const char* IDENTIFICADOR_ANCORA = "ANCORA_01";

WiFiUDP clienteUDP;

bool udpIniciado = false;

constexpr size_t TAMANHO_BUFFER_UART = 256;

char bufferLinhaUART[TAMANHO_BUFFER_UART];

size_t tamanhoLinhaUART = 0;

bool descartandoLinhaUART = false;

unsigned long ultimaTentativaWiFi = 0;

constexpr unsigned long INTERVALO_RECONEXAO_WIFI_MS = 5000;

bool wifiEstavaConectado = false;

void iniciarConexaoWiFi() {
  Serial.printf(
    "[Wi-Fi] Conectando em \"%s\"...\n",
    NOME_WIFI
  );

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(NOME_WIFI, SENHA_WIFI);

  ultimaTentativaWiFi = millis();
}

void iniciarUDP() {
  if (udpIniciado) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (clienteUDP.begin(PORTA_LOCAL_UDP)) {
    udpIniciado = true;

    Serial.printf(
      "[UDP] Porta local iniciada: %u\n",
      PORTA_LOCAL_UDP
    );
  } else {
    Serial.println(
      "[UDP] Não foi possível iniciar a porta local"
    );
  }
}

void verificarConexaoWiFi() {
  bool wifiConectado =
    WiFi.status() == WL_CONNECTED;

  if (wifiConectado && !wifiEstavaConectado) {
    Serial.println();
    Serial.println("[Wi-Fi] Conectado");

    Serial.print("[Wi-Fi] IP do espPC: ");
    Serial.println(WiFi.localIP());

    Serial.print("[Wi-Fi] Servidor UDP: ");
    Serial.print(IP_SERVIDOR);
    Serial.print(":");
    Serial.println(PORTA_SERVIDOR);

    iniciarUDP();
  }

  if (!wifiConectado && wifiEstavaConectado) {
    Serial.println("[Wi-Fi] Conexão perdida");

    clienteUDP.stop();
    udpIniciado = false;
  }

  wifiEstavaConectado = wifiConectado;

  if (wifiConectado) {
    iniciarUDP();
    return;
  }

  unsigned long agora = millis();

  if (
    agora - ultimaTentativaWiFi <
    INTERVALO_RECONEXAO_WIFI_MS
  ) {
    return;
  }

  ultimaTentativaWiFi = agora;

  Serial.println(
    "[Wi-Fi] Tentando reconectar..."
  );

  WiFi.begin(NOME_WIFI, SENHA_WIFI);
}

bool enviarMensagemUDP(const char* mensagem) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(
      "[UDP] Pacote descartado: Wi-Fi desconectado"
    );

    return false;
  }

  if (!udpIniciado) {
    iniciarUDP();
  }

  if (!udpIniciado) {
    Serial.println(
      "[UDP] Pacote descartado: UDP não iniciado"
    );

    return false;
  }

  int inicioPacote = clienteUDP.beginPacket(
    IP_SERVIDOR,
    PORTA_SERVIDOR
  );

  if (inicioPacote != 1) {
    Serial.println(
      "[UDP] Erro ao iniciar pacote"
    );

    return false;
  }

  size_t tamanhoMensagem = strlen(mensagem);

  size_t bytesEscritos = clienteUDP.write(
    reinterpret_cast<const uint8_t*>(mensagem),
    tamanhoMensagem
  );

  if (bytesEscritos != tamanhoMensagem) {
    Serial.println(
      "[UDP] Nem todos os bytes foram escritos"
    );

    return false;
  }

  int resultadoEnvio = clienteUDP.endPacket();

  if (resultadoEnvio != 1) {
    Serial.println(
      "[UDP] Erro ao finalizar pacote"
    );

    return false;
  }

  return true;
}

bool formatoFrameValido(const char* linha) {
  constexpr char PREFIXO_FRAME[] = "FRAME|";

  if (
    strncmp(
      linha,
      PREFIXO_FRAME,
      sizeof(PREFIXO_FRAME) - 1
    ) != 0
  ) {
    return false;
  }

  size_t quantidadeSeparadores = 0;

  for (const char* atual = linha; *atual != '\0'; atual++) {
    if (*atual == '|') {
      quantidadeSeparadores++;
    }
  }

  return quantidadeSeparadores == 6;
}

void processarLinhaUART(const char* linhaRecebida) {
  Serial.printf(
    "[UART] Recebido: %s\n",
    linhaRecebida
  );

  if (!formatoFrameValido(linhaRecebida)) {
    Serial.println(
      "[UART] Mensagem de controle ou formato inválido"
    );

    return;
  }

  constexpr char PREFIXO_FRAME[] = "FRAME|";

  constexpr size_t TAMANHO_PREFIXO =
    sizeof(PREFIXO_FRAME) - 1;

  const char* dadosFrame =
    linhaRecebida + TAMANHO_PREFIXO;

  char pacoteUDP[256];

  int tamanhoPacote = snprintf(
    pacoteUDP,
    sizeof(pacoteUDP),
    "%s|%s",
    IDENTIFICADOR_ANCORA,
    dadosFrame
  );

  if (tamanhoPacote < 0) {
    Serial.println(
      "[UDP] Erro ao montar pacote"
    );

    return;
  }

  if (
    tamanhoPacote >=
    static_cast<int>(sizeof(pacoteUDP))
  ) {
    Serial.println(
      "[UDP] Pacote excedeu o tamanho do buffer"
    );

    return;
  }

  if (enviarMensagemUDP(pacoteUDP)) {
    Serial.printf(
      "[UDP] Enviado: %s\n",
      pacoteUDP
    );
  }
}

void receberDadosUART() {
  while (uartComunicacao.available() > 0) {
    int valorRecebido = uartComunicacao.read();

    if (valorRecebido < 0) {
      continue;
    }

    char caractere = static_cast<char>(
      valorRecebido
    );

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
      bufferLinhaUART[tamanhoLinhaUART] =
        caractere;

      tamanhoLinhaUART++;

      continue;
    }

    Serial.println(
      "[UART] Linha excedeu o buffer e foi descartada"
    );

    tamanhoLinhaUART = 0;
    descartandoLinhaUART = true;
  }
}

void setup() {
  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("==============================");
  Serial.println("espPC iniciado");
  Serial.println("==============================");

  Serial.print("Identificador: ");
  Serial.println(IDENTIFICADOR_ANCORA);

  uartComunicacao.begin(
    BAUD_UART,
    SERIAL_8N1,
    PINO_RX,
    PINO_TX
  );

  Serial.printf(
    "UART2 iniciada: RX GPIO %d, TX GPIO %d, baud %lu\n",
    PINO_RX,
    PINO_TX,
    static_cast<unsigned long>(BAUD_UART)
  );

  iniciarConexaoWiFi();

  Serial.println(
    "Aguardando frames do espCarregador..."
  );
}

void loop() {
  receberDadosUART();
  verificarConexaoWiFi();

  delay(1);
}