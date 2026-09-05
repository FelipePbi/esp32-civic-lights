# SP624E HEADLIGHT-SWITCH RECOVERY REPORT

## Firmware

- Version: 0.8.0
- ESP-IDF: 6.0.2
- Target: ESP32-D0WD-V3
- Build: PASS (`scripts/build.ps1 -FullClean`)
- Full-clean final: PASS em 2026-08-22 BRT
- Firmware binary: 1,091,760 bytes; SHA-256 `06BD64A1BA63461637A762CE9796955736A9D328B87D94A2A21C66BC86B318BD`
- Web SPIFFS binary: 2,031,616 bytes; SHA-256 `AE09C2CCB3D52E22B28C666649FF97064FC1D392981BD56CBE228F74D399C52D`
- App partition livre: 48% (`0xF5750` bytes)
- Flash final: PASS em COM5 a 115200 bit/s; bootloader, partition table, web e
  aplicação foram verificados por hash pelo esptool. NVS `0x9000` foi preservada
- A primeira tentativa manual encontrou o driver CH9102 travado antes da escrita;
  após reconectar USB e repetir BOOT + RESET, a gravação completou normalmente
- CH9102 confirmado em COM5, VID/PID `1A86:55D4`
- A tentativa final a 460800 bit/s perdeu o fluxo durante upload do stub, antes de
  qualquer escrita; a repetição a 115200 bit/s completou e verificou todos os hashes
- Preflight final: PASS com os hashes acima e NVS ausente do plano de flash
- Baseline serial capturado em sessão curta; log bruto:
  `logs/recovery-bench/20260821-124634-COM5-serial.log` (11,186 bytes)
- Boot 0.8.0 capturado por 60 s; log bruto:
  `logs/recovery-bench/20260821-154951-COM5-serial.log` (24,441 bytes)
- Firmware atualmente gravado e confirmado no serial: 0.8.0

## Baseline

- Firmware anterior usado como baseline: 0.7.7
- ESP32-D0WD-V3 rev. 3.1, flash 4 MB e boot normal
- Plano de flash não contém `0x9000`; NVS não foi apagada nem gravada pelo host
- O boot atualizou somente a calibração PHY pelo próprio firmware; mapping persistido
  continuou `READY`
- Wi-Fi SoftAP, HTTP, RX480E e NimBLE inicializaram sem panic/watchdog no trace curto
- Old supervision timeout observado: 2560 ms
- Código anterior solicitava: 10000 ms
- Old average recovery: sem medição confiável disponível

## Root Cause Evidence

- LEFT loss: confirmado nas três transições controladas
- RIGHT loss: confirmado nas três transições controladas
- Both simultaneous: confirmado; eventos GAP ficaram separados por 0-102 ms nos
  ciclos simétricos e mais espaçados quando o timeout adaptativo diferiu por RSSI
- Disconnect reasons: `0x208 CONNECTION_SUPERVISION_TIMEOUT` em todas as perdas
  físicas observadas
- No-advertising duration: média 2,769 s; máximo 4,116 s nos seis traces laterais
- Likely power cycle: **LIKELY CONTROLLER POWER INTERRUPTION CONFIRMED** para as
  transições de energização testadas

Classificação continua heurística no firmware. A confirmação deste relatório usa a
correlação externa: comando humano de transição, perda dupla por supervision timeout,
ausência dos dois MACs e retorno posterior por advertising compatível com reboot.
Os rótulos específicos DRL/AUTO/farol baixo não foram instrumentados externamente;
o ensaio registrou transições reais de energização disponíveis no veículo.

Queda de link estável só entra como indício de power-cycle quando Wi-Fi AP e
heartbeat RF continuam operacionais. RSSI fraco tem precedência como provável
perda RF, salvo evidência forte de supervision timeout ou perda dupla.

## Fast Recovery

- Implemented: sim
- Fast scan: ativo, 30 ms interval / 30 ms window, janela 5 s
- Duplicate filtering: desabilitado durante FAST_RECOVERY
- Connect on ADV: sim; primeiro ADV válido agenda conexão imediatamente
- Fast retry: 150 ms após falha/0x3E
- Connection attempt: 3000 ms durante fase rápida
- Fallback backoff: 500 ms, 1 s, 2 s, 4 s, 8 s, 10 s
- Dual recovery: scan aceita LEFT e RIGHT ausentes; conexão sequencial rápida
- Connect-with-scan: deliberadamente não habilitado na primeira candidata
- NimBLE 0x3E reattempt: habilitado, máximo 2; manager continua autoridade
- Hardware: FAST_RECOVERY, fallback normal e retorno automático a `SYNCED` foram
  observados; nenhum ciclo exigiu reset ou intervenção BLE manual

