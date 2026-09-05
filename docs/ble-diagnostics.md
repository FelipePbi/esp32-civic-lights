# Console de diagnóstico BLE

Ferramenta de engenharia reversa embarcada para investigar um terceiro
dispositivo BLE sem interferir nos SP624E. Ela observa e envia somente o que o
operador digitar: não existe fuzzing, brute force, varredura de valores nem
qualquer write automático.

## Princípio de prioridade

```text
1. Farol LEFT
2. Farol RIGHT
3. runtime normal do ESP32
4. diagnóstico do terceiro dispositivo
```

O NimBLE possui um único papel *master*: scan e connect competem entre si. O
Connection Manager chama `ble_diagnostics_release_master()` imediatamente antes
de precisar do rádio, na mesma task e no mesmo ciclo em que chamaria
`start_connect()` ou `begin_recovery_scan()`. O diagnóstico cancela seu scan ou
sua tentativa de conexão de forma síncrona e o papel master já está livre
quando a recuperação do SP624E o solicita.

Consequências:

- um scan de diagnóstico **é abortado** quando um farol precisa reconectar;
- uma conexão de diagnóstico **já estabelecida não é derrubada**, porque um link
  aberto não bloqueia scan nem connect;
- com um farol permanentemente ausente, o Connection Manager alterna entre scan
  de 3 s e backoff, então o diagnóstico recebe janelas curtas. A tabela de
  dispositivos é cumulativa entre sessões até `diag scan clear`, portanto vários
  scans curtos produzem o mesmo resultado de um scan longo.

O diagnóstico nunca chama `ble_connection*`, `sp624e_*`, Desired State, Group
Controller ou o registry dos SP624E. Ele mantém tabelas próprias.

## Ativação

O modo começa **desligado** a cada boot e não é persistido. Comandos que usam o
rádio são recusados até `diag enable`. `diag status`, `diag scan list`,
`diag scan clear`, `diag target` e `diag help` funcionam sempre.

O console é a mesma UART0 a 115200 baud usada pelos comandos do SP624E. O
leitor de linha agora pertence a `main/console/runtime_console.c` e sobe no
boot, e não mais junto do `sp624e_controller`, então `diag` responde mesmo com
os faróis desligados, ausentes ou ainda não conectados.

## Comandos

```text
diag help
diag status
diag enable
diag disable

diag scan start [seconds]
diag scan stop
diag scan list [address]
diag scan clear

diag target <address>
diag target clear

diag connect
diag disconnect

diag gatt

diag read <service_uuid> <characteristic_uuid>

diag subscribe <service_uuid> <characteristic_uuid>
diag unsubscribe <service_uuid> <characteristic_uuid>

diag write <service_uuid> <characteristic_uuid> <hex>
diag write_nr <service_uuid> <characteristic_uuid> <hex>
```

Formatos aceitos:

| Campo | Formato |
|---|---|
| Endereço | `AA:BB:CC:DD:EE:FF`, `AA-BB-...` ou `AABBCCDDEEFF` |
| UUID | `FFE0`, `0000FFE0` ou `0000ffe0-0000-1000-8000-00805f9b34fb` |
| Payload | `7E000503FF0000EF`, `7E 00 05 03`, `0x7E:00:05` |

## Scan

Scan ativo (recebe scan response), duração padrão de 15 s e máximo de 60 s,
com janela de 30 ms a cada 30 ms (100% de duty cycle). Dispositivos são
deduplicados por endereço em uma tabela de 32 entradas.

- primeira aparição: bloco completo com nome, endereço, tipo de endereço, RSSI,
  UUIDs de serviço, manufacturer data, service data, TX power, appearance,
  flags, ADV RAW e SCAN RESPONSE RAW;
- aparições seguintes: nada, exceto uma linha curta quando o payload ou o nome
  mudam.

`diag scan list` imprime a tabela resumida; `diag scan list <address>` reimprime
o bloco completo de um dispositivo, reparseando os payloads brutos guardados.

## GATT

`diag gatt` faz discovery completo de serviços, characteristics e descriptors e
imprime handles, value handles, propriedades e a lista de candidatos com
`WRITE`, `WRITE_NO_RESPONSE`, `NOTIFY` ou `INDICATE`.

A lista de candidatos é apenas um filtro por propriedade. **Propriedade não
identifica canal de cor**; isso só se confirma observando o controlador real.

Limites por conexão: 16 serviços, 48 characteristics e 48 descriptors, iguais
aos do caminho SP624E (`APP_BLE_MAX_*`). Excedentes são descartados com aviso.

## Read, subscribe e write

Todas as operações exigem `diag gatt` executado antes, porque a resolução é
feita pelo par (service UUID, characteristic UUID) sobre o perfil descoberto.
Havendo mais de um par idêntico, o primeiro é usado e a duplicidade é avisada.

Validações antes de qualquer operação:

| Verificação | Efeito |
|---|---|
| Modo habilitado | recusa com `diag enable` sugerido |
| Conexão aberta | recusa se não conectado |
| Perfil descoberto | recusa se `diag gatt` não rodou |
| Characteristic existe | recusa com o par informado |
| Propriedade compatível | `READ`, `WRITE` ou `WRITE_NO_RESPONSE` conforme o comando |
| `NOTIFY`/`INDICATE` + CCCD | subscribe exige descriptor `0x2902` |
| HEX válido | dígito inválido, comprimento ímpar, vazio ou maior que 64 bytes |
| Tamanho vs MTU | recusa payload maior que `MTU - 3` |

`subscribe` escreve `01 00` (notification) ou `02 00` (indication, quando a
characteristic só suporta indicate); `unsubscribe` escreve `00 00`. O slot só
vira `active` depois da confirmação do periférico.

Writes registram exatamente os bytes enviados antes do envio. `write` aguarda
confirmação; `write_nr` apenas informa que foi enfileirado sem ACK.

## Logging

```text
[BLE-DIAG][<uptime>s][SCAN]
[BLE-DIAG][<uptime>s][CONNECT]
[BLE-DIAG][<uptime>s][DISCONNECT]
[BLE-DIAG][<uptime>s][GATT]
[BLE-DIAG][<uptime>s][READ]
[BLE-DIAG][<uptime>s][WRITE]
[BLE-DIAG][<uptime>s][NOTIFY]
[BLE-DIAG][<uptime>s][STATUS]
[BLE-DIAG][<uptime>s][HELP]
[BLE-DIAG][<uptime>s][ERROR]
```

Exemplo real do formato emitido pelo ESP-IDF:

```text
I (12432) BLE-DIAG: [12.432][WRITE] WRITE handle=0x0003 length=8 bytes=7E 00 05 03 FF 00 00 EF
```

Com o diagnóstico desativado e ocioso não há nenhum log deste componente.

## Filtro de duplicatas do host

`CONFIG_BT_NIMBLE_HOST_QUEUE_CONG_CHECK` foi desligado em `sdkconfig.defaults`.

Com ele ligado, `ble_hs_hci_evt.c` descarta todo advertising report cujo
endereço já foi visto no scan atual, e a checagem acontece antes de olhar o tipo
do pacote. Como o `ADV_IND` sempre chega antes do `SCAN_RSP`, o scan response
era sempre descartado e o **nome do dispositivo nunca era capturável** — que é
justamente o campo que identifica um controlador desconhecido.

Medido no hardware, scan ativo de 15 s com os dois faróis conectados:

| | Pacotes | Dispositivos | Scan responses | Nomes |
|---|---:|---:|---:|---:|
| Filtro ligado, duty 30% | 8 | 8 | 0 | 0 |
| Filtro desligado, duty 100% | 634 | 11 | sim | 4 |

Justificativa da mudança global: em regime normal, com ambos os lados `READY`,
não existe scan algum, então a opção é irrelevante; os scans de recuperação são
curtos, direcionados e cancelam no primeiro match, logo mais reports apenas
fazem o match chegar antes. Validado no carro: LEFT e RIGHT permaneceram
`READY` por 278 s, `GROUP SYNCED`, `reconnects=0`, inclusive durante scan a 100%
de duty cycle e com a terceira conexão aberta.

## Recursos e limites

| Item | Valor |
|---|---|
| Stack BLE | NimBLE, central + observer, BLE-only |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` | 3 |
| `CONFIG_BTDM_CTRL_BLE_MAX_CONN` | 3 |
| Conexões usadas normalmente | 2 (LEFT + RIGHT) |
| Conexão de diagnóstico | a terceira, dentro do limite já configurado |
| Tabela de scan | 32 dispositivos |
| Subscriptions simultâneas | 8 |
| Payload máximo de write | 64 bytes, limitado também por `MTU - 3` |
| Terceira conexão simultânea | validada no carro: LEFT + RIGHT + diagnóstico, MTU 247 |

O limite de conexões não precisou mudar: o projeto já reservava três. A única
alteração de `sdkconfig.defaults` foi desligar o filtro de duplicatas do host,
descrita acima. `diag status` mostra heap livre, heap mínimo, maior bloco livre
e quantas conexões estão em uso para acompanhar o custo real durante o teste.

## Arquitetura

```text
UART0
  ↓ runtime_console (task própria, sobe no boot)
  ├── handler "diag"  → ble_diagnostics (executa na task do console)
  └── fallback        → fila existente → group_runtime → comandos SP624E
```

O handler do SP624E apenas repassa a linha para a fila que já existia, então os
comandos SP624E continuam sendo processados em `group_runtime`, sem mudança de
concorrência.

Chamadas NimBLE nunca acontecem com o mutex do diagnóstico retido, o que
elimina inversão de lock entre a task do console, a host task do NimBLE e a
`connection_mgr`.
