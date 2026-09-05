# Arquitetura de runtime 0.4.0

## Fluxo

```text
GAP / GATT callback
        ↓ evento curto
Connection Manager task
        ↓ transporte pronto
LEFT worker          RIGHT worker
        ↓ filas independentes
STATE QUERY / parser
        ↓
State Reconciler
        ↓ query de verificação
Group State
```

O callback GAP apenas atualiza o registry, copia notifications para uma fila e
publica eventos. Backoff, scans, decisões de pipeline e timeouts ficam na task do
Connection Manager. Reassembly e parse ficam na task do Group Controller. Cada
worker realiza no máximo um write por vez no próprio FFE1.

## Tasks e filas

- NimBLE Host Task: stack do host BLE.
- `connection_mgr`: eventos e timers de conectividade.
- `left_cmd` e `right_cmd`: writes, queries e reconciliação serializados.
- `group_runtime`: notifications, console, estado do grupo, health e NVS.
- `runtime_console`: entrada UART em linhas.
- `reliability_test`: testes que aguardam o runtime sem bloquear callbacks.

As filas de comando têm 16 posições por lado. O comando contém `id`,
`generation`, tipo, payload, tamanho, retry e flag de verificação. RGB, effect,
brightness e reconcile pendentes são coalescíveis. No dequeue, uma geração menor
que a Desired State atual é descartada.

## Timeouts

| Operação | Timeout |
|---|---:|
| Connect NimBLE | 30 s |
| Discovery GATT | 10 s |
| Write / CCCD | 2 s |
| State Query | 2,5 s |
| Reconcile completo | 5 s |
| Pipeline após GATT | 6 s |

Um write tem somente um retry enquanto o transporte continua saudável. Segunda
falha marca a conexão unhealthy e transfere recuperação ao Connection Manager.
