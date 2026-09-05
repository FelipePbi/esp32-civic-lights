# SP624E RF REMOTE + INDICATOR REPORT

Data: 2026-08-15. Status: RF/Police/PWA concluídos. Welcome foi rejeitado no
teste físico por iniciar vários segundos após a alimentação e foi removido do
firmware/API/PWA em `0.7.6`. Candidata robusta `0.7.7` compilada; flash pendente.

## Firmware

- Version candidata: `0.7.7`
- Build full clean: PASS (`.\scripts\build.ps1 -FullClean`)
- App binary: `0x107020` bytes; 49% da partição de 2 MB livre
- App SHA-256: `1DF3D3BC485EB118F0716E6E090796C1975D4E616494CFC7CF7C72818ED00919`
- Web SHA-256: `F5CAEFAD8F9C9BA96A48E894139866C213C7CB2794C27B1BE31FE5154CE58BE0`
- Flash `0.7.1`: PASS em COM5; firmware e PWA com hashes verificados
- Flash `0.7.2`: PASS em COM5; todos os hashes verificados
- Flash `0.7.4`: PASS em COM5; todos os hashes verificados
- Flash `0.7.6`: PASS em COM5; bootloader, partição, SPIFFS e app com hashes verificados
- Runtime USB `0.7.6`: boot normal, SPIFFS/HTTP/RF/BLE inicializados e nenhum
  módulo/evento Welcome presente
- Runtime USB após flash: boot `0.7.5` normal; Wi-Fi/HTTP/RF/BLE inicializados,
  mapping NVS READY, sem panic/watchdog

## RX480E

- GPIO25 / D0: configurado, input pulldown
- GPIO26 / D1: configurado, input pulldown
- GPIO27 / D2: configurado, input pulldown
- GPIO32 / D3: configurado, input pulldown
- GPIO33 / VT: configurado, input pulldown, confirmação auxiliar

## Physical Mapping

- Button A (lógico 1): D3 / GPIO32
- Button B (lógico 2): D2 / GPIO27
- Button C (lógico 3): D1 / GPIO26
- Button D (lógico 4): D0 / GPIO25

Mapping identificado após parear o controle no modo momentâneo do RX480E e
reconfirmado no firmware final com `mapping=READY`.

## Input Tests

- Single press: PASS físico A/B/C/D; um evento por botão
- Long press: PASS físico A/B/C/D; um evento por botão em ~2 s
- Duplicate suppression: PASS
- Release detection: PASS
- Debounce: 50 ms, PASS
- RX480E bancada: PASS para cliques A/B/C/D; canais únicos confirmados por serial
- Discovery seguro repetível + runner interativo: implementado; execução pendente

## Button 1

- Action: WHITE real, brilho 255, via Group Controller
- Mock/plan: PASS
- Hardware/final group/indicator: PASS

## Button 2

- Action: RGB 255/0/0, brilho 255 (100%)
- Mock/plan: PASS
- Hardware/final group/indicator: PASS

## Button 3 / Police

- Pattern: RED → OFF → RED → OFF → BLUE → OFF; brilho fixo 255 inclusive no
  apagão RGB 0/0/0, evitando rampa de brilho no flash seguinte
- Velocidades NVS/PWA (ciclo): 1800/1350/1110/900 ms; `very_fast` usa 150 ms/fase
- Queue behavior: apagões usam barreira FIFO e fluxo adaptativo; ordem coberta por teste host
- Toggle/cancel/timeout: implementado; timeout 30 s
- Strict LEFT/RIGHT: usa sessão única e dispatch pareado existente
- Physical duration/sync/restore: PASS em `0.7.2`; 66/66 frames, sem drop,
  brilho aprovado e restore `SYNCED`

## Button 4

- Default: Favorite
- Ações: Favorite, RGB, WHITE e Police
- NVS encode/decode e API validation: PASS
- Configuração física atual: RGB WHITE `#FFFFFF`, brilho 100%
- NVS/reboot/PWA: PASS; usuário confirmou configuração atual correta

## LED Indicator

- GPIO: 23; hardware ativo LOW; boot HIGH/inativo
- WHITE/RGB/Police/offline policy: PASS host
- Bancada: PASS visual; sequência única OFF → ON → OFF

