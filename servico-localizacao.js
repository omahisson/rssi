//codigo anne

import { promises as fs } from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";


// =====================================================
// CONFIGURAÇÕES DO SERVIDOR
// =====================================================

const PORTA_HTTP = 3001;

const ANCORA_01 = "ANCORA_01";
const ANCORA_02 = "ANCORA_02";
const ANCORA_03 = "ANCORA_03";

const IDENTIFICADORES_ANCORAS = [
  ANCORA_01,
  ANCORA_02,
  ANCORA_03,
];

const MAC_PROBE_INICIAL = "9A98234C9B9D";


// =====================================================
// COORDENADAS DAS ÂNCORAS
// =====================================================
//
// As coordenadas são calculadas a partir das três
// distâncias informadas pela interface.
//
// A organização visual mantém:
//
// ANCORA_02 no alto à esquerda
// ANCORA_03 no alto à direita
// ANCORA_01 na parte inferior
//

const DISTANCIAS_PADRAO = {
  ancora1Ancora2: 16.9706,
  ancora1Ancora3: 16.9706,
  ancora2Ancora3: 24,
};

const TAMANHO_MINIMO_MAPA = {
  largura: 28,
  altura: 16,
};

const MARGEM_MAPA_METROS = 2;


// =====================================================
// PARÂMETROS DO RSSI
// =====================================================

const A = -51.3;
const n = 3.0;

const NUM_LEITURAS_RSSI = 10;


// =====================================================
// TEMPOS DAS LEITURAS
// =====================================================
//
// O tempo de 5 segundos protege a trilateração contra
// combinações de leituras reais muito distantes no tempo.
//
// O tempo de 3 minutos controla apenas quando uma âncora
// deixa de ser exibida como REAL e volta para SIMULADA.
//

const TEMPO_MAXIMO_LEITURA_MS = 5000;
const TEMPO_ANCORA_REAL_MS = 180000;


// =====================================================
// ARQUIVOS LOCAIS
// =====================================================

const caminhoArquivoAtual = fileURLToPath(import.meta.url);
const diretorioAtual = path.dirname(caminhoArquivoAtual);
const caminhoConfiguracao = path.join(
  diretorioAtual,
  "configuracao-localizacao.json"
);
const diretorioGravacoes = path.join(diretorioAtual, "gravacoes");


// =====================================================
// ESTADO DO SERVIÇO
// =====================================================

let configuracaoDistancias = { ...DISTANCIAS_PADRAO };
let macProbeAtual = MAC_PROBE_INICIAL;
let servidorHTTPIniciado = false;
let gravacaoAtual = null;
let intervaloGravacao = null;

const dispositivosLocalizacao = new Map();
const ultimasLocalizacoes = new Map();


// =====================================================
// FUNÇÕES AUXILIARES
// =====================================================

function agoraIso() {
  return new Date().toISOString();
}

function normalizarMac(macInformado) {
  const macLimpo = String(macInformado ?? "")
    .toUpperCase()
    .replace(/[^0-9A-F]/g, "");

  return /^[0-9A-F]{12}$/.test(macLimpo)
    ? macLimpo
    : null;
}

function formatarMac(mac) {
  return mac.match(/.{2}/g)?.join(":") ?? mac;
}

function limitar(valor, minimo, maximo) {
  return Math.min(maximo, Math.max(minimo, valor));
}

function arredondar(valor, casas = 4) {
  return Number(valor.toFixed(casas));
}

function responderJson(res, status, dados) {
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
  });
  res.end(JSON.stringify(dados));
}

async function lerCorpoJson(req) {
  const partes = [];
  let tamanho = 0;

  for await (const parte of req) {
    tamanho += parte.length;

    if (tamanho > 1024 * 1024) {
      throw new Error("Corpo da requisição maior que 1 MB.");
    }

    partes.push(parte);
  }

  if (partes.length === 0) {
    return {};
  }

  return JSON.parse(Buffer.concat(partes).toString("utf8"));
}


