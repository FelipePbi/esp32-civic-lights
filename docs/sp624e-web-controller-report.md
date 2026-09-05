# SP624E WEB CONTROLLER REPORT

Status: **PASS NO HARDWARE** em 2026-08-09.

## Firmware

| Item | Resultado |
|---|---|
| Version | `0.5.0` |
| Build | ESP-IDF 6.0.2; `scripts/build.ps1 -FullClean` PASS |
| Flash | ESP32-D0WD-V3 rev. 3.1, 4 MB; hash verificado |
| Web partition | `web`, offset `0x210000`, tamanho `0x1f0000` |
| Firmware | `0x1012d0` bytes |
| Free firmware space | `0xfed30` bytes, 50% |
| Web bundle | JS 205.03 kB / 64.91 kB gzip; CSS 10.50 kB / 2.94 kB gzip |
| Web filesystem | SPIFFS 246984/1860161 bytes usados |

## Wi-Fi

| Item | Resultado |
|---|---|
| Mode | SoftAP |
| SSID | `Civic-Lights` |
| IP | `192.168.4.1`, DHCP ativo |
| Security | WPA2-PSK; senha centralizada em `app_config.h` |
| iPhone connected | PASS, IP `192.168.4.2` observado |
| Internet required | NO |

A busca BLE de recuperação usa janela de 30 ms a cada 100 ms e scans curtos de
3 s. Isso eliminou o atraso de DHCP observado quando os SP624E estavam
desligados.

## Web

| Item | Resultado |
|---|---|
| React / TypeScript / Vite | PASS |
| Frontend tests | 10/10 PASS |
| HTTP server | `esp_http_server`, PASS |
| Initial load | PASS no Safari do iPhone |
| Touch color picker / brightness | PASS |
| PWA na Tela de Início | PASS; abriu standalone e controlou os faróis |
| Assets externos | Nenhum |

## REST API

| Endpoint | Resultado |
|---|---|
| `GET /api/v1/status` | PASS |
| `GET /api/v1/state` | PASS |
| `PUT /api/v1/state` | PASS, `202 Accepted` + generation |
| `GET/PUT /api/v1/presets` | PASS |
| `POST /api/v1/resync` | PASS |

Parsing, ranges, modo inválido, JSON malformado e body excessivo passaram nos
testes C de host. Handlers usam a API thread-safe do Group Controller; nenhum
handler HTTP escreve GATT diretamente.

## WebSocket

| Item | Resultado |
|---|---|
| Endpoint `/ws` e snapshot inicial | PASS |
| Reconnect browser com backoff | PASS |
| LEFT updates | PASS sem refresh |
| RIGHT updates | PASS sem refresh |
| Group updates | PASS |
| Dois clientes | PWA + Safari abertos; segundo fd conectado sem erro/panic |

O fallback HTTP só consulta enquanto o WebSocket está desconectado. O socket é
o canal principal e evita tráfego que concorra com os enlaces BLE fracos.

## RGB

| Item | Resultado |
|---|---|
| Color picker | PASS por touch no iPhone |
| Brightness | PASS |
| Throttle | 100 ms durante gesto; valor final imediato |
| Teste rápido | 139 generations (`240` → `379`) |
| Filas finais | 0 / 0 |
| Estado final do teste | LEFT = RIGHT, `SYNCED` |

## White

| Item | Resultado |
|---|---|
| `15 01 CC` | PASS |
| `21 02 LEVEL FF` | PASS |
| Individual LEFT | PASS + restore PASS |
| Individual RIGHT | PASS + restore PASS |
| Group test | PASS, ambos verificados por 30 s |
| WHITE → RGB | PASS |
| RGB → WHITE | PASS pela UI |
| Capability | Habilitada somente após confirmação visual; persistiu no reboot |

## Favorite

| Item | Resultado |
|---|---|
| Default vermelho | PASS |
| Atualização para roxo | PASS |
| NVS | PASS |
| Persistência após reboot | PASS |
| Aplicação nos dois | PASS |

