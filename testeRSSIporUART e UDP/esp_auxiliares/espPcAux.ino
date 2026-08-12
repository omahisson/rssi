#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>


HardwareSerial uartComunicacao(2);


constexpr int PINO_RX = 4;
constexpr int PINO_TX = 2;
constexpr uint32_t BAUD_UART = 115200;


const char* IDENTIFICADOR_ANCORA = "ANCORA_02";


const char* NOME_WIFI = "host";
const char* SENHA_WIFI = "senha";


IPAddress IP_SERVIDOR(172, 20, 10, 2);


constexpr uint16_t PORTA_SERVIDOR = 5005;
constexpr uint16_t PORTA_LOCAL_UDP = 5006;


const unsigned long INTERVALO_LOOP_MS = 1000;
constexpr unsigned long INTERVALO_REGISTRO_MS = INTERVALO_LOOP_MS;


constexpr size_t TAMANHO_BUFFER_UART = 512;
constexpr size_t TAMANHO_BUFFER_UDP = 512;


WiFiUDP comunicacaoUDP;


bool udpIniciado = false;
bool wifiEstavaConectado = false;


unsigned long ultimoRegistroServidor = 0;
unsigned long ultimoLoopMs = 0;


char bufferLinhaUART[TAMANHO_BUFFER_UART];
size_t tamanhoLinhaUART = 0;
bool descartandoLinhaUART = false;


char bufferMensagemUDP[TAMANHO_BUFFER_UDP];


void iniciarConexaoWiFi() {
  Serial.printf(
    "[Wi-Fi] Conectando em \"%s\"...\n",
    NOME_WIFI
  );


  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(NOME_WIFI, SENHA_WIFI);
}


bool enviarMensagemUDP(const char* mensagem) {
  if (
    WiFi.status() != WL_CONNECTED ||
    !udpIniciado
  ) {
    return false;
  }


  if (
    comunicacaoUDP.beginPacket(
      IP_SERVIDOR,
      PORTA_SERVIDOR
    ) != 1
  ) {
    Serial.println("[UDP] Erro ao iniciar pacote");
    return false;
  }


  const size_t tamanhoMensagem = strlen(mensagem);


  const size_t bytesEscritos = comunicacaoUDP.write(
    reinterpret_cast<const uint8_t*>(mensagem),
    tamanhoMensagem
  );


  if (bytesEscritos != tamanhoMensagem) {
    Serial.println("[UDP] Escrita incompleta");
    return false;
  }


  if (comunicacaoUDP.endPacket() != 1) {
    Serial.println("[UDP] Erro ao finalizar pacote");
    return false;
  }


  return true;
}


void enviarRegistroAoServidor() {
  char mensagem[96];


  snprintf(
    mensagem,
    sizeof(mensagem),
    "REGISTRO|%s|AUXILIAR",
    IDENTIFICADOR_ANCORA
  );


  if (enviarMensagemUDP(mensagem)) {
    ultimoRegistroServidor = millis();
    Serial.printf("[UDP] Registro enviado: %s\n", mensagem);
  }
}


void iniciarUDP() {
  if (
    udpIniciado ||
    WiFi.status() != WL_CONNECTED
  ) {
    return;
  }


  if (comunicacaoUDP.begin(PORTA_LOCAL_UDP)) {
    udpIniciado = true;


    Serial.printf(
      "[UDP] Porta local iniciada: %u\n",
      PORTA_LOCAL_UDP
    );


    enviarRegistroAoServidor();
  } else {
    Serial.println("[UDP] Falha ao iniciar porta local");
  }
}


void verificarConexaoWiFi() {
  const bool wifiConectado =
    WiFi.status() == WL_CONNECTED;


  if (wifiConectado && !wifiEstavaConectado) {
    Serial.println();
    Serial.println("[Wi-Fi] Conectado");
    Serial.print("[Wi-Fi] IP do espPc: ");
    Serial.println(WiFi.localIP());


    iniciarUDP();
  }


  if (!wifiConectado && wifiEstavaConectado) {
    Serial.println("[Wi-Fi] Conexão perdida");


    comunicacaoUDP.stop();
    udpIniciado = false;
  }


  wifiEstavaConectado = wifiConectado;


  if (wifiConectado) {
    iniciarUDP();
  }
}


bool mensagemEhConfiguracao(const char* mensagem) {
  constexpr char PREFIXO[] = "CONFIG|";


  return strncmp(
    mensagem,
    PREFIXO,
    sizeof(PREFIXO) - 1
  ) == 0;
}


void processarMensagemDoServidor(
  const char* mensagem,
  const IPAddress& ipRemoto,
  uint16_t portaRemota
) {
  if (ipRemoto != IP_SERVIDOR) {
    Serial.printf(
      "[UDP] Mensagem ignorada de %s:%u\n",
      ipRemoto.toString().c_str(),
      portaRemota
    );
    return;
  }


  Serial.printf("[UDP] Recebido do servidor: %s\n", mensagem);


  if (!mensagemEhConfiguracao(mensagem)) {
    Serial.println("[UDP] Tipo de mensagem não reconhecido");
    return;
  }


  uartComunicacao.println(mensagem);
  Serial.println("[UART] Configuração enviada ao espCarregador");
}