// =====================================================
// RSSI → DISTÂNCIA
// =====================================================

function calcularDistancia(rssi) {
  return Math.pow(
    10,
    (A - rssi) / (10 * n)
  );
}


// =====================================================
// DISTÂNCIA → RSSI SIMULADO
// =====================================================

function calcularRssiSimulado(distancia, indiceAncora, tempo) {
  const ruido = Math.sin(tempo * 1.35 + indiceAncora * 2.1) * 1.6;

  return Math.round(
    A -
    10 * n * Math.log10(Math.max(1, distancia)) +
    ruido
  );
}


// =====================================================
// MÉDIA DO RSSI
// =====================================================

function calcularMediaRSSI(historico) {
  if (!historico || historico.length === 0) {
    return null;
  }

  let soma = 0;

  for (const rssi of historico) {
    soma += rssi;
  }

  return soma / historico.length;
}


// =====================================================
// CONFIGURAÇÃO GEOMÉTRICA DAS ÂNCORAS
// =====================================================

function validarDistancias(distancias) {
  const valores = [
    Number(distancias?.ancora1Ancora2),
    Number(distancias?.ancora1Ancora3),
    Number(distancias?.ancora2Ancora3),
  ];

  if (valores.some((valor) => !Number.isFinite(valor) || valor <= 0)) {
    return {
      valida: false,
      erro: "As três distâncias devem ser números maiores que zero.",
    };
  }

  const [distancia12, distancia13, distancia23] = valores;
  const formaTriangulo =
    distancia12 + distancia13 > distancia23 &&
    distancia12 + distancia23 > distancia13 &&
    distancia13 + distancia23 > distancia12;

  if (!formaTriangulo) {
    return {
      valida: false,
      erro: "As distâncias informadas não formam um triângulo.",
    };
  }

  return {
    valida: true,
    distancias: {
      ancora1Ancora2: distancia12,
      ancora1Ancora3: distancia13,
      ancora2Ancora3: distancia23,
    },
  };
}

function calcularGeometriaAncoras(distancias) {
  const distancia12 = distancias.ancora1Ancora2;
  const distancia13 = distancias.ancora1Ancora3;
  const distancia23 = distancias.ancora2Ancora3;

  const xAncora1 =
    (distancia12 ** 2 - distancia13 ** 2 + distancia23 ** 2) /
    (2 * distancia23);

  const alturaAoQuadrado = distancia12 ** 2 - xAncora1 ** 2;
  const altura = Math.sqrt(Math.max(0, alturaAoQuadrado));

  const menorX = Math.min(0, xAncora1);
  const maiorX = Math.max(distancia23, xAncora1);
  const larguraTriangulo = maiorX - menorX;

  const largura = Math.max(
    TAMANHO_MINIMO_MAPA.largura,
    larguraTriangulo + MARGEM_MAPA_METROS * 2
  );
  const alturaMapa = Math.max(
    TAMANHO_MINIMO_MAPA.altura,
    altura + MARGEM_MAPA_METROS * 2
  );

  const deslocamentoX = (largura - larguraTriangulo) / 2 - menorX;
  const deslocamentoY = (alturaMapa - altura) / 2;

  return {
    tamanho: {
      largura: arredondar(largura),
      altura: arredondar(alturaMapa),
    },
    ancoras: [
      {
        id: ANCORA_01,
        nome: "A1",
        cor: "#7463a8",
        posicao: {
          x: arredondar(xAncora1 + deslocamentoX),
          y: arredondar(deslocamentoY),
        },
      },
      {
        id: ANCORA_02,
        nome: "A2",
        cor: "#176b5c",
        posicao: {
          x: arredondar(deslocamentoX),
          y: arredondar(altura + deslocamentoY),
        },
      },
      {
        id: ANCORA_03,
        nome: "A3",
        cor: "#397e93",
        posicao: {
          x: arredondar(distancia23 + deslocamentoX),
          y: arredondar(altura + deslocamentoY),
        },
      },
    ],
  };
}


