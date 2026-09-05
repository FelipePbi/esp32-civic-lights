# Engenharia reversa do controlador da iluminação interna

Registro da investigação do kit de iluminação ambiente RGB instalado dentro do
carro, com o objetivo futuro de controlá-lo pelo mesmo ESP32 que já controla os
faróis SP624E.

**Nada neste documento pode ser preenchido por hipótese.** Somente evidência
obtida do hardware real, através do console de diagnóstico
([ble-diagnostics.md](ble-diagnostics.md)), entra nas tabelas.

Status: **DISPOSITIVO IDENTIFICADO, PERFIL GATT MAPEADO E COMANDO RGB
CONFIRMADO VISUALMENTE.** Vermelho, verde, azul e branco foram validados no
carro; os três canais são independentes e combináveis. O protocolo continua
**parcialmente desconhecido**: brilho, efeitos, liga/desliga e o significado dos
bytes de moldura seguem sem evidência. Cada payload enviado ao controlador foi
fornecido explicitamente pelo operador; nenhum foi gerado automaticamente.

Evidência coletada em 2026-09-05, carro ligado, iluminação interna ligada, ESP32
com LEFT e RIGHT conectados durante todo o teste.

## Hardware

```text
Kit:
AliExpress 1005003666858928

Controller:
módulo BLE com perfil serial transparente FFE0/FFE1
(mesmos UUIDs do SP624E, propriedades diferentes)

Bluetooth type:
BLE — CONFIRMADO (anuncia ADV_IND, aceita conexão GATT)

Aplicativo usado hoje:
não informado
```

## Discovery

**CONFIRMADO** por scan ativo de 15 s.

```text
Name:                LEDCAR-00-1900   (via SCAN RESPONSE)
Address:             20:23:12:15:00:19
Address type:        public
RSSI:                -76 dBm (melhor) / -88 dBm (típico)
Packet type:         ADV_IND  (conectável)
Advertisement RAW:   02 01 06          (somente Flags 0x06)
Manufacturer:        ausente
Services advertised: ausentes no advertisement
Service data:        ausente
TX power:            ausente
Pacotes no scan:     139 ADV + 115 SCAN_RESPONSE em 15 s
```

O advertisement carrega apenas Flags; o nome só existe no scan response. Isso
tornou o dispositivo invisível até o filtro de duplicatas do host NimBLE ser
desligado (ver [ble-diagnostics.md](ble-diagnostics.md)).

### Inventário completo do scan, kit LIGADO

| # | Endereço | Tipo | RSSI melhor | ADV/RSP | Nome | Observação |
|---|---|---|---|---|---|---|
| 1 | `20:23:12:15:00:19` | public | -76 | 139/115 | **LEDCAR-00-1900** | **candidato confirmado** |
| 2 | `79:B6:2A:A5:24:15` | random | -56 | 33/27 | — | Apple `0x004C` |
| 3 | `76:C4:B8:6E:93:6E` | random | -56 | 59/0 | — | Apple `0x004C` |
| 4 | `6E:CF:1B:35:45:D9` | random | -56 | 42/33 | — | Apple `0x004C` |
| 5 | `BC:02:6E:F5:F5:EF` | public | -97 | 2/1 | `ffe8e0` | manufacturer `0x4242` |
| 6 | `3C:0B:59:45:80:31` | public | -82 | 52/41 | `TY` | serviço `0xA201` |
| 7 | `6C:0F:7C:3A:F4:C0` | random | -55 | 40/33 | — | Apple `0x004C` |
| 8 | `08:D1:F9:5E:A3:82` | public | -94 | 3/0 | `P20400A025200307` | serviços `0x00EE`/`0x00FF` |
| 9 | `D2:84:77:3B:46:77` | random | -56 | 6/0 | — | Apple `0x004C` |
| 10 | `CE:CA:9A:09:01:4B` | random | -58 | 5/0 | — | Apple `0x004C` |
| 11 | `BC:35:1E:F8:FB:AC` | public | -96 | 3/0 | — | não conectável |

Os SP624E não aparecem porque estavam conectados e portanto não anunciavam.

O teste comparativo kit desligado x ligado **não foi necessário** para
identificar: o nome `LEDCAR-00-1900` é inequívoco. Ele continua sendo útil como
confirmação independente, se houver dúvida.

## GATT

**CONFIRMADO** por discovery completo em conexão real.

```text
Connection handle:      2  (terceira conexão simultânea)
MTU:                    247  → payload GATT máximo de 244 bytes
Connection parameters:  interval=40 (50,00 ms) latency=0 supervision=256 (2560 ms)
Disconnect reason:      0x216 (encerrado localmente pelo operador)
```

| Service | Characteristic | Def / Value handle | Properties | Descriptors | Observações |
|---|---|---|---|---|---|
| `0xFFE0` (`0x0001`–`0xFFFF`) | `0xFFE1` | `0x0002` / `0x0003` | `0x16` = READ, WRITE_NO_RESPONSE, NOTIFY | CCCD `0x2902` em `0x0004` | único serviço e única characteristic do dispositivo |

Fatos adicionais observados:

- **Não existe WRITE com resposta.** Só `WRITE_NO_RESPONSE`. Portanto o comando
  a usar é `diag write_nr`, nunca `diag write`.
- **READ é declarado mas rejeitado.** `ble_gattc_read` no value handle `0x0003`
  retornou status `270` (`0x10E` = ATT error `0x0E`, *Unlikely Error*).
- **CCCD aceita subscribe.** Escrita de `01 00` em `0x0004` foi confirmada pelo
  periférico; `00 00` também.
- **Nenhuma notification espontânea** em 30 s subscrito e ocioso. O controlador
  aparentemente só responde quando comandado.

