# HTTP API e WebSocket

Base same-origin: `http://192.168.4.1/api/v1`. Não há CORS aberto.

## REST

### `GET /api/v1/status`

Retorna firmware, uptime, clientes Wi-Fi, estado do grupo e conexão/RSSI de
LEFT e RIGHT. Também retorna capabilities; `white` só fica verdadeiro após a
validação física e confirmação persistida:

```json
{"capabilities":{"rgb":true,"white":false}}
```

### `GET /api/v1/state`

Retorna Desired State, estados observados e gerações verificadas. Campos
internos como handles BLE e ponteiros nunca são enviados.

### `PUT /api/v1/state`

RGB:

```json
{"mode":"rgb","r":255,"g":0,"b":80,"brightness":64}
```

WHITE, somente depois da validação física:

```json
{"mode":"white","brightness":96}
```

Todos os canais precisam ser inteiros entre 0 e 255. Payload inválido retorna
400. WHITE bloqueado retorna 409. Controller ainda indisponível ou fila ocupada
retorna 503. Aceite retorna 202:

```json
{"accepted":true,"generation":43,"group_state":"RECONCILING"}
```

202 não significa que o hardware já sincronizou. A confirmação chega por
WebSocket quando o grupo declara `SYNCED`.

### `GET /api/v1/presets`

```json
{
  "favorite":{"mode":"rgb","r":255,"g":0,"b":0,"brightness":64},
  "white_available":false
}
```

### `PUT /api/v1/presets/favorite`

Aceita o mesmo objeto RGB completo. A gravação NVS acontece somente nessa ação,
nunca durante o arraste do seletor.

### `POST /api/v1/resync`

Invalida as gerações verificadas e solicita reconciliação. Não força uma queda
BLE.

O protocolo de eventos está detalhado em [websocket.md](websocket.md).

### Controle remoto

- `GET /api/v1/remote`: receptor, VT, mapping, último botão, Button 4, Police e LED.
- `PUT /api/v1/remote/button4`: persiste ação do Button 4 em NVS.
- `PUT /api/v1/remote/mapping`: persiste mapping físico após descoberta.
- `POST /api/v1/remote/discovery/start`: suprime ações e publica canais físicos.
- `POST /api/v1/remote/discovery/stop`: reativa mapping persistido.

Exemplos Button 4:

```json
{"type":"favorite"}
{"type":"rgb","r":128,"g":0,"b":255,"brightness":64}
{"type":"white","brightness":128}
{"type":"police"}
```

Mapping requer quatro canais únicos; nunca é inferido:

```json
{"button1":"d2","button2":"d0","button3":"d3","button4":"d1"}
```

### Teste do indicador

`POST /api/v1/indicator/test` executa uma única sequência GPIO23 OFF 1 s, ON
1 s, OFF. Não altera Desired State nem BLE; chamada concorrente retorna 409.