// =====================================================
// TRILATERAÇÃO
// =====================================================
//
// Recebe as três distâncias do probe até as âncoras.
// Retorna X e Y estimados.
//

function calcularPosicao(distanciasProbe, ancoras) {
  const ancora1 = ancoras.find((ancora) => ancora.id === ANCORA_01)?.posicao;
  const ancora2 = ancoras.find((ancora) => ancora.id === ANCORA_02)?.posicao;
  const ancora3 = ancoras.find((ancora) => ancora.id === ANCORA_03)?.posicao;

  if (!ancora1 || !ancora2 || !ancora3) {
    return null;
  }

  const d1 = distanciasProbe[ANCORA_01];
  const d2 = distanciasProbe[ANCORA_02];
  const d3 = distanciasProbe[ANCORA_03];

  const A1 = 2 * (ancora2.x - ancora1.x);
  const B1 = 2 * (ancora2.y - ancora1.y);
  const C1 =
    d1 * d1 -
    d2 * d2 -
    ancora1.x * ancora1.x +
    ancora2.x * ancora2.x -
    ancora1.y * ancora1.y +
    ancora2.y * ancora2.y;

  const A2 = 2 * (ancora3.x - ancora1.x);
  const B2 = 2 * (ancora3.y - ancora1.y);
  const C2 =
    d1 * d1 -
    d3 * d3 -
    ancora1.x * ancora1.x +
    ancora3.x * ancora3.x -
    ancora1.y * ancora1.y +
    ancora3.y * ancora3.y;

  const denominador = A1 * B2 - A2 * B1;

  // Evita divisão por zero caso as três âncoras estejam alinhadas.
  if (Math.abs(denominador) < 0.000001) {
    return null;
  }

  return {
    x: (C1 * B2 - C2 * B1) / denominador,
    y: (A1 * C2 - A2 * C1) / denominador,
  };
}


// =====================================================
// ESTRUTURA DOS DADOS DE LOCALIZAÇÃO
// =====================================================
//
// Para cada MAC:
//
// MAC
//   ├── ANCORA_01
//   │      └── histórico RSSI
//   ├── ANCORA_02
//   │      └── histórico RSSI
//   └── ANCORA_03
//          └── histórico RSSI
//

function obterDispositivo(mac) {
  if (!dispositivosLocalizacao.has(mac)) {
    dispositivosLocalizacao.set(mac, {
      mac,
      ancoras: new Map(),
    });
  }

  return dispositivosLocalizacao.get(mac);
}


// =====================================================
// ADICIONA UMA LEITURA DE RSSI
// =====================================================

export function adicionarLeituraRSSI(frame) {
  const mac = normalizarMac(frame.mac);

  if (!mac || !IDENTIFICADORES_ANCORAS.includes(frame.identificadorAncora)) {
    return;
  }

  const dispositivo = obterDispositivo(mac);
  macProbeAtual = mac;

  if (!dispositivo.ancoras.has(frame.identificadorAncora)) {
    dispositivo.ancoras.set(frame.identificadorAncora, {
      historicoRSSI: [],
      ultimaLeituraMs: 0,
      ultimaLeituraIso: null,
    });
  }

  const ancora = dispositivo.ancoras.get(frame.identificadorAncora);
  ancora.historicoRSSI.push(frame.rssi);

  // Mantém somente as últimas 10 leituras.
  if (ancora.historicoRSSI.length > NUM_LEITURAS_RSSI) {
    ancora.historicoRSSI.shift();
  }

  ancora.ultimaLeituraMs = Date.now();
  ancora.ultimaLeituraIso = frame.recebidoEm ?? agoraIso();

  if (gravacaoAtual && !gravacaoAtual.pausada) {
    gravacaoAtual.frames.push({
      decorridoMs: calcularDecorridoGravacao(),
      frame: {
        identificadorAncora: frame.identificadorAncora,
        tempoMs: frame.tempoMs,
        mac,
        rssi: frame.rssi,
        canal: frame.canal,
        sequenciaFrame: frame.sequenciaFrame,
        retransmissao: frame.retransmissao,
        recebidoEm: frame.recebidoEm,
      },
    });
  }
}


