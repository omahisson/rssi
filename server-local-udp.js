"use strict";

const dgram = require("node:dgram");

const PORTA_SERVIDOR = 5005;
const ENDERECO_SERVIDOR = "0.0.0.0";

const IDENTIFICADOR_ANCORA_PRINCIPAL = "ANCORA_01";
const MAXIMO_MACS_INTERESSE = 10;
const INTERVALO_REENVIO_CONFIGURACAO_MS = 3000;
const TEMPO_INATIVIDADE_GATEWAY_MS = 30000;

const servidorUDP = dgram.createSocket("udp4");

const gatewaysAuxiliares = new Map();

let configuracaoAtual = {
  versao: 0,
  canal: null,
  macs: [],
  origem: null,
  atualizadoEm: null,
};

function agoraIso() {
  return new Date().toISOString();
}

function normalizarMac(macInformado) {
  const macLimpo = String(macInformado)
    .toUpperCase()
    .replace(/[^0-9A-F]/g, "");

  return /^[0-9A-F]{12}$/.test(macLimpo)
    ? macLimpo
    : null;
}

function inteiroValido(valor, minimo, maximo) {
  const numero = Number(valor);

  if (!Number.isInteger(numero)) {
    return null;
  }

  if (numero < minimo || numero > maximo) {
    return null;
  }

  return numero;
}

function listasIguais(listaA, listaB) {
  if (listaA.length !== listaB.length) {
    return false;
  }

  return listaA.every((valor, indice) => valor === listaB[indice]);
}

function normalizarListaMacs(macsInformados) {
  const macsValidos = [];
  const macsJaIncluidos = new Set();

  for (const macInformado of macsInformados) {
    const macNormalizado = normalizarMac(macInformado);

    if (!macNormalizado || macsJaIncluidos.has(macNormalizado)) {
      continue;
    }

    macsJaIncluidos.add(macNormalizado);
    macsValidos.push(macNormalizado);

    if (macsValidos.length >= MAXIMO_MACS_INTERESSE) {
      break;
    }
  }

  return macsValidos.sort();
}

function atualizarConfiguracao(canal, macs, origem) {
  const canalValidado = inteiroValido(canal, 1, 13);
  const macsNormalizados = normalizarListaMacs(macs);

  if (canalValidado === null) {
    console.warn(`[CONFIG] Canal inválido recebido de ${origem}: ${canal}`);
    return false;
  }

  const configuracaoMudou =
    configuracaoAtual.canal !== canalValidado ||
    !listasIguais(configuracaoAtual.macs, macsNormalizados);

  if (!configuracaoMudou) {
    return false;
  }

  configuracaoAtual = {
    versao: configuracaoAtual.versao + 1,
    canal: canalValidado,
    macs: macsNormalizados,
    origem,
    atualizadoEm: agoraIso(),
  };

  console.log("\n[CONFIG] Configuração de interesse atualizada:");
  console.log(configuracaoAtual);

  distribuirConfiguracao();
  return true;
}

function aprenderConfiguracaoPeloFramePrincipal(frame) {
  const macNormalizado = normalizarMac(frame.mac);

  if (!macNormalizado) {
    return;
  }

  const novosMacs = [...configuracaoAtual.macs];

  if (!novosMacs.includes(macNormalizado)) {
    novosMacs.push(macNormalizado);
  }

  atualizarConfiguracao(
    frame.canal,
    novosMacs,
    "FRAME_DA_ANCORA_PRINCIPAL"
  );
}

function montarMensagemConfiguracao() {
  if (
    configuracaoAtual.versao === 0 ||
    configuracaoAtual.canal === null
  ) {
    return null;
  }

  return [
    "CONFIG",
    configuracaoAtual.versao,
    configuracaoAtual.canal,
    configuracaoAtual.macs.length,
    ...configuracaoAtual.macs,
  ].join("|");
}

function gatewayEstaAtivo(gateway) {
  return Date.now() - gateway.ultimoContatoMs <= TEMPO_INATIVIDADE_GATEWAY_MS;
}

function enviarMensagemUDP(mensagem, endereco, porta, descricao) {
  const dados = Buffer.from(mensagem, "utf8");

  servidorUDP.send(dados, porta, endereco, (erro) => {
    if (erro) {
      console.error(`[UDP] Falha ao enviar ${descricao}:`, erro.message);
    }
  });
}

