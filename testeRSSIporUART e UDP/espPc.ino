#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// RX = GPIO13 <- ligar ao TX do espCarregador
// TX = GPIO15 -> não precisa ser ligado ao espCarregador
// GND comum entre as placas.
//
// Logs usam Serial1 (TX somente no GPIO2), evitando misturar
// mensagens de debug com os frames recebidos pela UART0.
constexpr uint32_t BAUD_UART = 115200;

const unsigned long INTERVALO_LOOP_MS = 1000;

// nao esquecer
const char* NOME_WIFI = "NOME_DA_SUA_REDE";
const char* SENHA_WIFI = "SENHA_DA_SUA_REDE";

// nao esquecer
IPAddress IP_SERVIDOR(172, 20, 10, 2);

constexpr uint16_t PORTA_SERVIDOR = 5005;
constexpr uint16_t PORTA_LOCAL_UDP = 5006;

const char* IDENTIFICADOR_ANCORA = "ANCORA_01";

constexpr size_t TAMANHO_BUFFER_UART = 256;

WiFiUDP clienteUDP;

HardwareSerial& logSerial = Serial1;

bool udpIniciado = false;
bool wifiEstavaConectado = false;

char bufferLinhaUART[TAMANHO_BUFFER_UART];
size_t tamanhoLinhaUART = 0;
bool descartandoLinhaUART = false;

unsigned long ultimoLoopMs = 0;

void iniciarConexaoWiFi() {
  logSerial.printf("[Wi-Fi] Conectando em \"%s\"...\n", NOME_WIFI);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(NOME_WIFI, SENHA_WIFI);
}

void iniciarUDP() {
  if (udpIniciado || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (clienteUDP.begin(PORTA_LOCAL_UDP)) {
    udpIniciado = true;

    logSerial.printf(
      "[UDP] Porta local iniciada: %u\n",
      PORTA_LOCAL_UDP
    );
  } else {
    logSerial.println("[UDP] Não foi possível iniciar a porta local");
  }
}

void verificarConexaoWiFi() {
  const bool wifiConectado = WiFi.status() == WL_CONNECTED;

  if (wifiConectado && !wifiEstavaConectado) {
    logSerial.println();
    logSerial.println("[Wi-Fi] Conectado");
    logSerial.print("[Wi-Fi] IP do espPC: ");
    logSerial.println(WiFi.localIP());
    logSerial.print("[Wi-Fi] Servidor UDP: ");
    logSerial.print(IP_SERVIDOR);
    logSerial.print(":");
    logSerial.println(PORTA_SERVIDOR);

    iniciarUDP();
  }

  if (!wifiConectado && wifiEstavaConectado) {
    logSerial.println("[Wi-Fi] Conexão perdida");

    clienteUDP.stop();
    udpIniciado = false;
  }

  wifiEstavaConectado = wifiConectado;

  if (wifiConectado) {
    iniciarUDP();
  }
}

bool enviarMensagemUDP(const char* mensagem) {
  if (WiFi.status() != WL_CONNECTED) {
    logSerial.println("[UDP] Pacote descartado: Wi-Fi desconectado");
    return false;
  }

  if (!udpIniciado) {
    iniciarUDP();
  }

  if (!udpIniciado) {
    logSerial.println("[UDP] Pacote descartado: UDP não iniciado");
    return false;
  }

  if (clienteUDP.beginPacket(IP_SERVIDOR, PORTA_SERVIDOR) != 1) {
    logSerial.println("[UDP] Erro ao iniciar pacote");
    return false;
  }

  const size_t tamanhoMensagem = strlen(mensagem);

  const size_t bytesEscritos = clienteUDP.write(
    reinterpret_cast<const uint8_t*>(mensagem),
    tamanhoMensagem
  );

  if (bytesEscritos != tamanhoMensagem) {
    logSerial.println("[UDP] Nem todos os bytes foram escritos");
    clienteUDP.endPacket();
    return false;
  }

  if (clienteUDP.endPacket() != 1) {
    logSerial.println("[UDP] Erro ao finalizar pacote");
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
  if (!formatoFrameValido(linhaRecebida)) {
    logSerial.printf(
      "[UART] Mensagem ignorada: %s\n",
      linhaRecebida
    );
    return;
  }

  constexpr char PREFIXO_FRAME[] = "FRAME|";
  constexpr size_t TAMANHO_PREFIXO = sizeof(PREFIXO_FRAME) - 1;

  const char* dadosFrame = linhaRecebida + TAMANHO_PREFIXO;

  char pacoteUDP[256];

  const int tamanhoPacote = snprintf(
    pacoteUDP,
    sizeof(pacoteUDP),
    "%s|%s",
    IDENTIFICADOR_ANCORA,
    dadosFrame
  );

  if (tamanhoPacote < 0) {
    logSerial.println("[UDP] Erro ao montar pacote");
    return;
  }

  if (tamanhoPacote >= static_cast<int>(sizeof(pacoteUDP))) {
    logSerial.println("[UDP] Pacote excedeu o tamanho do buffer");
    return;
  }

  if (enviarMensagemUDP(pacoteUDP)) {
    logSerial.printf("[UDP] Enviado: %s\n", pacoteUDP);
  }
}

void receberDadosUART() {
  while (Serial.available() > 0) {
    const int valorRecebido = Serial.read();

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

    if (tamanhoLinhaUART < sizeof(bufferLinhaUART) - 1) {
      bufferLinhaUART[tamanhoLinhaUART++] = caractere;
      continue;
    }

    logSerial.println("[UART] Linha excedeu o buffer e foi descartada");
    tamanhoLinhaUART = 0;
    descartandoLinhaUART = true;
  }
}

void setup() {
  Serial.begin(BAUD_UART);
  Serial.swap();

  logSerial.begin(115200);
  delay(500);

  logSerial.println();
  logSerial.println("==============================");
  logSerial.println("espPC ESP8266 iniciado");
  logSerial.println("==============================");
  logSerial.print("Identificador: ");
  logSerial.println(IDENTIFICADOR_ANCORA);
  logSerial.println("UART0 remapeada: RX GPIO13, TX GPIO15, 115200 baud");

  iniciarConexaoWiFi();

  logSerial.println("Aguardando frames do espCarregador...");
}

void loop() {
  receberDadosUART();

  const unsigned long agora = millis();

  if (agora - ultimoLoopMs >= INTERVALO_LOOP_MS) {
    ultimoLoopMs = agora;
    verificarConexaoWiFi();
  }

  yield();
}