// =====================================================
// DADOS SIMULADOS PARA ÂNCORAS AUSENTES
// =====================================================

function gerarPosicaoSimulada(tempo, tamanho) {
  const centroX = tamanho.largura / 2;
  const centroY = tamanho.altura / 2;
  const margem = Math.min(3, tamanho.largura / 5, tamanho.altura / 5);

  const x =
    centroX +
    tamanho.largura * 0.26 * Math.sin(tempo * 0.43) +
    tamanho.largura * 0.06 * Math.sin(tempo * 0.91);
  const y =
    centroY +
    tamanho.altura * 0.24 * Math.cos(tempo * 0.36) +
    tamanho.altura * 0.07 * Math.sin(tempo * 0.72);

  return {
    x: limitar(x, margem, tamanho.largura - margem),
    y: limitar(y, margem, tamanho.altura - margem),
  };
}

function calcularDistanciaEntrePontos(pontoA, pontoB) {
  return Math.hypot(pontoB.x - pontoA.x, pontoB.y - pontoA.y);
}


// =====================================================
// MONTA O ESTADO UTILIZADO PELA INTERFACE
// =====================================================

function montarEstadoAtual(mac = macProbeAtual, agora = Date.now()) {
  const macNormalizado = normalizarMac(mac) ?? macProbeAtual;
  const dispositivo = dispositivosLocalizacao.get(macNormalizado);
  const geometria = calcularGeometriaAncoras(configuracaoDistancias);
  const tempo = agora / 1000;
  const posicaoSimulada = gerarPosicaoSimulada(tempo, geometria.tamanho);
  let podeRecalcularPosicao = true;

  const leituras = geometria.ancoras.map((ancora, indice) => {
    const leituraReal = dispositivo?.ancoras.get(ancora.id);
    const idadeLeitura = leituraReal
      ? agora - leituraReal.ultimaLeituraMs
      : Number.POSITIVE_INFINITY;
    const origemReal = idadeLeitura <= TEMPO_ANCORA_REAL_MS;

    if (origemReal) {
      const rssiMedio = calcularMediaRSSI(leituraReal.historicoRSSI);

      if (idadeLeitura > TEMPO_MAXIMO_LEITURA_MS) {
        podeRecalcularPosicao = false;
      }

      return {
        ancoraId: ancora.id,
        distancia: arredondar(calcularDistancia(rssiMedio)),
        rssi: Math.round(rssiMedio),
        origem: "real",
        restanteRealMs: Math.max(0, TEMPO_ANCORA_REAL_MS - idadeLeitura),
        ultimaLeituraReal: leituraReal.ultimaLeituraIso,
      };
    }

    const distancia = calcularDistanciaEntrePontos(
      posicaoSimulada,
      ancora.posicao
    );

    return {
      ancoraId: ancora.id,
      distancia: arredondar(distancia),
      rssi: calcularRssiSimulado(distancia, indice, tempo),
      origem: "simulada",
      restanteRealMs: 0,
      ultimaLeituraReal: leituraReal?.ultimaLeituraIso ?? null,
    };
  });

  const distanciasProbe = Object.fromEntries(
    leituras.map((leitura) => [leitura.ancoraId, leitura.distancia])
  );
  const novaPosicao = podeRecalcularPosicao
    ? calcularPosicao(distanciasProbe, geometria.ancoras)
    : null;

  if (novaPosicao) {
    ultimasLocalizacoes.set(macNormalizado, {
      posicao: {
        x: arredondar(novaPosicao.x),
        y: arredondar(novaPosicao.y),
      },
      atualizadoEm: agoraIso(),
    });
  }

  const ultimaLocalizacao = ultimasLocalizacoes.get(macNormalizado);
  const posicao = ultimaLocalizacao?.posicao ?? posicaoSimulada;

  return {
    probe: {
      id: "PROBE_01",
      nome: "Probe 01",
      mac: formatarMac(macNormalizado),
    },
    ancoras: geometria.ancoras,
    tamanho: geometria.tamanho,
    distanciasAncoras: { ...configuracaoDistancias },
    leituras,
    posicao: {
      x: arredondar(posicao.x),
      y: arredondar(posicao.y),
    },
    atualizadoEm: ultimaLocalizacao?.atualizadoEm ?? agoraIso(),
    posicaoCongelada: !podeRecalcularPosicao,
  };
}