function enviarConfiguracaoParaGateway(identificadorAncora) {
  const gateway = gatewaysAuxiliares.get(identificadorAncora);
  const mensagemConfiguracao = montarMensagemConfiguracao();

  if (!gateway || !mensagemConfiguracao || !gatewayEstaAtivo(gateway)) {
    return;
  }

  enviarMensagemUDP(
    mensagemConfiguracao,
    gateway.endereco,
    gateway.porta,
    `configuração para ${identificadorAncora}`
  );

  gateway.ultimaVersaoEnviada = configuracaoAtual.versao;
  gateway.ultimoEnvioConfiguracaoMs = Date.now();

  console.log(
    `[CONFIG] Enviada versão ${configuracaoAtual.versao} para ` +
    `${identificadorAncora} em ${gateway.endereco}:${gateway.porta}`
  );
}

function distribuirConfiguracao() {
  for (const identificadorAncora of gatewaysAuxiliares.keys()) {
    enviarConfiguracaoParaGateway(identificadorAncora);
  }
}

function registrarGatewayAuxiliar(partes, origemRemota) {
  if (partes.length !== 3 || partes[2] !== "AUXILIAR") {
    console.warn("[REGISTRO] Mensagem de registro inválida.");
    return;
  }

  const identificadorAncora = partes[1].trim();

  if (!identificadorAncora) {
    console.warn("[REGISTRO] Identificador de âncora vazio.");
    return;
  }

  const gatewayAnterior = gatewaysAuxiliares.get(identificadorAncora);

  gatewaysAuxiliares.set(identificadorAncora, {
    endereco: origemRemota.address,
    porta: origemRemota.port,
    ultimoContatoMs: Date.now(),
    ultimaVersaoEnviada: gatewayAnterior?.ultimaVersaoEnviada ?? 0,
    ultimaVersaoConfirmada: gatewayAnterior?.ultimaVersaoConfirmada ?? 0,
    ultimoEnvioConfiguracaoMs:
      gatewayAnterior?.ultimoEnvioConfiguracaoMs ?? 0,
  });

  console.log(
    `[REGISTRO] ${identificadorAncora} registrado em ` +
    `${origemRemota.address}:${origemRemota.port}`
  );

  enviarConfiguracaoParaGateway(identificadorAncora);
}

function processarConfirmacaoConfiguracao(partes, origemRemota) {
  if (partes.length !== 3) {
    console.warn("[ACK] Confirmação de configuração inválida.");
    return;
  }

  const identificadorAncora = partes[1].trim();
  const versao = inteiroValido(partes[2], 0, Number.MAX_SAFE_INTEGER);
  const gateway = gatewaysAuxiliares.get(identificadorAncora);

  if (!gateway || versao === null) {
    console.warn(`[ACK] Gateway ou versão inválida: ${identificadorAncora}`);
    return;
  }

  gateway.endereco = origemRemota.address;
  gateway.porta = origemRemota.port;
  gateway.ultimoContatoMs = Date.now();
  gateway.ultimaVersaoConfirmada = versao;

  console.log(
    `[ACK] ${identificadorAncora} confirmou a configuração ${versao}`
  );
}

function processarConfiguracaoPrincipal(partes) {
  if (partes.length < 5) {
    console.warn("[CONFIG_PRINCIPAL] Mensagem incompleta.");
    return;
  }

  const identificadorAncora = partes[1].trim();
  const canal = inteiroValido(partes[3], 1, 13);
  const quantidade = inteiroValido(
    partes[4],
    0,
    MAXIMO_MACS_INTERESSE
  );

  if (identificadorAncora !== IDENTIFICADOR_ANCORA_PRINCIPAL) {
    console.warn(
      `[CONFIG_PRINCIPAL] Origem não autorizada: ${identificadorAncora}`
    );
    return;
  }

  if (canal === null || quantidade === null) {
    console.warn("[CONFIG_PRINCIPAL] Canal ou quantidade inválida.");
    return;
  }

  if (partes.length !== 5 + quantidade) {
    console.warn(
      "[CONFIG_PRINCIPAL] Quantidade de MACs diferente da declarada."
    );
    return;
  }

  const macs = partes.slice(5);

  atualizarConfiguracao(
    canal,
    macs,
    "LISTA_COMPLETA_DA_ANCORA_PRINCIPAL"
  );
}