### Comparação com o SP624E

| Item | SP624E (faróis) | LEDCAR-00-1900 (interno) |
|---|---|---|
| Serviço | `0xFFE0` | `0xFFE0` |
| Characteristic | `0xFFE1` value `0x0003` | `0xFFE1` value `0x0003` |
| Properties | `0x1C` WRITE, WRITE_NO_RSP, NOTIFY | `0x16` READ, WRITE_NO_RSP, NOTIFY |
| CCCD | `0x2902` em `0x0004` | `0x2902` em `0x0004` |
| Outros serviços | `5833ff01-...` | nenhum |
| Manufacturer no ADV | `0x5053` + prefixo `0F 00` | ausente |
| Endereço | `FF:FF:11:CD:xx:xx` | `20:23:12:15:00:19` |

Os UUIDs coincidem porque `FFE0`/`FFE1` é o perfil serial transparente genérico
de módulos BLE baratos. **Isso não implica protocolo igual** e não autoriza
reutilizar os comandos BanlanX v3 do SP624E.

## Impacto no sistema, medido

| Momento | Free heap | Heap mínimo | Conexões |
|---|---:|---:|---:|
| Diagnóstico desligado | 23 148 | 16 320 | 2 de 3 |
| Após `diag enable` (7 360 bytes de tabelas) | 15 772 | 13 028 | 2 de 3 |
| Durante scan de 15 s a 100% duty | 15 772 | 11 532 | 2 de 3 |
| Com a terceira conexão + GATT | 18 060 | 9 832 | **3 de 3** |
| Subscrito, com cliente Wi-Fi conectado | 15 784 | 8 708 | 3 de 3 |
| Após `diag disable` | 23 056 | 8 708 | 2 de 3 |

LEFT e RIGHT permaneceram `READY` em toda a sessão: `GROUP state=SYNCED`,
`generation=1165`, `reconnects=0`, 278 s de uptime contínuo, nenhuma
desconexão, nenhum watchdog. A terceira conexão simultânea é viável.

O heap mínimo de 8 708 bytes é o ponto de atenção. Manter o diagnóstico
desligado quando não estiver em uso.

## Incidente de estabilidade — 2026-09-05, perda simultânea das três conexões

Ocorrido **entre** o teste do vermelho e o teste do verde, com o diagnóstico
habilitado e ocioso, sem nenhum comando sendo enviado.

```text
LEFT   disconnect_evt: 1282265 ms  reason=0x208  classification=LIKELY_POWER_CYCLE
RIGHT  disconnect_evt: 1282132 ms  reason=0x208  classification=LIKELY_POWER_CYCLE
DIAG   LEDCAR         reason=0x208
```

`0x208` = `BLE_HS_HCI_ERR(0x08)` = *Connection Timeout* (supervision timeout).
As três conexões caíram dentro de uma janela de 133 ms.

Estado observado durante a janela:

```text
GROUP state=POWER_CYCLE_RECOVERY
LEFT   WAITING_FOR_ADV connected=NO
RIGHT  BACKOFF         connected=NO
Connections in use: 0 of 3
Minimum free heap: 2 740 bytes
```

Recuperação, automática e sem intervenção:

```text
LEFT   first_adv 1610382 ms → connected 1610622 → gatt_ready 1610719
       → notify_ready 1610874 → state_valid 1611041 → synced 1612184
RIGHT  first_adv 1610812 ms → connected 1611424 → gatt_ready 1611486
       → notify_ready 1611626 → state_valid 1611841 → synced 1612267

Recovery after ADV:  LEFT 1 802 ms | RIGHT 1 455 ms
Total recovery:      LEFT 329 919 ms | RIGHT 330 135 ms
GROUP desync:        330 049 ms
Resultado:           GROUP SYNCED, generation 1165 preservada, reconnects=2
Watchdog / panic / reboot: nenhum
```

### Causa — CONFIRMADA pelo operador

**O carro foi desligado e religado.** O operador confirmou o evento; ele explica
integralmente a ocorrência e encerra a investigação da causa.

A evidência de log é consistente e podia ter sido lida assim mesmo antes da
confirmação: os SP624E **não anunciaram por ~328 s** (`first_adv` só em
1610 ms). Um periférico que não anuncia está sem energia. Se a falha fosse da
stack BLE do ESP32, eles continuariam anunciando e a reconexão seria quase
imediata — que foi exatamente o que aconteceu assim que voltaram a anunciar
(1 802 ms e 1 455 ms). O kit interno caiu no mesmo instante porque também é
alimentado pelo carro.

**Não houve falha de firmware.** Descartado: travamento da stack BLE, exaustão
de heap como causa da queda, defeito no diagnóstico. O diagnóstico não estava
enviando nada quando a queda ocorreu.

Fatos preservados do incidente:

- a recuperação automática funcionou exatamente como projetada;
- `generation=1165` foi preservada e reverificada nos dois lados;
- nenhum watchdog, panic ou reboot;
- o ESP32 permaneceu vivo durante todo o desligamento do carro, o que indica
  que ele está em alimentação independente ou tamponada.

O mínimo de **2 740 bytes** continua sendo evidência válida, mas de outra coisa:
é o custo de pico da recuperação simultânea de dois SP624E **enquanto o
diagnóstico retinha 7 360 bytes**. Não foi causa da queda; foi consequência
dela. Ainda assim define o teto de memória para a integração permanente.

### Memória por teste de escrita