// =====================================================
// PERSISTÊNCIA DA CONFIGURAÇÃO
// =====================================================

async function carregarConfiguracao() {
  try {
    const conteudo = await fs.readFile(caminhoConfiguracao, "utf8");
    const arquivo = JSON.parse(conteudo);
    const resultado = validarDistancias(arquivo.distancias);

    if (resultado.valida) {
      configuracaoDistancias = resultado.distancias;
    }
  } catch (erro) {
    if (erro.code !== "ENOENT") {
      console.warn("[LOCALIZAÇÃO] Configuração ignorada:", erro.message);
    }
  }
}

async function salvarConfiguracao(distancias) {
  const conteudo = {
    distancias,
    atualizadoEm: agoraIso(),
  };

  await fs.writeFile(
    caminhoConfiguracao,
    `${JSON.stringify(conteudo, null, 2)}\n`,
    "utf8"
  );
}


// =====================================================
// GRAVAÇÃO E REPRODUÇÃO
// =====================================================

function criarIdentificadorGravacao() {
  return new Date()
    .toISOString()
    .replace(/[:.]/g, "-");
}

function calcularDecorridoGravacao(agora = Date.now()) {
  if (!gravacaoAtual) {
    return 0;
  }

  const pausaAtualMs = gravacaoAtual.pausada
    ? agora - gravacaoAtual.pausadaEmMs
    : 0;
  const tempoExecutandoMs =
    agora -
    gravacaoAtual.iniciadoEmMs -
    gravacaoAtual.tempoPausadoMs -
    pausaAtualMs;

  return Math.max(0, tempoExecutandoMs);
}

function registrarAmostraGravacao() {
  if (!gravacaoAtual || gravacaoAtual.pausada) {
    return;
  }

  gravacaoAtual.amostras.push({
    decorridoMs: calcularDecorridoGravacao(),
    estado: montarEstadoAtual(),
  });
}

function iniciarGravacao() {
  if (gravacaoAtual) {
    throw new Error("Já existe uma gravação em andamento.");
  }

  const iniciadoEmMs = Date.now();
  const id = criarIdentificadorGravacao();

  gravacaoAtual = {
    id,
    nome: `Gravação ${new Date(iniciadoEmMs).toLocaleString("pt-BR")}`,
    iniciadoEm: new Date(iniciadoEmMs).toISOString(),
    iniciadoEmMs,
    pausada: false,
    pausadaEmMs: null,
    tempoPausadoMs: 0,
    configuracao: {
      distanciasAncoras: { ...configuracaoDistancias },
      potenciaReferencia: A,
      expoentePerda: n,
      quantidadeLeiturasMedia: NUM_LEITURAS_RSSI,
    },
    frames: [],
    amostras: [],
  };

  registrarAmostraGravacao();
  intervaloGravacao = setInterval(registrarAmostraGravacao, 500);

  return {
    id: gravacaoAtual.id,
    nome: gravacaoAtual.nome,
    iniciadoEm: gravacaoAtual.iniciadoEm,
    pausada: gravacaoAtual.pausada,
  };
}

function pausarGravacao() {
  if (!gravacaoAtual) {
    throw new Error("Não existe gravação em andamento.");
  }

  if (!gravacaoAtual.pausada) {
    registrarAmostraGravacao();
    gravacaoAtual.pausada = true;
    gravacaoAtual.pausadaEmMs = Date.now();
  }

  return {
    id: gravacaoAtual.id,
    pausada: gravacaoAtual.pausada,
  };
}