function converterFrameEmObjeto(partes, mensagemBruta) {
  if (partes.length !== 7) {
    return {
      valido: false,
      erro: "Frame deve possuir exatamente 7 campos",
      mensagemBruta,
    };
  }

  const [
    identificadorAncora,
    tempoMsTexto,
    macTexto,
    rssiTexto,
    canalTexto,
    sequenciaTexto,
    retransmissaoTexto,
  ] = partes;

  const tempoMs = inteiroValido(tempoMsTexto, 0, 0xFFFFFFFF);
  const mac = normalizarMac(macTexto);
  const rssi = inteiroValido(rssiTexto, -127, 20);
  const canal = inteiroValido(canalTexto, 1, 13);
  const sequenciaFrame = inteiroValido(sequenciaTexto, 0, 4095);
  const retransmissao = inteiroValido(retransmissaoTexto, 0, 1);

  if (
    !identificadorAncora ||
    tempoMs === null ||
    !mac ||
    rssi === null ||
    canal === null ||
    sequenciaFrame === null ||
    retransmissao === null
  ) {
    return {
      valido: false,
      erro: "Um ou mais campos do frame são inválidos",
      mensagemBruta,
    };
  }

  return {
    valido: true,
    identificadorAncora,
    tempoMs,
    mac,
    rssi,
    canal,
    sequenciaFrame,
    retransmissao,
    recebidoEm: agoraIso(),
  };
}

function processarFrame(partes, mensagemBruta, origemRemota) {
  const frame = converterFrameEmObjeto(partes, mensagemBruta);

  if (!frame.valido) {
    console.warn("[FRAME] Pacote inválido:", {
      origem: `${origemRemota.address}:${origemRemota.port}`,
      ...frame,
    });
    return;
  }

  if (frame.identificadorAncora === IDENTIFICADOR_ANCORA_PRINCIPAL) {
    aprenderConfiguracaoPeloFramePrincipal(frame);
  }

  console.log({
    origem: `${origemRemota.address}:${origemRemota.port}`,
    ...frame,
  });
}

function processarMensagem(mensagemBruta, origemRemota) {
  const mensagem = mensagemBruta.trim();

  if (!mensagem) {
    return;
  }

  const partes = mensagem.split("|");
  const tipoMensagem = partes[0];

  switch (tipoMensagem) {
    case "REGISTRO":
      registrarGatewayAuxiliar(partes, origemRemota);
      return;

    case "ACK_CONFIG":
      processarConfirmacaoConfiguracao(partes, origemRemota);
      return;

    case "CONFIG_PRINCIPAL":
      processarConfiguracaoPrincipal(partes);
      return;

    default:
      processarFrame(partes, mensagem, origemRemota);
  }
}

servidorUDP.on("listening", () => {
  const enderecoAtual = servidorUDP.address();

  console.log(
    `Servidor UDP escutando em ${enderecoAtual.address}:${enderecoAtual.port}`
  );
  console.log(`Âncora principal: ${IDENTIFICADOR_ANCORA_PRINCIPAL}`);
  console.log("Aguardando gateways e frames...\n");
});

servidorUDP.on("message", (mensagemRecebida, origemRemota) => {
  processarMensagem(
    mensagemRecebida.toString("utf8"),
    origemRemota
  );
});

servidorUDP.on("error", (erro) => {
  console.error("Erro no servidor UDP:", erro);
  servidorUDP.close();
});

setInterval(() => {
  const agora = Date.now();

  for (const [identificadorAncora, gateway] of gatewaysAuxiliares) {
    if (!gatewayEstaAtivo(gateway)) {
      continue;
    }

    const configuracaoPendente =
      configuracaoAtual.versao > gateway.ultimaVersaoConfirmada;

    const intervaloAtingido =
      agora - gateway.ultimoEnvioConfiguracaoMs >=
      INTERVALO_REENVIO_CONFIGURACAO_MS;

    if (configuracaoPendente && intervaloAtingido) {
      enviarConfiguracaoParaGateway(identificadorAncora);
    }
  }
}, 1000);

servidorUDP.bind(PORTA_SERVIDOR, ENDERECO_SERVIDOR);