## Strict Sync UI Test

O ensaio final usou RIGHT offline e Desired State ciano, partindo dos dois em
laranja:

| Item | Resultado |
|---|---|
| Desired generation | `426` → `427` com RIGHT offline |
| LEFT saudável | Permaneceu laranja por aproximadamente 39 s |
| Escrita unilateral | Nenhuma; logs `reconcile deferred ... peer offline` |
| RIGHT recovery | Query do estado antigo antes da reconciliação |
| Group dispatch | RGB enfileirado para LEFT e RIGHT somente após ambos READY |
| Final | LEFT = RIGHT = `0,255,255`; generation 427; `SYNCED` |

Durante a validação foi encontrado e corrigido um caminho de health query que
reconciliava diretamente um lado. Também foi preservada a generation da
primeira queda para que tentativas GATT intermediárias não liberem despacho
antecipado.

## BLE + Wi-Fi Stability

| Item | Resultado |
|---|---|
| Duration | 901000 ms (15 min) |
| LEFT disconnects | 0 |
| RIGHT disconnects | 0 |
| Wi-Fi disconnects | 0 observados |
| WebSocket reconnects | 0 durante a janela |
| Health checks | LEFT 90 / RIGHT 90 |
| Unexpected desyncs | 0 |

Os enlaces finais negociaram supervision timeout de 10000 ms. Solicitações
L2CAP que reduziriam esse valor são rejeitadas, necessário com RSSI entre cerca
de -87 e -94 dBm.

## Performance

| Medição | Valor observado |
|---|---|
| Boot → Wi-Fi | ~0,85 s |
| Boot → HTTP | ~1,02 s |
| Boot → GROUP SYNCED | ~20,9 s no ensaio final |
| Reconnect RIGHT + verify | 5,7 s no ensaio visual |
| Reconnect LEFT + verify | 6,3 s no ensaio visual |
| Strict dispatch após RIGHT READY | <1 s |

## Memory

| Momento | Free heap | Minimum |
|---|---:|---:|
| Antes de Wi-Fi | 199944 | 199944 |
| Após Wi-Fi | 146508 | 146356 |
| Com HTTP/WebSocket | 127896 | 127772 |
| Início estabilidade | 51404 | 33024 |
| Final estabilidade | 53716 | 33024 |

Não houve queda contínua de heap.

## Safety

| Item | Resultado final |
|---|---|
| Panic | 0 no firmware final |
| Watchdog | 0 |
| Unexpected reboot | 0 no firmware final |
| Estado persistente incorreto | 0 |
| Final LEFT | RGB `255,9,222`, generation 477 |
| Final RIGHT | RGB `255,9,222`, generation 477 |
| Final GROUP | `SYNCED` |

Um acesso nulo no tratamento experimental de L2CAP foi detectado durante o
desenvolvimento, simbolizado com `addr2line`, corrigido e retestado antes do
resultado final.

## Conclusion

**PASS.** O fluxo iPhone → SoftAP → HTTP/WebSocket → Desired State → Group
Controller → filas LEFT/RIGHT → State Query → verification → `SYNCED` foi
comprovado no hardware, incluindo White, favorita NVS, PWA, reconnect, 139
alterações RGB, Strict Sync offline e estabilidade simultânea por 15 minutos.

## Open Issues

- O farol convencional LEFT do veículo (não o RGB/SP624E) apaga após alguns
  segundos. O firmware não controla esse circuito. Revisar lâmpada/driver,
  conector, aterramento, fusível, proteção do BCM e a derivação elétrica da
  instalação antes de dirigir à noite.
- RSSI BLE é muito baixo. A tolerância de 10 s passou no ensaio, mas aproximar
  fisicamente o ESP32 ou melhorar sua posição/antena aumentará a margem.

## Recommended Next Step

Corrigir primeiro o farol convencional LEFT. Depois disso, a próxima etapa de
produto pode adicionar o receptor RF 433 MHz usando exatamente a mesma API de
Desired State e Group Controller; não escrever BLE diretamente.