| Teste | Free heap antes | Free heap depois | Minimum free heap | Conexões |
|---|---:|---:|---:|---:|
| vermelho (janela longa) | 15 744 | 15 744 | 4 856 | 3 |
| verde (janela curta) | 18 040 | 18 040 | 2 740 (inalterado) | 3 |
| azul (janela curta) | 15 744 | 15 744 | 2 740 (inalterado) | 3 |
| branco (janela curta) | 15 672 | 18 040 * | 2 740 (inalterado) | 3 |

\* O ganho de 2 368 bytes na linha do branco veio da saída de um cliente Wi-Fi
do AP (`reason = 8` em `2346168`), não do write.

O mínimo de 2 740 na linha do verde é herdado do power-cycle do carro, não
provocado pelo teste: o valor não se moveu durante toda a janela. O write em si
custa essencialmente nada — 9 bytes num mbuf do pool do NimBLE. O custo real do
diagnóstico está nas tabelas (7 360 bytes) e na conexão, não no comando.

### Margem de heap observada ao longo da investigação

| Momento | Free heap | Minimum free heap |
|---|---:|---:|
| Diagnóstico desligado, 2 conexões | 23 048 | — |
| Diagnóstico ligado, 3 conexões, subscrito | 15 744 | 4 856 |
| Durante a recuperação das 3 conexões | 15 672 | **2 740** |
| Após `diag disable` | 22 708 | 2 740 (histórico) |

**2 740 bytes é margem inaceitável para operação permanente.** O diagnóstico
retém 7 360 bytes enquanto habilitado; a recuperação simultânea de dois SP624E
consome heap em rajada. Qualquer integração permanente do interior precisa
custar uma fração disso — ver a análise de arquitetura ao final deste
documento.

## Protocol investigation

Regra desta tabela: uma linha só sai de `não` para `sim` na coluna
**Confirmado** após observação visual direta do operador no carro. Sucesso de
GATT write nunca é confirmação — e neste dispositivo o write nem sequer tem ACK,
porque é `WRITE_NO_RESPONSE`.

| Ação | Service | Characteristic | Write type | Payload HEX | Resposta | Confirmado |
|---|---|---|---|---|---|---|
| vermelho | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | `7E FF 05 03 FF 00 00 FF EF` | nenhuma notification | **sim — 2026-09-05** |
| RGB arbitrário | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | `7E FF 05 03 78 1E DC FF EF` (120,30,220) | nenhuma notification | **sim — 2026-09-05, via integração** |
| visual off | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | `7E FF 05 03 00 00 00 FF EF` | nenhuma notification | **sim — 2026-09-05, via integração** |
| verde | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | `7E FF 05 03 00 FF 00 FF EF` | nenhuma notification | **sim — 2026-09-05** |
| azul | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | `7E FF 05 03 00 00 FF FF EF` | nenhuma notification | **sim — 2026-09-05** |
| branco | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | `7E FF 05 03 FF FF FF FF EF` | nenhuma notification | **sim — 2026-09-05** |
| brilho | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | ? | ? | não |
| ligar | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | ? | ? | não |
| desligar | `FFE0` | `FFE1` | WRITE_NO_RESPONSE | ? | ? | não |

### Teste de escrita 1 — vermelho — CONFIRMADO em 2026-09-05

Payload fornecido pelo operador, enviado uma única vez, sem variações e sem
fuzzing.

```text
Data:            2026-09-05
Payload:         7E FF 05 03 FF 00 00 FF EF   (9 bytes)
Service:         FFE0
Characteristic:  FFE1  (value handle 0x0003)
Write type:      WRITE_NO_RESPONSE
Hipótese:        RGB(255, 0, 0)
```

Log real da operação:

```text
[1137.674][WRITE] WRITE_NO_RESPONSE handle=0x0003 length=9 bytes=7E FF 05 03 FF 00 00 FF EF
NimBLE: GATT procedure initiated: write no rsp; att_handle=3 len=9
[1137.692][WRITE] Queued without response (no acknowledgement)
```

| Item | Resultado |
|---|---|
| Erro de GATT | nenhum |
| Notification recebida | **nenhuma**, em 12 s subscrito ao CCCD `0x0004` |
| **Resultado visual** | **iluminação interna ficou VERMELHA — confirmado pelo operador** |
| LEFT durante o teste | `READY` connected handle=1 RSSI=-67 |
| RIGHT durante o teste | `READY` connected handle=0 RSSI=-73 |
| Grupo | `SYNCED` `generation=1165`, inalterada |
| Conexões | 3 de 3 |
| Free heap | 15 744 bytes |
| Minimum free heap | 4 856 bytes |

O controlador **não emite notification** ao aceitar um comando. Portanto não há
canal de verificação por software: confirmação de efeito neste dispositivo é
sempre visual. Isso é uma diferença importante em relação ao SP624E, que
responde a State Query e permite reconciliação verificada.

### Teste de escrita 2 — verde — CONFIRMADO em 2026-09-05

Payload fornecido pelo operador. Única variável em relação ao teste 1: os três
bytes centrais. Todo o resto do quadro permaneceu byte a byte idêntico.

```text
Data:            2026-09-05
Payload:         7E FF 05 03 00 FF 00 FF EF   (9 bytes)
Write type:      WRITE_NO_RESPONSE
Hipótese:        RGB(0, 255, 0)
```

Log real da operação:

```text
[1890.421][WRITE] WRITE_NO_RESPONSE handle=0x0003 length=9 bytes=7E FF 05 03 00 FF 00 FF EF
NimBLE: GATT procedure initiated: write no rsp; att_handle=3 len=9
[1890.440][WRITE] Queued without response (no acknowledgement)
```

