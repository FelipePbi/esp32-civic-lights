# Arquitetura web 0.7.6

## Fluxo

```text
iPhone / notebook
  │  WPA2 SoftAP Civic-Lights — 192.168.4.1
  ├─ GET assets locais no SPIFFS
  ├─ REST /api/v1 para comandos e snapshots
  └─ WebSocket /ws para eventos
                  │
                  ▼
       Group Controller API thread-safe
                  │
       Desired State + generation
                  │
       Strict Sync + filas por lado
             ┌────┴────┐
           LEFT       RIGHT
```

O SoftAP é iniciado antes do scan BLE; portanto a página pode abrir enquanto os
controladores ainda estão conectando. Nesse intervalo, comandos retornam 503 em
vez de acessar estruturas não inicializadas.

## Concorrência

- HTTP e futuras entradas externas postam comandos em `s_group_api_queue`.
- Uma trava serializa chamadas síncronas e uma resposta estática protegida evita
  ponteiros temporários entre tasks.
- A task do Group Controller é a única que altera Desired State pela API web.
- Workers LEFT/RIGHT continuam sendo os únicos responsáveis por GATT write.
- Snapshots são copiados sob mutex; nenhum ponteiro interno é exposto.
- Eventos BLE são detectados por uma task leve a cada 250 ms. Só diferenças
  relevantes geram frames.
- O envio WebSocket usa `httpd_queue_work` e
  `httpd_ws_send_frame_async`, com payload alocado até o término do envio.
- Todos os sockets WebSocket ativos obtidos pelo servidor recebem o evento;
  sockets fechados deixam automaticamente a lista de clientes.

## Partições de flash

| Nome | Offset | Tamanho | Uso |
|---|---:|---:|---|
| nvs | `0x9000` | `0x6000` | mapping, estado, favorita e capability WHITE |
| phy_init | `0xf000` | `0x1000` | calibração RF |
| factory | `0x10000` | `0x200000` | firmware |
| web | `0x210000` | `0x1f0000` | React/Vite em SPIFFS |

O frontend usa somente React, CSS e SVG locais. Não há CDN, fonte remota,
analytics, Service Worker ou dependência de internet.

## Frontend

`App` mantém uma única instância de `useController`; trocar rota não derruba o
WebSocket nem repete bootstrap. Router leve usa History API e o servidor devolve
`index.html` para caminhos sem extensão.

| Rota | Responsabilidade |
|---|---|
| `/` | Status LEFT/RIGHT, conjunto, quick actions e navegação |
| `/color` | RGB, hue, brilho e presets |
| `/remote` | RX480E, Button D, Police e indicador |
| `/diagnostics` | Telemetria e resync |

Fluxo visual dos faróis:

```text
SP624E → State Query/parser → observed.left/right → WebSocket snapshot
                                              ↓
                      mapObservedStateToHeadlightVisual(side, observed)
                                              ↓
                         SVG carcaça + assinatura + glow por lado
```

Disconnect não apaga a última leitura observada no firmware. Por isso o mapper
prioriza `left/right.state` e `ready`: apenas lado READY com Observed State válido
pode acender. RGB usa `brightness`; WHITE usa `white`. Desired State continua
alimentando picker e controles, nunca a confirmação visual.

O fallback HTTP não transforma rotas desconhecidas sob `/api/` em páginas SPA;
essas rotas retornam 404.
