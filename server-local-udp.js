const dgram = require("dgram");

const PORTA_SERVIDOR = 5005;
const ENDERECO_SERVIDOR = "0.0.0.0";

const servidorUDP = dgram.createSocket("udp4");

function converterMensagemEmObjeto(mensagemBruta) {
  const partesMensagem = mensagemBruta.trim().split("|");

  if (partesMensagem.length !== 7) {
    return {
      valido: false,
      erro: "Formato inválido",
      mensagem_bruta: mensagemBruta,
    };
  }

  const [
    identificador_ancora,
    tempo_ms,
    mac,
    rssi,
    canal,
    sequencia_frame,
    retransmissao,
  ] = partesMensagem;

  return {
    valido: true,
    identificador_ancora,
    tempo_ms: Number(tempo_ms),
    mac,
    rssi: Number(rssi),
    canal: Number(canal),
    sequencia_frame: Number(sequencia_frame),
    retransmissao: Number(retransmissao),
    recebido_em: new Date().toISOString(),
    mensagem_bruta: mensagemBruta,
  };
}

servidorUDP.on("listening", () => {
  const enderecoAtual = servidorUDP.address();

  console.log(
    `Servidor UDP escutando em ${enderecoAtual.address}:${enderecoAtual.port}`
  );

  console.log("Aguardando pacotes dos gateways...\n");
});

servidorUDP.on("message", (mensagemRecebida, origemRemota) => {
  const mensagemBruta = mensagemRecebida.toString("utf8");
  const pacoteConvertido = converterMensagemEmObjeto(mensagemBruta);

  if (!pacoteConvertido.valido) {
    console.warn("Pacote inválido recebido:");
    console.warn({
      origem: `${origemRemota.address}:${origemRemota.port}`,
      ...pacoteConvertido,
    });

    return;
  }

  console.log({
    origem: `${origemRemota.address}:${origemRemota.port}`,
    ...pacoteConvertido,
  });
});

servidorUDP.on("error", (erro) => {
  console.error("Erro no servidor UDP:", erro);
  servidorUDP.close();
});

servidorUDP.bind(PORTA_SERVIDOR, ENDERECO_SERVIDOR);