| Item | Resultado |
|---|---|
| Erro de GATT | nenhum |
| Notification recebida | **nenhuma**, em 10 s subscrito ao CCCD `0x0004` |
| **Resultado visual** | **iluminação interna ficou VERDE — confirmado pelo operador** |
| LEFT durante o teste | `READY` connected handle=0 RSSI=-66 uptime 289 s |
| RIGHT durante o teste | `READY` connected handle=1 RSSI=-84 uptime 288 s |
| Grupo | `SYNCED` `generation=1165`, inalterada |
| Conexões | 3 de 3 |
| Free heap antes / depois | 18 040 / 18 040 |
| Minimum free heap | 2 740 — **inalterado pelo teste** |

Executado com o padrão de **janela curta** adotado após o incidente: o
diagnóstico ficou habilitado por ~48 s (`enable → target → connect → gatt →
subscribe → write → status → disable`) em vez de permanecer ligado por minutos.
O mínimo de heap não se moveu um byte, validando o padrão. Após o `disable`, o
heap voltou a 25 416 e as conexões a 2 de 3.

### Teste de escrita 3 — azul — CONFIRMADO em 2026-09-05

Payload fornecido pelo operador. Única variável: os três bytes centrais.

```text
Data:            2026-09-05
Payload:         7E FF 05 03 00 00 FF FF EF   (9 bytes)
Write type:      WRITE_NO_RESPONSE
Hipótese:        RGB(0, 0, 255)
```

Log real da operação:

```text
[2112.913][WRITE] WRITE_NO_RESPONSE handle=0x0003 length=9 bytes=7E FF 05 03 00 00 FF FF EF
NimBLE: GATT procedure initiated: write no rsp; att_handle=3 len=9
[2112.931][WRITE] Queued without response (no acknowledgement)
```

| Item | Resultado |
|---|---|
| Erro de GATT | nenhum |
| Notification recebida | **nenhuma**, em 10 s subscrito ao CCCD `0x0004` |
| **Resultado visual** | **iluminação interna ficou AZUL — confirmado pelo operador** |
| LEFT durante o teste | `READY` connected handle=0 RSSI=-66 uptime 509 s |
| RIGHT durante o teste | `READY` connected handle=1 RSSI=-84 uptime 508 s |
| Grupo | `SYNCED` `generation=1165`, inalterada |
| Conexões | 3 de 3 |
| Free heap antes / depois | 15 744 / 15 744 |
| Minimum free heap | 2 740 — **inalterado pelo teste** |

Janela curta de ~49 s. Segundo teste consecutivo sem mover o mínimo de heap.

### Teste de escrita 4 — branco — CONFIRMADO em 2026-09-05

Primeiro teste a acionar **mais de um canal simultaneamente**.

```text
Data:            2026-09-05
Payload:         7E FF 05 03 FF FF FF FF EF   (9 bytes)
Write type:      WRITE_NO_RESPONSE
Hipótese:        RGB(255, 255, 255)
```

Log real da operação:

```text
[2347.795][WRITE] WRITE_NO_RESPONSE handle=0x0003 length=9 bytes=7E FF 05 03 FF FF FF FF EF
NimBLE: GATT procedure initiated: write no rsp; att_handle=3 len=9
[2347.813][WRITE] Queued without response (no acknowledgement)
```

| Item | Resultado |
|---|---|
| Erro de GATT | nenhum |
| Notification recebida | **nenhuma**, em 10 s subscrito ao CCCD `0x0004` |
| **Resultado visual** | **iluminação interna ficou BRANCA — confirmado pelo operador** |
| LEFT durante o teste | `READY` connected handle=0 RSSI=-66 uptime 739 s |
| RIGHT durante o teste | `READY` connected handle=1 RSSI=-84 uptime 738 s |
| Grupo | `SYNCED` `generation=1165`, inalterada |
| Conexões | 3 de 3 |
| Free heap antes / depois | 15 672 / 18 040 |
| Minimum free heap | 2 740 — **inalterado pelo teste** |

Este resultado estabelece que **os três canais são independentes e
combináveis**: acionados juntos produzem a soma, não um comportamento
exclusivo. É a propriedade de que qualquer controle de cor arbitrária depende.

### Estrutura observada do payload

Derivada de **quatro amostras confirmadas visualmente** — vermelho, verde, azul
e branco — que diferem apenas nos bytes centrais.

```text
offset:   0    1    2    3    4    5    6    7    8
         7E   FF   05   03   RR   GG   BB   FF   EF

vermelho  7E   FF   05   03   FF   00   00   FF   EF   → vermelho (confirmado)
verde     7E   FF   05   03   00   FF   00   FF   EF   → verde    (confirmado)
azul      7E   FF   05   03   00   00   FF   FF   EF   → azul     (confirmado)
branco    7E   FF   05   03   FF   FF   FF   FF   EF   → branco   (confirmado)
                             └────┬────┘
                   único trecho que variou entre os quatro testes
```

**CONFIRMADO — esses bytes controlam RGB:**

- `offset 4` = canal **vermelho** (`FF` acende, `00` não);
- `offset 5` = canal **verde** (`FF` acende, `00` não);
- `offset 6` = canal **azul** (`FF` acende, `00` não);
- os três canais são **independentes e combináveis**: acionados juntos produzem
  a soma (branco), não um comportamento exclusivo;
- os offsets 0–3, 7 e 8 são invariantes de quadro para comando de cor: mantidos
  byte a byte idênticos nos quatro testes, todos aceitos.

Método: cada canal foi isolado por construção — em três dos testes exatamente um
dos bytes valia `FF` e os outros dois `00`, e a cor observada correspondeu ao
byte ativo; o quarto teste acionou os três juntos e produziu branco. Quatro
amostras, quatro resultados distintos, nenhuma ambiguidade de ordem nem de
combinação.

**AINDA NÃO CONFIRMADO:**

