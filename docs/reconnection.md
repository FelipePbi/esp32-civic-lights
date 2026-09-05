# Reconexão automática

## Máquina por lado

```text
UNKNOWN → DISCONNECTED → CONNECTING → CONNECTED → DISCOVERING
→ SUBSCRIBING → QUERYING_STATE → SYNC_PENDING → RECONCILING → READY

falha GATT/timeout → RECOVERING → DISCONNECTED → BACKOFF
queda normal → DISCONNECTED → BACKOFF → WAITING_FOR_ADV → CONNECTING
queda compatível com power-cycle → FAST_RECOVERY → primeiro ADV → CONNECTING
```

Toda transição inclui motivo no log. BLE conectado não equivale a READY.
`SYNC_PENDING` significa CCCD ativo e State Query válida, mas ainda sem prova de
reconcile/verify. Apenas `READY` comprova pipeline completo.

## Backoff e scanner

Falhas normais de conexão, GATT ou desconexão usam 500 ms, 1 s, 2 s, 4 s, 8 s e
depois 10 s, com jitter de ±10%. Um scan que apenas confirma que o periférico
continua desligado usa 1,5 s fixo e não incrementa o contador de falhas. Após
10 s contínuos em READY, a penalidade volta ao início. RSSI não dispara reconnect.

Queda estável súbita, supervision timeout ou perda dupla em até 500 ms entra em
janela `FAST_RECOVERY` de 5 s. Scan ativo usa 30 ms/30 ms, duplicate filtering
desabilitado e somente MACs ausentes. Primeiro advertising cancela scan e inicia
conexão no mesmo ciclo do manager. `0x3E` retorna ao scan após 150 ms. Expirada a
janela, fluxo cai automaticamente no backoff normal.

No boot ocorre scan global limitado. Durante recuperação, scanner procura
somente endereços ausentes e é cancelado assim que advertising válido aparece.
Quando ambos estão READY, não há scan ativo. A conexão saudável nunca é
terminada para recuperar a outra.

Reconnect conhecido tenta handles FFE1/CCCD cacheados em RAM. CCCD é sempre
reativado e State Query sempre executada. Erro/timeout no fast path invalida
cache e inicia full GATT discovery na mesma conexão.

Conexão começa com baseline de 2560 ms e solicita 1500 ms sem mudar interval ou
latency. Pedido e valor aceito são registrados. Candidatos 1200/1000 ms dependem
de validação física posterior.

## Recuperação de travamento

Falha crítica não depende da fila normal do manager: cada lado possui mailbox
crítica substituível. Timeout de conexão, discovery, pipeline ou callback de
disconnect entra em `RECOVERING`, encerra link e força limpeza local após 2 s se
NimBLE não responder. Depois usa mesmo backoff limitado.

Tasks de conexão, grupo, RF, indicador e WebSocket publicam heartbeat e alimentam
Task Watchdog de 12 s. Supervisor inicia após 25 s de grace, cobrindo scan BLE
inicial de 15 s sem falso positivo; depois detecta heartbeat
parado por 2 s e reinicia ESP após mais 8 s. Reset reason, componente anterior,
heap, recuperações e drops ficam em `system_health` no diagnóstico. RSSI baixo
continua sendo somente diagnóstico.

## Correção: trava em WAITING_FOR_ADV — 2026-09-05

Sintoma relatado e reproduzido no carro: **após desligar e religar o carro, um
dos faróis parava de responder permanentemente**, e só um reboot do ESP32
recuperava. O ESP32 permanece energizado pelo USB durante o ciclo, então ele
sobrevive à queda; os SP624E não.

### Mecanismo

O scan de recuperação procura os dois lados em uma única operação, com
`s_scan_mask` marcando quem está sendo procurado. Quando o advertising de um
lado chegava, `EVENT_ADV_FOUND` cancelava o scan e zerava a **máscara inteira**,
mas só fazia avançar o lado encontrado. O outro permanecia em
`WAITING_FOR_ADV`, e a partir daí:

- não existe timeout para `WAITING_FOR_ADV` em `process_timers()`;
- `begin_recovery_scan()` só considera lados em `BACKOFF`.

O lado não encontrado nunca mais era procurado. Trava permanente.

Evidência capturada:

```text
I (99519)  BLE_SCAN: NORMAL recovery scan targets=...AC:FA,...A0:60
I (101700) LEFT conectou
           (nenhum scan de recuperação nos 240 s seguintes)
I (340989) RIGHT state=WAITING_FOR_ADV connected=NO  ← parado desde 101700
```

O mesmo vazamento existia em `start_connect()`, `complete_disconnect()` e
`begin_recovery()`: todos cancelavam o scan compartilhado e limpavam a máscara
sem devolver o outro lado a um estado que voltasse a ser escaneado.

### Correção

`release_recovery_scan(keep, reason)` centraliza o cancelamento: limpa a
máscara, para o scan e devolve a `BACKOFF` — via `enter_absent_backoff()`, que
usa o retry fixo de 1,5 s e **não** incrementa o contador de falhas — todo lado
que estava na máscara, estava em `WAITING_FOR_ADV` e não é o lado que motivou o
cancelamento.

Os quatro pontos de cancelamento passaram a usar o helper. Nenhuma outra
alteração no fluxo de reconexão, backoff, Strict Sync ou máquina de estados.


## Testes e evidência

Comandos:

```text
test-reconnect left
test-reconnect right
test-stress
test-midfail
test-stability 900
```

`test-reconnect` usa terminate local marcado explicitamente como simulação; não
confirma power loss físico. Cada ciclo registra lado, detecção, tempo até
CONNECTED/READY/SYNCED, tentativas,
comandos e verificação. Resultados históricos de hardware estão em
`sp624e-reliability-report.md`. Recuperação/supervisor 0.8.0 possuem testes host
e clean build; validação após flash deve confirmar reset reason e operação RF
sem cliente PWA conectado.