## Connection Parameters

- Initial supervision: 2560 ms
- Requested supervision: 1500 ms
- Accepted supervision: 1500 ms em RSSI de -78 a -87 dBm; 4000 ms permitido ao
  pedido do peer quando um lado chegou ao limiar fraco de -88 dBm
- Latency: preservada
- Connection interval: preservado
- False disconnects: zero em 110 s contínuos com ambos a 1500 ms (-82/-78 dBm),
  mais 67 s após o último ciclo; todas as quedas coincidiram com transições físicas
- O primeiro trace mostrou que o peer elevava 1500 para 4000 ms. A política final
  responde 1500 ms em sinal utilizável e aceita o valor maior somente com RSSI fraco
- 1200/1000 ms: não selecionados sem evidência física

## GATT Fast Path

- Cached handles: RAM, por MAC conhecido
- FFE1: cache validado após full discovery
- CCCD: sempre reativado após reconnect
- Query: obrigatória antes de `SYNC_PENDING`
- Fallback discovery: erro/timeout de CCCD ou query invalida cache e dispara full discovery
- READY: somente após query, reconcile e verificação
- Hardware: fast path observado repetidamente em LEFT e RIGHT; GATT ready ocorreu
  62-108 ms depois de `CONNECTED`, sempre seguido de CCCD e State Query

## RF During Recovery

- RX480E task permanece independente do BLE
- Button events received: PASS em hardware. D3/BUTTON_A foi recebido e mudou o
  Desired State 1107 -> 1108 (WHITE)
- Intenções RF estáticas atualizam Desired State imediatamente mesmo com BLE offline;
  Police continua exigindo ambos READY/SYNCED
- Latest Desired State preserved: coberto por teste host de gerações RED → PURPLE → WHITE
- Stale commands discarded: coberto por fila geracional
- Partial Strict Sync: bloqueado por `SYNC_PENDING`; dispatch exige ambos elegíveis
- Verificação física RF: os dois lados receberam a mesma geração, foram consultados,
  verificados e voltaram a `SYNCED` em 710 ms; persistência NVS retornou `ESP_OK`
- RF especificamente durante a janela de recovery não foi acionado no carro; a
  preservação da última geração durante recovery permanece coberta pelo teste host K

## Host / Software Tests

- A-C: outages 100/500/1500 ms dentro da janela rápida — PASS
- D-E: perda dupla simultânea/defasada em até 500 ms — PASS policy
- F-G: ADV após 200/1000 ms — PASS policy
- H-I: um/dois 0x3E usam retry curto limitado — PASS policy
- J: desligado 30 s cai no recovery normal — PASS policy
- K: vários comandos durante recovery preservam somente última geração — PASS
- L: disconnect durante Police cancela runtime e restaura via Desired State — código auditado; bancada pendente
- M: Welcome Animation não existe no firmware 0.8.0 atual; nenhum módulo foi reintroduzido
- Duplicate scan profile — PASS: active scan, duplicate filter off, 100% duty temporário
- Timeout de CONNECTING cancela explicitamente a tentativa NimBLE e possui deadline
  adicional para ausência do callback de cancelamento
- Callback GATT de conexão antiga é ignorado por handle/contexto de transporte
- `last_ble_rx` e `last_state_query` possuem captura e trace independentes; o
  timestamp BLE é coletado no callback e processado fora dele pela fila existente
- Software disconnect — usa FAST_RECOVERY marcado como simulação; não confirma power loss
- Host C tests — PASS
- Web unit tests — 19 PASS
- Playwright — 27 PASS, inclusive Home 402 × 874 sem scroll e WCAG A/AA
- Diagnóstico PWA mostra tempo, motivo, classificação e supervision timeout de
  LEFT/RIGHT separadamente

## Car Test

- Number of lighting transitions: 3, executadas por sequência curta; nenhum stress
  prolongado ou dezenas de alternâncias
