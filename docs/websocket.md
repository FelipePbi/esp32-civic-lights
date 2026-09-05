# WebSocket

Endpoint same-origin: `ws://192.168.4.1/ws`. O canal é usado para eventos do
ESP32 para o navegador; comandos continuam em REST.

## Conexão e snapshot

Após o upgrade HTTP, cada cliente recebe imediatamente um frame JSON com
`type: "snapshot"`. Todo evento é autocontido e inclui firmware, uptime,
clientes Wi-Fi, status e geração do grupo, capabilities, Desired State,
LEFT/RIGHT, estados observados e favorita. O firmware continua sendo a fonte de
verdade.

## Tipos de evento

```text
snapshot
wifi_client_connected
wifi_client_disconnected
group_status
controller_status
desired_state
observed_state
sync_complete
sync_failed
favorite_updated
remote_button
remote_action_started
remote_action_completed
remote_action_failed
remote_config_updated
remote_discovery_started
remote_discovery_stopped
police_starting
police_started
police_stopped
police_timeout
police_failed
runtime_animation
indicator_status
indicator_test_started
```

Os frames só são publicados quando estado relevante muda. O arraste do seletor
não produz logs INFO nem polling HTTP de status.

## Clientes e concorrência

O servidor enumera todos os sockets WebSocket ativos e usa o envio assíncrono
do `esp_http_server`; assim iPhone e notebook podem acompanhar simultaneamente.
O payload permanece alocado até o callback de envio terminar. Sockets fechados
deixam a lista do servidor automaticamente.

Frames de controle são tratados pelo servidor HTTP. Frames de aplicação
recebidos do browser não alteram BLE ou Desired State.

## Reconexão no frontend

Ao perder o socket, a interface conserva o último snapshot válido e tenta
reconectar após 500 ms, 1 s, 2 s e depois 5 s. O indicador **RECONECTANDO** do
cabeçalho descreve browser ↔ ESP32; os cartões LEFT/RIGHT descrevem BLE.