- significado individual de `7E`, `FF`, `05`, `03`, `FF`, `EF`;
- comportamento com valores intermediários — só `00` e `FF` foram testados, e
  nada prova que a escala é linear, gama-corrigida ou sequer de 8 bits por
  canal. Uma cor arbitrária do PWA depende dessa premissa não verificada;
- brilho, efeitos, liga/desliga, e qualquer outro comando.

Nada aqui autoriza gerar payloads novos automaticamente. O próximo payload deve
ser fornecido explicitamente pelo operador, exatamente como estes foram.

## Notifications observadas

| Momento | Characteristic | Length | HEX | ASCII | Contexto |
|---|---|---|---|---|---|
| 30 s subscrito, ocioso | `0xFFE1` | — | nenhuma | — | controlador não emite estado espontâneo |
| 12 s subscrito, após o write de vermelho | `0xFFE1` | — | nenhuma | — | não responde nem a comando aceito |
| 10 s subscrito, após o write de verde | `0xFFE1` | — | nenhuma | — | confirma o padrão write-only |
| 10 s subscrito, após o write de azul | `0xFFE1` | — | nenhuma | — | quarta amostra do mesmo padrão |
| 10 s subscrito, após o write de branco | `0xFFE1` | — | nenhuma | — | quinta amostra; padrão write-only definitivo |

Conclusão: o controlador é **write-only na prática**. `READ` é declarado e
rejeitado, e `NOTIFY` está disponível mas nunca dispara. Qualquer integração
futura será cega — sem Observed State, sem verificação, sem reconciliação. Isso
reforça o requisito de que `INTERIOR` seja **best effort** e jamais participe do
Strict Sync de LEFT/RIGHT.

## Obstáculo para descobrir o protocolo

O controlador aceita **uma única conexão central**. Enquanto o ESP32 está
conectado, o aplicativo original não consegue conectar — e vice-versa. Portanto
**o ESP32 não consegue observar o tráfego do aplicativo**: ele não é um sniffer,
apenas um participante da conexão.

Caminhos possíveis, em ordem de preferência:

1. **HCI snoop log do Android.** Ativar "Registro de captura Bluetooth HCI" nas
   opções do desenvolvedor, operar o kit pelo app (vermelho, verde, azul,
   branco, brilho, liga, desliga), extrair o `btsnoop_hci.log` e ler os writes
   em `0xFFE1`. É o método que produz o protocolo real, sem adivinhação.
2. **Sniffer BLE dedicado**, por exemplo nRF52840 dongle com Wireshark.
3. **Bytes fornecidos manualmente** pelo operador, testados um a um com
   `diag write_nr FFE0 FFE1 <hex>` e confirmados visualmente.

Atualização de 2026-09-05: o caminho 3 já produziu o primeiro resultado — o
payload de vermelho foi fornecido pelo operador e confirmado visualmente. Os
caminhos 1 e 2 continuam sendo a forma mais rápida de obter o restante do
protocolo de uma só vez, em vez de um payload por rodada.

Enquanto os demais comandos não forem confirmados visualmente, o protocolo
continua incompleto e nenhum `InteriorLightController` deve ser escrito.

## Procedimento de coleta

Testes A, B e C já executados em 2026-09-05; os resultados estão acima. O
roteiro abaixo permanece como referência para repetição.

### Teste A — kit interno desligado

```text
diag enable
diag scan clear
diag scan start 15
diag scan list
```

Salvar a saída completa.

### Teste B — kit interno ligado

Ligar a iluminação interna e o controlador, então repetir:

```text
diag scan clear
diag scan start 15
diag scan list
```

Comparar com o Teste A e preencher a tabela de comparação acima.

### Teste C — candidato identificado

```text
diag scan list 20:23:12:15:00:19
diag target 20:23:12:15:00:19
diag connect
diag status
diag gatt
```

### Teste D — observação passiva

```text
diag subscribe FFE0 FFE1
```

Limitado pelo obstáculo descrito acima: com o ESP32 conectado, o aplicativo
original não consegue operar o kit, então não há tráfego para observar. Serve
apenas para capturar respostas a writes que nós mesmos enviarmos.

### Teste E — reprodução manual

Somente depois de haver bytes observados vindos do aplicativo original:

```text
diag write_nr FFE0 FFE1 <hex>
```

`diag write` falhará: a characteristic não suporta WRITE com resposta.

Registrar o efeito visual observado. Payload sem origem observada não entra na
tabela de protocolo.

## Análise para a integração permanente

Escrita depois da confirmação das quatro cores. **Nada abaixo foi implementado.**

### Diagnóstico versus produção — de onde vem o consumo

Medido, não estimado. `diag enable` reporta a alocação exata:

```text
Tables allocated devices=4096 services=384 characteristics=1536
                 descriptors=1344 total=7360 bytes
```

| Componente do diagnóstico | Bytes | Necessário em produção? |
|---|---:|---|
| Tabela de scan (32 dispositivos) | 4 096 | **Não.** O endereço é conhecido e fixo. |
| Tabela de serviços (16) | 384 | Não. Só interessa FFE0. |
| Tabela de characteristics (48) | 1 536 | Não. Só interessa FFE1. |
| Tabela de descriptors (48) | 1 344 | Não. CCCD é inútil: o device não notifica. |
| Subscriptions (8 slots) | ~200 estáticos | **Não.** Cinco amostras sem nenhuma notification. |
| GATT discovery completo | transitório, ~1 s | Reduzido: basta localizar FFE1. |
| Task dedicada | — | **Não.** Ver abaixo. |
| **Total do diagnóstico** | **7 360** | **→ produção: dezenas de bytes** |