- Modos: transições controladas de desenergizado/energizado dos dois controladores;
  nomes DRL/AUTO/baixo não estavam disponíveis na telemetria
- Todos os ciclos: LEFT e RIGHT desconectaram, foram classificados como
  `LIKELY_POWER_CYCLE`, reapareceram, usaram query/verificação e voltaram a `SYNCED`

| Transição | Disconnect | Sem ADV até primeiro retorno | ADV -> estado válido | ADV -> GROUP SYNCED |
|---|---|---:|---:|---:|
| 1 | LEFT+RIGHT, `0x208` | 2,827 / 2,897 s | 0,711 / 1,403 s | 1,724 / 1,847 s |
| 2 | LEFT+RIGHT, `0x208` | 4,116 / 1,406 s | 1,324 / 1,034 s | 5,858 / 1,438 s |
| 3 | LEFT+RIGHT, `0x208` | 2,926 / 2,440 s | 1,064 / 0,848 s | 1,401 / 1,867 s |

Valores em cada célula são LEFT / RIGHT. O máximo de 5,858 s ocorreu com perdas
defasadas durante uma sequência rápida e a barreira Strict Sync; não houve aplicação
parcial. Logs principais:

- `logs/recovery-bench/20260822-123149-COM5-serial.log`: causa inicial e RF físico
- `logs/recovery-bench/20260822-134158-COM5-serial.log`: 110 s estáveis a 1500 ms
- `logs/recovery-bench/20260822-134404-COM5-serial.log`: queda longa, fallback e cache
- `logs/recovery-bench/20260822-140904-COM5-serial.log`: três transições medidas

## Performance

- Average detection (GAP event -> FAST_RECOVERY): 0 ms
- Maximum detection (GAP event -> FAST_RECOVERY): 0 ms
- Corte físico -> GAP: não recebeu timestamp externo; limitado aproximadamente pelo
  supervision timeout negociado de 1,5 s, ou 4,0 s no lado em RSSI fraco
- Average ADV -> valid State Query: 1,064 s
- Maximum ADV -> valid State Query: 1,403 s
- Average ADV -> GROUP SYNCED: 2,356 s nos seis traces laterais
- Maximum ADV -> GROUP SYNCED: 5,858 s no ciclo defasado
- Average total recovery: 5,125 s por trace lateral; 6,348 s por transição/grupo
- Maximum total recovery: 9,974 s, incluindo 4,116 s sem advertising e espera Strict Sync

Firmware separa `disconnect → first ADV` de `ADV → READY/SYNCED`; tempo sem ADV
é boot/alimentação do periférico, não tempo de recuperação do ESP32.

## Safety

- Desired State/mapping/RF/Button4/NVS preservados
- Command/query/reassembly/live handles limpos no disconnect
- Police cancelada no disconnect
- State machines possuem deadlines
- Panic/watchdog/unexpected reboot: zero nas capturas físicas finais
- Queue/memory leak: nenhuma fila crescente; heap livre final 27,244 bytes e mínimo
  26,292 bytes, estáveis após três transições
- Partial color application: zero; Strict Sync reteve a conclusão até ambos verificarem

## Conclusion

Firmware 0.8.0 final implementado, flashado e validado no carro. Os traces confirmam
interrupção provável de alimentação dos dois controladores nas transições testadas.
O Connection Manager detectou `0x208`, entrou em FAST_RECOVERY, usou cache GATT,
reativou CCCD, consultou estado, preservou Desired State/Strict Sync e retornou a
`SYNCED` sem intervenção manual. A recuperação do ESP32 após o primeiro ADV ficou
normalmente abaixo de 2 s; o tempo total continua limitado pelo boot/advertising do
SP624E e pela chegada defasada dos dois lados.

## Recommended next step

1. Manter 1500 ms com fallback adaptativo a 4000 ms em RSSI <= -88 dBm; não reduzir
   para 1200/1000 ms sem ensaio separado.
2. Se a interrupção visual continuar indesejável, avaliar fora desta tarefa uma fonte
   estável adequada para os SP624E; não modificar chicote sem projeto elétrico.
3. Opcional: repetir uma única transição com marcação externa do seletor DRL/AUTO/
   baixo para nomear precisamente os modos, sem novo stress prolongado.