void receberMensagensUDP() {
  if (!udpIniciado) {
    return;
  }


  int tamanhoPacote = comunicacaoUDP.parsePacket();


  while (tamanhoPacote > 0) {
    const IPAddress ipRemoto = comunicacaoUDP.remoteIP();
    const uint16_t portaRemota = comunicacaoUDP.remotePort();


    if (
      tamanhoPacote >=
      static_cast<int>(sizeof(bufferMensagemUDP))
    ) {
      while (comunicacaoUDP.available() > 0) {
        comunicacaoUDP.read();
      }


      Serial.println("[UDP] Mensagem excedeu o buffer");
      tamanhoPacote = comunicacaoUDP.parsePacket();
      continue;
    }


    const int bytesLidos = comunicacaoUDP.read(
      bufferMensagemUDP,
      sizeof(bufferMensagemUDP) - 1
    );


    if (bytesLidos > 0) {
      bufferMensagemUDP[bytesLidos] = '\0';


      processarMensagemDoServidor(
        bufferMensagemUDP,
        ipRemoto,
        portaRemota
      );
    }


    tamanhoPacote = comunicacaoUDP.parsePacket();
  }
}


bool formatoFrameValido(const char* linha) {
  constexpr char PREFIXO[] = "FRAME|";


  if (
    strncmp(
      linha,
      PREFIXO,
      sizeof(PREFIXO) - 1
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


void enviarFrameAoServidor(const char* linhaRecebida) {
  constexpr char PREFIXO[] = "FRAME|";
  constexpr size_t TAMANHO_PREFIXO = sizeof(PREFIXO) - 1;


  const char* dadosFrame = linhaRecebida + TAMANHO_PREFIXO;


  char pacoteUDP[320];


  const int tamanhoPacote = snprintf(
    pacoteUDP,
    sizeof(pacoteUDP),
    "%s|%s",
    IDENTIFICADOR_ANCORA,
    dadosFrame
  );


  if (
    tamanhoPacote < 0 ||
    tamanhoPacote >= static_cast<int>(sizeof(pacoteUDP))
  ) {
    Serial.println("[UDP] Frame excedeu o buffer");
    return;
  }


  if (enviarMensagemUDP(pacoteUDP)) {
    Serial.printf("[UDP] Frame enviado: %s\n", pacoteUDP);
  } else {
    Serial.println("[UDP] Frame descartado: envio indisponível");
  }
}


void enviarConfirmacaoAoServidor(const char* linhaRecebida) {
  char copia[TAMANHO_BUFFER_UART];


  strncpy(copia, linhaRecebida, sizeof(copia) - 1);
  copia[sizeof(copia) - 1] = '\0';


  char* contexto = nullptr;
  char* tipo = strtok_r(copia, "|", &contexto);
  char* versao = strtok_r(nullptr, "|", &contexto);


  if (
    tipo == nullptr ||
    versao == nullptr ||
    strcmp(tipo, "ACK_CONFIG") != 0
  ) {
    Serial.println("[UART] ACK de configuração inválido");
    return;
  }


  char mensagemAck[96];


  snprintf(
    mensagemAck,
    sizeof(mensagemAck),
    "ACK_CONFIG|%s|%s",
    IDENTIFICADOR_ANCORA,
    versao
  );


  if (enviarMensagemUDP(mensagemAck)) {
    Serial.printf("[UDP] Confirmação enviada: %s\n", mensagemAck);
  }
}


void processarLinhaUART(const char* linhaRecebida) {
  Serial.printf("[UART] Recebido: %s\n", linhaRecebida);


  if (formatoFrameValido(linhaRecebida)) {
    enviarFrameAoServidor(linhaRecebida);
    return;
  }


  if (strncmp(linhaRecebida, "ACK_CONFIG|", 11) == 0) {
    enviarConfirmacaoAoServidor(linhaRecebida);
    return;
  }


  Serial.println("[UART] Mensagem de status ou formato desconhecido");
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


void verificarRegistroPeriodico() {
  if (
    !udpIniciado ||
    WiFi.status() != WL_CONNECTED
  ) {
    return;
  }


  if (
    millis() - ultimoRegistroServidor >=
    INTERVALO_REGISTRO_MS
  ) {
    enviarRegistroAoServidor();
  }
}


void setup() {
  Serial.begin(115200);
  delay(500);


  Serial.println();
  Serial.println("==============================");
  Serial.println("espPc auxiliar iniciado");
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
    "UART2: RX GPIO %d, TX GPIO %d, baud %lu\n",
    PINO_RX,
    PINO_TX,
    static_cast<unsigned long>(BAUD_UART)
  );


  iniciarConexaoWiFi();
}


void loop() {
  receberMensagensUDP();
  receberDadosUART();


  const unsigned long agora = millis();


  if (
    agora - ultimoLoopMs >=
    INTERVALO_LOOP_MS
  ) {
    ultimoLoopMs = agora;


    verificarConexaoWiFi();
    verificarRegistroPeriodico();
  }


  delay(1);
}