O runtime de produção precisa reter apenas: RGB desejado (3 bytes), estado de
conexão, `conn_handle`, value handle de FFE1, timestamp da última tentativa e um
buffer de 9 bytes para o quadro. **Ordem de 40 a 60 bytes.**

**Não criar uma task dedicada.** No ESP-IDF uma task custa seu stack inteiro —
as tasks existentes deste projeto usam de 3 072 a 8 192 bytes. Uma task para o
interior custaria mais do que todo o resto da integração somada e mais do que a
margem de heap disponível. A máquina de estados do interior deve ser conduzida
de forma não bloqueante a partir de uma task já existente, no mesmo padrão em
que `group_runtime` já faz polling de fila, health e persistência.

### Custo real da terceira conexão — medido

Comparando o heap logo após `diag enable` com o heap após conectar, fazer GATT
discovery e assinar o CCCD, nos três testes de janela curta:

| Teste | Heap após `enable` | Heap conectado + GATT + subscrito | Delta |
|---|---:|---:|---:|
| verde | 18 040 | 18 040 | **0** |
| azul | 15 744 | 15 744 | **0** |
| branco | 15 672 | 15 672 | **0** |

**A terceira conexão BLE não consome heap dinâmico.** O NimBLE já reserva
estaticamente as estruturas de três conexões por causa de
`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3`, que o projeto já configurava antes desta
investigação. O custo de 7 360 bytes é inteiramente das tabelas de
diagnóstico — que a produção não terá.

Isso remove o heap como argumento contra manter a conexão aberta.

### Latência de conexão — medida

Quatro conexões ao LEDCAR, do comando ao evento `Connected`:

| Sessão | Início | Connected | Latência |
|---|---:|---:|---:|
| GATT inicial | 1075.302 | 1075.969 | 667 ms |
| verde | 1865.305 | 1865.932 | 627 ms |
| azul | 2087.757 | 2088.437 | 680 ms |
| branco | 2316.232 | 2316.841 | 609 ms |

Média ~646 ms, desvio pequeno. Somando MTU exchange (~130-180 ms) e um GATT
discovery reduzido, um ciclo completo conectar-e-escrever fica em torno de
**800 ms a 1 s**. Uma escrita em conexão já aberta é imediata, limitada apenas
pelo connection interval de 50 ms.

### Conexão permanente x sob demanda

| Critério | A — permanente | B — sob demanda |
|---|---|---|
| Heap | 0 (medido) | 0 (medido) |
| Latência da 1ª mudança | ~50 ms | ~800 ms a 1 s |
| Latência das seguintes | ~50 ms | ~800 ms a 1 s cada |
| Papel master BLE | só no reconnect | **em toda mudança de cor** |
| Máquina de reconexão | necessária, com backoff | desnecessária |
| App do celular | **bloqueado permanentemente** | livre entre comandos |
| Após power-cycle do carro | 3 recuperações competindo pelo rádio | 2, como hoje |

O critério decisivo não é memória — é o **papel master do NimBLE**, o mesmo
recurso único que já obrigou o `ble_diagnostics_release_master()`. Conectar
exige o papel master; escrever numa conexão aberta não exige nada.

A opção B paga esse custo a cada mudança de cor, e um color picker arrastado no
PWA geraria uma sequência de conexões de 800 ms — inviável. A opção A paga
apenas no reconnect, mas mantém uma terceira conexão disputando slots de rádio
com LEFT e RIGHT indefinidamente, e adiciona uma terceira recuperação
competindo pelo papel master exatamente quando os faróis mais precisam dele.

**Recomendação: híbrido — conectar sob demanda e manter aberto com timeout de
ociosidade.**

```text
ocioso, desconectado                    custo zero
       ↓ primeira mudança de cor
adquire master (se nenhum SP624E precisar dele)
       ↓ ~800 ms
conectado → write                       mudanças seguintes: ~50 ms
       ↓ N segundos sem mudança
desconecta                              libera o app do celular e o rádio
```

Isso dá latência baixa durante a interação, custo zero em repouso, devolve o
controlador ao app do celular quando o carro não está mudando de cor, e reduz a
janela em que uma terceira recuperação poderia competir com os faróis. O valor
de `N` deve ser calibrado fisicamente; 60 s é um ponto de partida razoável, não
um número medido.

### Sem Observed State — consequência arquitetural

Cinco amostras confirmam: o controlador **não emite notification nem após
comando aceito**, e rejeita `READ`. Não existe canal de verificação.

Portanto a integração **não pode** reutilizar o modelo de reconciliação dos
SP624E. O estado do interior é:

```text
desiredInteriorColor   = RGB          ← intenção, o que sabemos
lastWriteAttemptMs     = timestamp    ← quando tentamos
interiorConnectionState= ...          ← estado do transporte
```

e **nunca**:

```text
observedInteriorColor  = RGB          ← não existe evidência que o sustente
```

`WRITE_NO_RESPONSE` não é ACK. Não é prova de entrega, muito menos de efeito
visual. Tratar sucesso de write como confirmação seria repetir exatamente o erro
que o princípio 4 do projeto proíbe para os SP624E.

Consequência prática: o interior **não pode participar do Strict Sync**, não
pode influenciar `GROUP = SYNCED`, não pode reter geração e não pode bloquear a
resposta ao PWA. Ele é best effort por limitação do hardware, não por escolha.

### Superfície mínima proposta

Adaptada ao padrão do projeto (módulos em C, sem C++, prefixo por subsistema):

```c
esp_err_t interior_light_init(void);
bool      interior_light_is_available(void);
void      interior_light_set_color(uint8_t r, uint8_t g, uint8_t b);
void      interior_light_service(void);   /* chamado da task existente */
```

