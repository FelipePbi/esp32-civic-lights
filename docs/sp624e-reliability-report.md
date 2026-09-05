# SP624E Reliability Report

Status: **VALIDADO EM HARDWARE**  
Data: 2026-08-09  
Plataforma: ESP32 + ESP-IDF v6.0.2 + dois controladores SP624E

## Firmware

| Item | Resultado |
|---|---|
| Versão | 0.4.0 |
| Binário | `sp624e_controller.bin`, 458704 bytes (`0x6ffd0`) |
| SHA-256 | `8D18A8A6AA33D5B3DACF921A933B6A1AD9907AC3ECA5FD97B289ED4F4A50FE9F` |
| Partição livre | 56% (`0x90030` bytes) |
| Host tests | PASS: protocolo, parser, mapping, reconciler, filas, stale/coalescing, backoff, invariantes de grupo e debounce NVS |
| Full-clean build | PASS |
| Flash final | PASS; hashes verificados pelo esptool |

## Mapping

| Lado | Endereço | Tipo |
|---|---|---|
| LEFT | `FF:FF:11:CD:AC:FA` | PUBLIC |
| RIGHT | `FF:FF:11:CD:A0:60` | PUBLIC |

Mapping carregado da NVS em todos os reboots finais.

## Connection Manager

| Item | Resultado |
|---|---|
| Máquinas LEFT/RIGHT | PASS; estados e motivos de transição independentes |
| Backoff | PASS: 500 ms, 1 s, 2 s, 4 s, 8 s, 10 s, jitter ±10% |
| Reset do backoff | PASS após 10 s contínuos em READY |
| Scanner | PASS; scan inicial limitado e scan direcionado somente ao lado ausente |
| Healthy side retained | PASS durante reconnect LEFT, RIGHT, stress e mid-command |
| Comando em voo | PASS; cancelado na desconexão sem backoff/falha duplicada |

## Command Queues e State Reconciler

| Item | Resultado |
|---|---|
| Filas LEFT/RIGHT | PASS; estáticas, independentes, 16 posições |
| Serialização | PASS; um worker e um write ativo por lado |
| Generation/stale/coalescing | PASS em testes de host e hardware |
| Strict Sync | PASS; SYNCED exige ambos conectados, READY e geração verificada |
| RGB sincronizado | PASS; dois lados verificados em verde e depois vermelho |
| Restauração dos snapshots | PASS nos dois lados |

## Reconnect individual

| Teste | Resultado | Tempo READY/SYNCED |
|---|---|---|
| LEFT | PASS | 5,949 s na execução isolada; 3,8–4,0 s nos exemplos preservados do stress final |
| RIGHT | PASS | 4,539 s na execução isolada; 4,2–4,6 s nos exemplos preservados do stress final |

Em todos os casos o lado saudável permaneceu READY. O objetivo de recuperação
em até 30 s foi atendido com o ESP32 posicionado próximo ao veículo.

## Alternating Reconnect Stress

| Item | Resultado |
|---|---|
| Ciclos | 10 |
| LEFT | 5/5 PASS |
| RIGHT | 5/5 PASS |
| Total | `pass=10 fail=0 result=PASS` |
| Tentativas | 1 por ciclo no ensaio final |
| Verificação | PASS em todos os ciclos |
| Intervalo | 10,5 s, permitindo reset de backoff após READY estável |

O firmware emitiu métricas individuais por ciclo. Exemplos preservados:
LEFT conectou em 1454–1587 ms e voltou a READY/SYNCED em 3818–3949 ms;
RIGHT conectou em 1530–1819 ms e voltou a READY/SYNCED em 4230–4590 ms.

## Mid-command Failure

| Item | Resultado |
|---|---|
| Lado desconectado | RIGHT |
| Desired State temporário | RED, brilho 64 |
| GROUP entrou em DEGRADED | PASS |
| Reconnect automático | PASS |
| Reconcile + query + verify | PASS |
| Retorno a SYNCED | PASS |
| Desync | 3990 ms |
| Restauração final | PASS nos dois lados |

## Reboot e persistência

| Item | Resultado |
|---|---|
| Mapping restaurado | PASS |
| LEFT/RIGHT auto-connect | PASS |
| READY/SYNCED | PASS, ~18,9 s no reboot final |
| Mudança visual no boot | Nenhuma; confirmação visual do usuário |
| Gravação Desired State | `generation=1 result=ESP_OK` |
| Carga após novo reboot | `generation=1 restore_on_boot=0` |
| Escritas visuais no boot | Nenhuma; somente CCCD e state query |

## Long-running Stability Test

| Item | Resultado |
|---|---|
| Duração | 900000 ms (15 minutos) |
| Resultado | PASS |
| LEFT disconnects | 0 |
| RIGHT disconnects | 0 |
| Health queries | 90 LEFT / 90 RIGHT |
| Initial/final heap | 147256 / 147256 bytes |
| Minimum heap | 146692 bytes no início e no fim |
| Memory leak evidente | Nenhuma |
| Panic/watchdog/reboot | Nenhum |

Uma tentativa anterior terminou em 699 s porque o carro desligou ambos os
faróis automaticamente. A queda quase simultânea foi confirmada fisicamente
pelo usuário e não foi contabilizada como falha do firmware. O ensaio final foi
repetido com alimentação contínua e passou por 900 s.

## BLE e ambiente

| Item | Resultado |
|---|---|
| RSSI próximo ao veículo | aproximadamente -81 a -88 dBm |
| Parâmetros solicitados | intervalo 50 ms, latency 0, supervision 2560 ms |
| Queda controlada | `0x216 CONNECTION_TERMINATED_LOCALLY` |
| Perda de alimentação/link | `0x208 CONNECTION_SUPERVISION_TIMEOUT` |
| Falha de estabelecimento observada | `0x23E CONNECTION_FAILED_TO_BE_ESTABLISHED` |

Com RSSI de -94 a -100 dBm houve falhas frequentes. Aproximar o ESP32 eliminou
essa limitação no ensaio final.

## Physical Power-cycle

`SKIPPED_PHYSICAL_POWER_CYCLE` para desligamento individual, pois o veículo
desliga os dois controladores conjuntamente. O corte conjunto foi observado e a
recuperação automática ocorreu, mas não substitui o teste individual opcional.

## Conclusão

Critérios obrigatórios atendidos. Firmware detecta falha, preserva lado saudável,
recupera com backoff, consulta estado real, reconcilia geração desejada, verifica
os dois lados e só então declara o grupo SYNCED.