## PWA

- Página própria `/remote`, atalho na Home, editor Button D, velocidade Police e autoteste LED: implementado
- Realtime snapshot/event support: implementado
- Unit/build: PASS; 18 testes frontend
- Playwright E2E/WCAG: PASS; 27 testes nas quatro rotas atuais
- Browser/hardware: PASS no iPhone; rota própria `/remote`, Button D configurável
  persistido após reboot e autoteste LED confirmado

## Mock / Host Tests

- Protocol e regressão existente: PASS
- RF mapping/debounce/duplicate/release: PASS
- Remote action plans: PASS
- Police timeline/30 s queue stress: PASS
- Indicator policy: PASS
- Remote API/JSON contracts: PASS

## Physical Test

- A/WHITE: ambos verificados pelo State Query; PASS
- B/RED 100%: LEFT/RIGHT verificados por State Query como RGB 255/0/0,
  brightness 255; PASS técnico
- C/Police: iniciou, mas botão A expôs corrida de geração na restauração
- Falha observada: geração nova descartou reconcile antigo sem limpar
  `reconcile_queued`; grupo ficou `RECONCILING`, filas vazias, verified=0
- Correção: cancelamento aguarda restore; descarte de geração limpa flag órfã
- Reteste `0.7.1`: restauração PASS; usuário ainda percebeu roxo no padrão;
  perfil inicial de 280 ms gerou backlog 10/11. Padrão final alterado para
  RED/OFF/RED/OFF/BLUE/OFF, fase mínima 150 ms e admissão só com filas livres
- Reteste do novo Police `0.7.1`: usuário confirmou ausência de roxo; fila
  observada no máximo 1. Brilho percebido baixo porque OFF ainda enviava
  brightness 0; `0.7.2` mantém brightness 255 e apaga por RGB 0/0/0
- Reteste `0.7.2`: brilho Police aprovado; 66/66 frames aceitos, sem drop,
  restauração verificada, filas finais 0
- Button D: RGB 255/255/255 brightness 255 verificado em LEFT/RIGHT e confirmado
  correto pelo usuário
- Inconsistência encontrada em `C → A → D`: `SOLID` expunha o último RGB Police
  antes do payload D. `0.7.3` passa a pré-carregar RGB antes de ativar `SOLID`
- Welcome `0.7.5`: mesmo iniciando antes da reconciliação padrão, o usuário mediu
  quase 8 s desde a alimentação até a animação; comportamento rejeitado
- `0.7.6`: Welcome removido integralmente; conexão volta ao fluxo normal de
  State Query, reconciliação e verificação sem staging de animação
- Faróis desligados imediatamente após falha; nenhuma depuração com bateria ligada

## BLE Regression e Safety

- Firmware `0.7.7` full-clean build: PASS
- App binary: cabe na partição de 2 MB
- Police after reboot: OFF por construção
- Single-side Police: bloqueado por `both_ready` e cancelamento em disconnect
- BLE: timeout e falha crítica entram em `RECOVERING`, com terminate, limpeza
  local forçada após 2 s e backoff limitado
- Group API: lock/resposta com request ID e timeouts finitos; RF não depende de
  cliente PWA nem de broadcast WebSocket
- Supervisor: heartbeats BLE/Group/RF/LED/Web; reinício ~10 s após última
  atividade; Task Watchdog 12 s; reset reason e contadores no diagnóstico
- Heartbeats 64-bit protegidos por seção crítica no ESP32 32-bit
- Panic/watchdog/unexpected reboot da candidata `0.7.7`: pendente bancada/carro

## Open Issues

1. Conectar ESP32 CH9102, preservar NVS, gravar `0.7.7` e validar RF/LED/PWA
   sem faróis; COM4 atual (`VID_1F3A`) não é a placa.
2. Executar teste final curto no carro, incluindo controle sem PWA conectado e
   `C → A → D`, observando reset reason/contadores.
3. Para espelhamento cromático futuro, substituir o LED auto-RGB de dois
   terminais por LED RGB controlável/endereçável.

## Recommended Next Step

Manter faróis desligados durante full-clean/flash. Depois validar somente
`C → A → D`, sempre exigindo restore `SYNCED`.