`interior_light_set_color()` apenas registra a intenção e retorna; nunca
bloqueia o chamador nem faz GATT. `interior_light_service()` conduz a máquina de
estados de forma não bloqueante.

Quadro a montar, **somente** com o que foi confirmado:

```text
7E FF 05 03 RR GG BB FF EF
```

com `RR`, `GG`, `BB` nas posições 4, 5 e 6. Valores intermediários seguem sem
validação: a primeira integração deveria ser testada com cores puras antes de
confiar em cor arbitrária.

### Riscos que permanecem

| Risco | Evidência | Mitigação proposta |
|---|---|---|
| Margem de heap de 2 740 bytes | medido durante recuperação simultânea | produção custa ~50 bytes em vez de 7 360; não criar task |
| Terceira recuperação competindo pelo master | observado no power-cycle | interior nunca preempta; cede sempre, como o diagnóstico já faz |
| Airtime de três conexões | não medido em longo prazo | timeout de ociosidade reduz a exposição |
| Wi-Fi + BLE coexistindo | cliente Wi-Fi custou ~2 368 bytes | já é realidade hoje; monitorar |
| Valores RGB intermediários | **não testados** | validar fisicamente antes de expor cor livre no PWA |
| Sem verificação de efeito | cinco amostras sem notification | nunca reportar o interior como confirmado na UI |

## Production integration

Implementada em 2026-09-05, depois da confirmação visual das quatro cores.
Módulo `main/interior/`, independente do modo diagnóstico.

### Arquitetura

```text
PWA / RF / presets / Group API
            ↓
      Desired State                    (fonte única de intenção)
            ↓
   ┌────────┴─────────┐
   ↓                  ↓
SP624E LEFT/RIGHT   interior_light_follow_desired()
(Strict Sync,        (best effort, sem Strict Sync,
 generation,          sem generation, sem verificação)
 reconciliação)
```

O ponto de integração é a task `group_runtime`, dentro de
`sp624e_controller.c`, logo após `update_group_completion()`. Ela copia o
Desired State sob o lock existente e chama duas funções não bloqueantes:

```c
interior_light_follow_desired(&interior_desired);
interior_light_service();
```

**Nenhuma task nova.** A análise de memória mostrou que uma task custaria mais
do que toda a integração; a máquina de estados roda dentro do laço de 20 ms que
já existe.

O frontend continua enviando apenas a intenção dos faróis. Não há chamada
separada para o interior, então qualquer origem de comando que passe pelo
Desired State — PWA, RF, presets — reflete automaticamente no interior.

### Regra do branco padrão

A condição usa a **semântica explícita do projeto**, não comparação de bytes:

```c
if (!desired->valid || !desired->power ||
    desired->light_mode == SP624E_LIGHT_MODE_WHITE) {
    interior = 0,0,0;
} else {
    interior = desired->red, desired->green, desired->blue;
}
```

`sp624e_light_mode_t` já distingue `SP624E_LIGHT_MODE_WHITE` de
`SP624E_LIGHT_MODE_RGB`, e o `indicator_policy` do projeto já apaga o LED físico
exatamente nessa condição. Comparar `RGB == 255,255,255` seria uma inferência
mais fraca e foi deliberadamente evitada: um RGB `255,255,255` pedido
explicitamente pelo usuário **não** é branco padrão, e o teste de host cobre
essa distinção.

`brightness` não é propagado. O comando de brilho do LEDCAR é desconhecido, e
escalar o RGB localmente inventaria um comportamento sem evidência. Consequência
conhecida: faróis vermelhos a 10% de brilho produzem interior vermelho pleno.

### Animações

Frames de animação não passam pelo Desired State (princípio 18 do projeto), e o
interior lê exatamente o Desired State. Portanto **animações não são
propagadas**: durante um Police, o interior mantém a cor desejada anterior.
Isso é intencional — o protocolo de efeitos do LEDCAR é desconhecido e nenhum
mapeamento foi criado.

### Máquina de estados

```text
IDLE ──(cor pendente)──> PENDING ──(LEFT e RIGHT READY)──> CONNECTING
                                                              │
                          BACKOFF <──(falha/timeout/yield)────┤
                             │                                ↓
                             └──(expirou)──> IDLE        DISCOVERING
                                                              ↓
                                                          CONNECTED
                                                          │       │
                                              (nova cor) ─┘       │
                                              (60 s ocioso) ──────┘
                                                              ↓
                                                            IDLE
```

- `interior_light_set_color()` e `interior_light_follow_desired()` apenas
  gravam a intenção e retornam. Sem scan, sem connect, sem GATT, sem espera.
- **Latest state wins**: só existe um RGB desejado. Um arraste do color picker
  colapsa em um único valor; não há fila.
- Coalescing adicional por `APP_INTERIOR_LIGHT_MIN_WRITE_INTERVAL_MS` (60 ms),
  próximo do connection interval observado de 50 ms.
- Backoff 2 s → 30 s, dobrando, zerado ao ficar `CONNECTED`.

### BLE de produção

| Item | Diagnóstico | Produção |
|---|---|---|
| Descoberta | walk completo, 16/48/48 tabelas | `ble_gattc_disc_svc_by_uuid(FFE0)` + `ble_gattc_disc_chrs_by_uuid(FFE1)` |
| Scan | tabela de 32 dispositivos | **nenhum** — conexão direta ao endereço público |
| Subscribe | CCCD `0x2902` | **nenhum** — o controlador nunca notifica |
| READ | tentado | **nunca** — é rejeitado com ATT `0x0E` |
| Estado retido | 7 360 bytes | dois handles + 3 bytes de RGB |