function retomarGravacao() {
  if (!gravacaoAtual) {
    throw new Error("Não existe gravação em andamento.");
  }

  if (gravacaoAtual.pausada) {
    gravacaoAtual.tempoPausadoMs +=
      Date.now() - gravacaoAtual.pausadaEmMs;
    gravacaoAtual.pausada = false;
    gravacaoAtual.pausadaEmMs = null;
    registrarAmostraGravacao();
  }

  return {
    id: gravacaoAtual.id,
    pausada: gravacaoAtual.pausada,
  };
}

async function pararGravacao() {
  if (!gravacaoAtual) {
    throw new Error("Não existe gravação em andamento.");
  }

  clearInterval(intervaloGravacao);
  intervaloGravacao = null;
  registrarAmostraGravacao();

  const finalizadoEmMs = Date.now();
  const gravacaoFinalizada = {
    ...gravacaoAtual,
    finalizadoEm: new Date(finalizadoEmMs).toISOString(),
    duracaoMs: calcularDecorridoGravacao(finalizadoEmMs),
  };

  delete gravacaoFinalizada.iniciadoEmMs;
  delete gravacaoFinalizada.pausada;
  delete gravacaoFinalizada.pausadaEmMs;
  delete gravacaoFinalizada.tempoPausadoMs;

  const caminho = path.join(
    diretorioGravacoes,
    `${gravacaoFinalizada.id}.json`
  );

  await fs.writeFile(
    caminho,
    `${JSON.stringify(gravacaoFinalizada, null, 2)}\n`,
    "utf8"
  );

  gravacaoAtual = null;

  return resumirGravacao(gravacaoFinalizada);
}

function resumirGravacao(gravacao) {
  return {
    id: gravacao.id,
    nome: gravacao.nome,
    iniciadoEm: gravacao.iniciadoEm,
    finalizadoEm: gravacao.finalizadoEm,
    duracaoMs: gravacao.duracaoMs,
    quantidadeAmostras: gravacao.amostras?.length ?? 0,
    quantidadeFrames: gravacao.frames?.length ?? 0,
  };
}

async function listarGravacoes() {
  const arquivos = await fs.readdir(diretorioGravacoes);
  const gravacoes = [];

  for (const arquivo of arquivos.filter((nome) => nome.endsWith(".json"))) {
    try {
      const conteudo = await fs.readFile(
        path.join(diretorioGravacoes, arquivo),
        "utf8"
      );
      gravacoes.push(resumirGravacao(JSON.parse(conteudo)));
    } catch (erro) {
      console.warn(`[GRAVAÇÃO] Arquivo ${arquivo} ignorado:`, erro.message);
    }
  }

  return gravacoes.sort((a, b) =>
    b.iniciadoEm.localeCompare(a.iniciadoEm)
  );
}

async function obterGravacao(id) {
  if (!/^[0-9TZ-]+$/.test(id)) {
    return null;
  }

  try {
    const caminho = path.join(diretorioGravacoes, `${id}.json`);
    const conteudo = await fs.readFile(caminho, "utf8");
    return JSON.parse(conteudo);
  } catch (erro) {
    if (erro.code === "ENOENT") {
      return null;
    }

    throw erro;
  }
}


// =====================================================
// SERVIDOR HTTP PARA A INTERFACE REACT
// =====================================================

const servidorHTTP = http.createServer(async (req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");

  if (req.method === "OPTIONS") {
    res.writeHead(204);
    res.end();
    return;
  }

  try {
    const url = new URL(req.url, `http://localhost:${PORTA_HTTP}`);

    if (req.method === "GET" && url.pathname === "/api/estado") {
      const macSolicitado = normalizarMac(url.searchParams.get("mac"));
      responderJson(res, 200, montarEstadoAtual(macSolicitado ?? macProbeAtual));
      return;
    }

    // Mantém a rota criada originalmente para a interface React.
    if (req.method === "GET" && url.pathname === "/api/localizacoes") {
      responderJson(res, 200, [montarEstadoAtual()]);
      return;
    }

    if (req.method === "GET" && url.pathname === "/api/configuracao/ancoras") {
      responderJson(res, 200, {
        distancias: configuracaoDistancias,
        ...calcularGeometriaAncoras(configuracaoDistancias),
      });
      return;
    }

    if (req.method === "PUT" && url.pathname === "/api/configuracao/ancoras") {
      const corpo = await lerCorpoJson(req);
      const resultado = validarDistancias(corpo.distancias);

      if (!resultado.valida) {
        responderJson(res, 400, { erro: resultado.erro });
        return;
      }

      configuracaoDistancias = resultado.distancias;
      ultimasLocalizacoes.clear();
      await salvarConfiguracao(configuracaoDistancias);

      responderJson(res, 200, {
        distancias: configuracaoDistancias,
        ...calcularGeometriaAncoras(configuracaoDistancias),
      });
      return;
    }

    if (req.method === "GET" && url.pathname === "/api/gravacoes") {
      responderJson(res, 200, {
        gravando: Boolean(gravacaoAtual),
        gravacaoAtual: gravacaoAtual
          ? {
              id: gravacaoAtual.id,
              nome: gravacaoAtual.nome,
              iniciadoEm: gravacaoAtual.iniciadoEm,
              pausada: gravacaoAtual.pausada,
            }
          : null,
        gravacoes: await listarGravacoes(),
      });
      return;
    }

    if (req.method === "POST" && url.pathname === "/api/gravacoes/iniciar") {
      responderJson(res, 201, iniciarGravacao());
      return;
    }

    if (req.method === "POST" && url.pathname === "/api/gravacoes/parar") {
      responderJson(res, 200, await pararGravacao());
      return;
    }

    if (req.method === "POST" && url.pathname === "/api/gravacoes/pausar") {
      responderJson(res, 200, pausarGravacao());
      return;
    }

    if (req.method === "POST" && url.pathname === "/api/gravacoes/retomar") {
      responderJson(res, 200, retomarGravacao());
      return;
    }

    if (req.method === "GET" && url.pathname.startsWith("/api/gravacoes/")) {
      const id = decodeURIComponent(url.pathname.slice("/api/gravacoes/".length));
      const gravacao = await obterGravacao(id);

      if (!gravacao) {
        responderJson(res, 404, { erro: "Gravação não encontrada." });
        return;
      }

      responderJson(res, 200, gravacao);
      return;
    }

    if (req.method === "GET" && url.pathname === "/") {
      res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
      res.end("Servidor de localização RSSI funcionando.");
      return;
    }

    responderJson(res, 404, { erro: "Rota não encontrada." });
  } catch (erro) {
    const mensagem = erro instanceof SyntaxError
      ? "JSON inválido."
      : erro.message;
    const status =
      mensagem.includes("gravação em andamento") ||
      mensagem.includes("Não existe gravação")
        ? 409
        : 500;

    console.error("[HTTP] Erro ao processar requisição:", erro);
    responderJson(res, status, { erro: mensagem });
  }
});


// =====================================================
// INICIA SERVIDOR HTTP
// =====================================================

export async function iniciarServicoLocalizacao() {
  if (servidorHTTPIniciado) {
    return;
  }

  await fs.mkdir(diretorioGravacoes, { recursive: true });
  await carregarConfiguracao();

  await new Promise((resolve, reject) => {
    servidorHTTP.once("error", reject);
    servidorHTTP.listen(PORTA_HTTP, "0.0.0.0", () => {
      servidorHTTP.off("error", reject);
      servidorHTTPIniciado = true;

      console.log(`Servidor HTTP disponível na porta ${PORTA_HTTP}`);
      console.log(
        `API de localização: http://localhost:${PORTA_HTTP}/api/estado`
      );

      resolve();
    });
  });
}