Endereço, UUIDs e temporizações ficam centralizados em `app_config.h`
(`APP_INTERIOR_LIGHT_*`). Nenhum MAC espalhado pelo código, nenhum `60000`
solto.

### Prioridade dos SP624E

Dois mecanismos, ambos reaproveitando infraestrutura existente:

1. **Portão de entrada.** Uma tentativa de conexão só começa quando
   `ble_connection_manager_both_ready()` é verdadeiro. Com ambos `READY` o
   Connection Manager não está escaneando nem conectando, logo o papel master
   está livre.
2. **Preempção.** `interior_light_release_master()` é registrado no mesmo guard
   do Connection Manager já usado pelo diagnóstico — agora com duas vagas
   (`BLE_MASTER_GUARD_MAX`). Quando a recuperação precisa do rádio, o guard
   cancela a tentativa do interior de forma síncrona, na mesma task e no mesmo
   ciclo, antes de `start_connect()`/`begin_recovery_scan()`.

O guard não altera a máquina de estados: apenas libera o rádio e levanta uma
flag, para que `s_state` continue tendo um único escritor. Uma conexão de
interior já estabelecida **não** é derrubada, porque um link aberto não bloqueia
scan nem connect.

Após um power-cycle do carro, o interior invalida a última tentativa e reaplica
a intenção assim que os dois faróis voltarem a `READY`.

### Sem Observed State

O módulo mantém `desired`, `last_attempted`, `last_write_ms` e o estado de
conexão. **Não existe** `observed_color`, `confirmed_color` nem `synced_color`.
`WRITE_NO_RESPONSE` não é ACK e nunca é tratado como prova de efeito físico. O
status de console reporta `attempted`, jamais `confirmed` ou `SYNCED`.

### Visual off ≠ power off

"Apagado" é `RGB(0,0,0)` pelo comando de cor confirmado:

```text
7E FF 05 03 00 00 00 FF EF
```

Isso é **visual off**. O comando real de power-off do LEDCAR continua
desconhecido e não foi inventado.

### Memória medida

`idf.py size`, comparando o firmware com diagnóstico e o mesmo firmware com a
integração de produção adicionada:

| Métrica | Antes (só diagnóstico) | Depois (+ interior) | Δ |
|---|---:|---:|---:|
| DRAM estática | 77 917 | 78 037 | **+120** |
| Flash Code | 802 452 | 805 840 | +3 388 |
| IRAM | 115 051 | 115 051 | 0 |
| DRAM estática livre | 46 663 | 46 543 | −120 |

**+120 bytes**, contra os 7 360 bytes que o diagnóstico aloca. Nenhuma task
nova, nenhuma alocação dinâmica no caminho de produção. A terceira conexão BLE
já foi medida como custo zero de heap (o NimBLE reserva as três estruturas
estaticamente por `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3`).

### Heap de runtime — medido no carro, 2026-09-05

`status` do console, ao longo de quatro sessões cobrindo boot, primeira conexão,
mudanças de cor, rajada de color picker, retorno ao branco e idle disconnect:

| Cenário | Free heap | Minimum free heap | Conexões |
|---|---:|---:|---:|
| Boot, faróis READY, interior IDLE | 25 556 | 22 856 | 2 de 3 |
| Interior CONNECTED, após write | 25 556 | 22 856 | 3 de 3 |
| Mudança de cor com link aberto | 25 556 | 22 856 | 3 de 3 |
| Rajada de 6 cores em 2,1 s | 25 556 | 22 856 | 3 de 3 |
| Volta ao branco padrão | 25 556 | 22 856 | 3 de 3 |
| Após idle disconnect | 25 556 | 22 856 | 2 de 3 |

**O valor não se moveu um byte em nenhuma das ~80 amostras.** Sem vazamento,
sem pico, sem pressão. Compare com o diagnóstico habilitado, que levava o mesmo
sistema a `free=15 744 / minimum=2 740`.

### Latências — medidas no carro

| Situação | Latência do comando até o write do interior |
|---|---:|
| Primeira mudança, link fechado | 3 680 ms (connect 1 670 + discovery 1 060 + write) |
| Mudanças seguintes, link aberto | 60 ms |
| Idle disconnect após o último write | 60,0 s |

A estratégia híbrida entregou o que a análise previa: o custo de conexão é pago
uma vez e as mudanças seguintes chegam praticamente junto com os faróis — em
vários casos o interior escreveu **antes** de os SP624E verificarem a geração.

### Validação da regra de branco — evidência de campo

Ao voltar para o branco padrão pelo PWA, o estado observado dos SP624E ficou:

```text
SP624E_NOTIFY: LEFT STATE power=1 effect=0xCC mode=0 rgb=1,97,253 ...
INTERIOR: write attempted RGB=0,0,0
INDICATOR: ON reason=WAITING_WHITE
```

Os faróis estavam em **branco** com `rgb=1,97,253` ainda residente nos
registradores do SP624E. A condição `RGB == 255,255,255` teria mantido o
interior aceso em azul enquanto os faróis estavam brancos. `light_mode ==
SP624E_LIGHT_MODE_WHITE` acertou. Este é o cenário concreto que justifica a
escolha semântica.

## Integração futura

Depois de o protocolo estar confirmado, o alvo é:

```text
PWA seleciona RGB
       ↓
Desired State / Group Controller
       ↓
 ┌───────────────┬────────────────┐
 ↓               ↓                ↓
LEFT           RIGHT           INTERIOR
```

com `INTERIOR` em **best effort**: indisponibilidade da iluminação interna não
pode reter, degradar ou bloquear a geração aplicada em LEFT e RIGHT, nem
participar do Strict Sync.

Nenhum `InteriorLightController` deve ser criado enquanto a tabela de protocolo
estiver vazia.
