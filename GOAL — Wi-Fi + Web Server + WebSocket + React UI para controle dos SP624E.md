# GOAL — Wi-Fi + Web Server + WebSocket + React UI para controle dos SP624E

## 1. Objetivo

Evoluir o firmware ESP32 atual para a versão `0.5.0`, adicionando uma interface web completa que será hospedada pelo próprio ESP32 e acessada diretamente pelo iPhone.

Não haverá:

- servidor externo;
- cloud;
- Vercel;
- AWS;
- computador ligado;
- App Store;
- aplicativo iOS;
- Apple Developer Program.

Arquitetura final desta etapa:

```text
                 iPhone
                   │
                   │ Wi-Fi
                   ▼
          ┌─────────────────┐
          │      ESP32      │
          │                 │
          │ Wi-Fi SoftAP    │
          │ HTTP Server     │
          │ WebSocket       │
          │ React UI        │
          └────────┬────────┘
                   │
              Desired State
                   │
             Group Controller
                   │
            ┌──────┴──────┐
            ▼             ▼
         SP624E         SP624E
          LEFT           RIGHT
```

O frontend deve permitir:

1. visualizar conexão do LEFT;
2. visualizar conexão do RIGHT;
3. visualizar status geral `SYNCED`, `DEGRADED`, `RECONNECTING`, etc.;
4. escolher qualquer cor RGB;
5. controlar brilho;
6. botão rápido para branco real;
7. botão rápido para uma cor favorita;
8. cor favorita inicialmente vermelha;
9. permitir salvar outra cor como favorita;
10. receber atualizações de conexão em tempo real;
11. funcionar no Safari do iPhone;
12. poder ser adicionado à Tela de Início.

---

# 2. Projeto existente

Projeto:

```text
C:\Projetos\ESP32
```

Não recriar.

Firmware atual:

```text
0.4.0
```

Base validada fisicamente:

```text
ESP32-D0WD-V3
ESP-IDF v6.0.2
NimBLE
BLE-only
4 MB flash
```

Mapping:

```text
LEFT
FF:FF:11:CD:AC:FA

RIGHT
FF:FF:11:CD:A0:60
```

Já comprovado:

- duas conexões BLE simultâneas;
- reconexão automática;
- backoff;
- command queues;
- generations;
- stale command cancellation;
- Strict Sync;
- Desired State;
- State Reconciler;
- health checks;
- State Query;
- notifications;
- NVS;
- 10/10 reconnect stress PASS;
- mid-command failure recovery PASS;
- teste contínuo de 15 min PASS;
- nenhuma memory leak evidente;
- nenhuma queda no teste final.

Não regressar essas funcionalidades.

---

# 3. Princípio fundamental

A interface web:

**NUNCA deve controlar diretamente LEFT ou RIGHT.**

PROIBIDO:

```text
HTTP request
→ write diretamente LEFT
→ write diretamente RIGHT
```

Obrigatório:

```text
HTTP request
        ↓
Desired State
        ↓
generation++
        ↓
Group Controller
        ↓
Command Queues
        ↓
LEFT + RIGHT
        ↓
State Query
        ↓
Verification
        ↓
SYNCED
```

Toda alteração visual deve passar pela arquitetura já validada.

---

# 4. Firmware

Atualizar versão para:

```text
0.5.0
```

Boot esperado:

```text
SP624E Controller
Firmware: 0.5.0
```

---

# 5. Wi-Fi

Configurar o ESP32 inicialmente como:

```text
WIFI_MODE_AP
```

O ESP32 será o próprio Access Point.

SSID default:

```text
Civic-Lights
```

Não depender de roteador externo.

---

# 6. Segurança do Wi-Fi

Utilizar WPA2 quando suportado pela configuração atual.

A senha não deve ficar espalhada pelo código.

Criar configuração centralizada.

Exemplo:

```text
main/config/app_config.h
```

ou equivalente.

Permitir posteriormente carregar a senha de NVS.

Não colocar uma senha pessoal real no Git.

Utilizar inicialmente uma senha de desenvolvimento claramente identificada e documentar como alterá-la antes da instalação definitiva.

---

# 7. Rede

Configuração esperada:

```text
ESP32 AP:
192.168.4.1
```

Fluxo:

```text
iPhone
↓
Wi-Fi
↓
Civic-Lights
↓
Safari
↓
http://192.168.4.1
```

O funcionamento não pode depender da existência de internet.

---

# 8. Wi-Fi + BLE

Wi-Fi e BLE utilizarão o mesmo ESP32.

Portanto:

- não fazer tráfego Wi-Fi desnecessário;
- não fazer polling HTTP rápido;
- usar WebSocket para eventos;
- não enviar status centenas de vezes por segundo;
- preservar BLE como prioridade funcional;
- medir comportamento durante uso simultâneo.

Não alterar parâmetros BLE estáveis sem evidência de necessidade.

---

# 9. Backend HTTP

Utilizar o servidor oficial:

```text
esp_http_server
```

do ESP-IDF.

Habilitar:

```text
CONFIG_HTTPD_WS_SUPPORT
```

Utilizar APIs compatíveis com ESP-IDF 6.0.2.

---

# 10. Estrutura sugerida

Adicionar algo semelhante a:

```text
main/
├── web/
│   ├── web_server.c
│   ├── web_server.h
│   ├── api_handlers.c
│   ├── api_handlers.h
│   ├── websocket.c
│   └── websocket.h
│
├── wifi/
│   ├── wifi_ap.c
│   └── wifi_ap.h
│
├── presets/
│   ├── preset_manager.c
│   └── preset_manager.h
│
└── ...
```

Frontend:

```text
web/
├── package.json
├── vite.config.ts
├── tsconfig.json
├── src/
│   ├── components/
│   ├── hooks/
│   ├── services/
│   ├── types/
│   ├── App.tsx
│   └── main.tsx
└── public/
```

Não misturar React com o código de firmware.

---

# 11. Frontend

Utilizar:

```text
React
TypeScript
Vite
```

Priorizar bundle pequeno.

Evitar bibliotecas grandes sem necessidade.

Antes de instalar biblioteca para:

```text
color picker
state management
UI framework
```

avaliar se uma implementação pequena é suficiente.

Não instalar Material UI ou bibliotecas enormes apenas para construir alguns controles.

---

# 12. Dependências web

Verificar se Node.js está instalado no Windows.

Caso não esteja, instalar uma versão LTS atual compatível.

Utilizar:

```text
npm
```

ou o package manager já padronizado no projeto se existir.

Criar build reproduzível:

```powershell
npm install
npm run build
```

---

# 13. Armazenamento do frontend

O ESP32 possui:

```text
4 MB flash
```

O firmware atual ocupa aproximadamente:

```text
458 KB
```

Criar partition table adequada para dar espaço separado ao frontend.

Sugestão inicial:

```text
nvs      0x9000   0x6000
phy_init 0xF000   0x1000
factory  0x10000  0x200000
web      0x210000 0x1F0000
```

O layout real deve ser validado tecnicamente.

A partição `web` pode utilizar SPIFFS ou outra solução oficialmente suportada e apropriada pelo ESP-IDF.

Não reduzir perigosamente o espaço disponível para firmware.

Não implementar OTA nesta etapa.

---

# 14. Build web → firmware

Automatizar:

```text
React/Vite
↓
npm run build
↓
web/dist
↓
filesystem image
↓
flash ESP32
```

Criar script:

```text
scripts/build-web.ps1
```

E integrar quando apropriado ao:

```text
scripts/flash-monitor.ps1
```

Criar também:

```text
scripts/flash-web.ps1
```

para atualizar somente assets web quando tecnicamente possível.

---

# 15. Servir arquivos estáticos

O ESP32 deverá servir:

```text
/
index.html

/assets/...
JS
CSS

/icon...
manifest...
```

Implementar MIME types corretamente:

```text
.html → text/html
.js   → text/javascript / application/javascript
.css  → text/css
.json → application/json
.png
.svg
.ico
```

Não carregar o arquivo inteiro na RAM quando puder enviar em chunks.

---

# 16. Web App no iPhone

Preparar a interface para:

```text
Safari
→ Compartilhar
→ Adicionar à Tela de Início
```

Adicionar metadados apropriados:

```text
viewport
theme-color
apple-mobile-web-app-capable
apple-mobile-web-app-status-bar-style
apple-mobile-web-app-title
```

Criar:

```text
manifest.webmanifest
```

com nome:

```text
Civic Lights
```

Não implementar Service Worker nesta fase caso o HTTP local impeça contexto seguro.

A aplicação NÃO depende de Service Worker para funcionar.

---

# 17. Design

Criar uma interface mobile-first.

Objetivo visual:

```text
moderna
minimalista
automotiva
escura
limpa
alto contraste
```

Evitar aparência genérica de dashboard administrativo.

---

# 18. Tela principal

Estrutura conceitual:

```text
┌────────────────────────────┐
│ CIVIC LIGHTS               │
│                            │
│ ● LEFT        Conectado    │
│ ● RIGHT       Conectado    │
│                            │
│      ✓ SINCRONIZADO        │
│                            │
│      COLOR PICKER          │
│                            │
│ Cor atual                  │
│ ███████████████████        │
│ #FF0033                    │
│                            │
│ Brilho                     │
│ ─────────────●────         │
│               72%          │
│                            │
│ Acesso rápido              │
│                            │
│ [ Branco ] [ Favorita ]    │
│                            │
│ Favorita: Vermelho         │
│                            │
└────────────────────────────┘
```

Isso é orientação, não obrigação pixel-perfect.

---

# 19. Status LEFT e RIGHT

Cada lado deve mostrar:

```text
CONNECTED
CONNECTING
RECONNECTING
READY
ERROR
OFFLINE
```

e, quando relevante:

```text
RSSI
```

Exemplo:

```text
LEFT
● Conectado
-82 dBm
```

```text
RIGHT
◉ Reconectando...
```

Não usar apenas cor.

Sempre acompanhar o indicador visual com texto.

---

# 20. Group Status

Mostrar claramente:

```text
SYNCED
DEGRADED
RECONCILING
UNSYNCED
ERROR
```

Sugestões:

```text
SYNCED
✓ Sincronizado
```

```text
DEGRADED
⚠ Reconectando farol direito
```

A UI não deve afirmar:

```text
Sincronizado
```

até o firmware realmente declarar `SYNCED`.

---

# 21. WebSocket

Endpoint:

```text
GET /ws
```

WebSocket deve ser utilizado principalmente:

```text
ESP32 → browser
```

para eventos em tempo real.

Não é obrigatório usar WebSocket para alteração de estado.

REST para comandos e WebSocket para eventos deixa a arquitetura inicial mais simples.

---

# 22. Eventos WebSocket

Implementar eventos tipados.

Exemplo:

```json
{
  "type": "group_status",
  "state": "SYNCED",
  "generation": 42
}
```

Controller:

```json
{
  "type": "controller_status",
  "side": "RIGHT",
  "state": "RECONNECTING",
  "connected": false,
  "rssi": -87
}
```

Desired state:

```json
{
  "type": "desired_state",
  "generation": 43,
  "mode": "rgb",
  "r": 255,
  "g": 0,
  "b": 80,
  "brightness": 64
}
```

---

# 23. Snapshot inicial WebSocket

Ao conectar um novo cliente, enviar snapshot do sistema.

Ele deve conter:

```text
firmware
uptime
group state
desired generation
LEFT status
RIGHT status
desired state
favorite preset
```

Assim a interface não precisa esperar eventos futuros para montar a tela.

---

# 24. WebSocket reconnect no frontend

Se o navegador perder WebSocket:

```text
CONNECTED
↓
DISCONNECTED
↓
reconnect com backoff
```

Sugestão:

```text
500 ms
1 s
2 s
5 s
máximo 5 s
```

Mostrar na UI:

```text
Reconectando ao ESP32...
```

Isso significa conexão:

```text
browser ↔ ESP32
```

e não necessariamente BLE.

Não confundir os dois estados.

---

# 25. API

Criar namespace:

```text
/api/v1
```

---

# 26. GET status

Endpoint:

```text
GET /api/v1/status
```

Resposta aproximada:

```json
{
  "firmware": "0.5.0",
  "uptime_ms": 123456,
  "group": {
    "state": "SYNCED",
    "generation": 42
  },
  "left": {
    "connected": true,
    "state": "READY",
    "rssi": -82
  },
  "right": {
    "connected": true,
    "state": "READY",
    "rssi": -85
  }
}
```

---

# 27. GET state

```text
GET /api/v1/state
```

Retornar:

```text
Desired State
Observed LEFT
Observed RIGHT
verified generations
```

Não expor ponteiros, handles internos ou dados não inicializados.

---

# 28. Alteração RGB

Endpoint:

```text
PUT /api/v1/state
```

Exemplo RGB:

```json
{
  "mode": "rgb",
  "r": 255,
  "g": 0,
  "b": 80,
  "brightness": 64
}
```

Validar:

```text
r 0–255
g 0–255
b 0–255
brightness 0–255
```

Payload inválido:

```text
HTTP 400
```

---

# 29. Resultado de alteração

A API não deve mentir que os faróis já estão sincronizados apenas porque aceitou o request.

Pode responder:

```json
{
  "accepted": true,
  "generation": 43,
  "group_state": "RECONCILING"
}
```

A confirmação final chega via WebSocket:

```json
{
  "type": "group_status",
  "state": "SYNCED",
  "generation": 43
}
```

---

# 30. Color Picker

Criar seletor de cor RGB touch-friendly.

Deve funcionar bem:

```text
iPhone
Safari
touch
```

Mostrar:

```text
preview
hex
RGB
```

Exemplo:

```text
#FF0050
RGB 255, 0, 80
```

---

# 31. Color Picker e frequência

O usuário pode arrastar o dedo rapidamente.

NÃO enviar centenas de requests por segundo.

Implementar throttle/debounce.

Sugestão inicial:

```text
80–120 ms
```

enquanto arrasta.

Ao finalizar o gesto:

```text
enviar estado final imediatamente
```

O firmware já possui:

```text
generation
stale commands
coalescing
```

e isso deve continuar funcionando.

---

# 32. Feedback de sincronização

Quando selecionar cor:

```text
UI muda preview imediatamente
↓
request aceito
↓
Sincronizando...
↓
ESP32 verifica LEFT/RIGHT
↓
SYNCED
↓
✓ Aplicado
```

Não bloquear a interface esperando cada BLE write.

---

# 33. Slider de brilho

Adicionar:

```text
0–100%
```

Na UI.

Converter para:

```text
0–255
```

no frontend ou backend de maneira consistente.

Mostrar percentual.

Utilizar o comando de brilho já validado no hardware.

Aplicar debounce igual ao color picker.

---

# 34. Branco real

Este requisito é importante.

O botão:

```text
BRANCO
```

deve utilizar o canal branco real do SP624E, e não simplesmente:

```text
RGB 255,255,255
```

A referência do protocolo indica:

```text
15 01 CC
```

para selecionar White.

E:

```text
21 02 LEVEL FF
```

para intensidade do branco.

PORÉM:

o relatório anterior marcou:

```text
21 02 white: NOT_TESTED
```

Portanto ele NÃO pode ser assumido como validado.

---

# 35. Validação do branco antes da UI

Antes de liberar o botão Branco:

1. conectar ambos;
2. obter snapshots;
3. confirmar grupo SYNCED;
4. testar primeiro em um controlador;
5. utilizar brilho moderado;
6. consultar estado;
7. confirmar mudança;
8. restaurar snapshot;
9. repetir no segundo;
10. testar nos dois pelo Group Controller;
11. restaurar;
12. verificar estados.

Se falhar:

```text
WHITE_PRESET_DISABLED
```

Não expor botão funcional quebrado.

---

# 36. Desired State com modos

Evoluir Desired State para distinguir explicitamente:

```text
RGB
WHITE
```

Conceitualmente:

```c
typedef enum {
    LIGHT_MODE_RGB,
    LIGHT_MODE_WHITE
} desired_light_mode_t;
```

Não inferir modo apenas comparando R/G/B.

---

# 37. Desired RGB

Exemplo:

```text
mode = RGB
effect = 0x63
RGB = ...
brightness = ...
```

---

# 38. Desired WHITE

Depois de validado:

```text
mode = WHITE
effect = 0xCC
white = ...
```

O reconciler deve saber comparar corretamente um estado WHITE.

---

# 39. Botão Branco

Interface:

```text
[ Branco ]
```

Ao tocar:

```text
Desired State
mode = WHITE
white brightness = valor atual apropriado
generation++
```

Não fazer write direto.

---

# 40. Preset favorito

Adicionar:

```text
[ Favorita ]
```

Inicialmente:

```text
vermelho
```

Default:

```json
{
  "mode": "rgb",
  "r": 255,
  "g": 0,
  "b": 0,
  "brightness": 64
}
```

---

# 41. Alterar Favorita

Permitir ao usuário:

1. escolher uma cor no color picker;
2. escolher brilho;
3. selecionar:

```text
Salvar como favorita
```

Persistir no ESP32.

Depois o botão deve representar essa cor.

Exemplo:

```text
Favorita
● Roxo
```

---

# 42. Persistência da favorita

Utilizar NVS.

Namespace apropriado.

Persistir:

```text
mode
RGB
brightness
version
```

Não gravar NVS durante cada movimento do picker.

Somente quando:

```text
Salvar como favorita
```

for solicitado.

---

# 43. API de preset

Exemplo:

```text
GET /api/v1/presets
```

Resposta:

```json
{
  "favorite": {
    "mode": "rgb",
    "r": 255,
    "g": 0,
    "b": 0,
    "brightness": 64
  },
  "white_available": true
}
```

Atualização:

```text
PUT /api/v1/presets/favorite
```

---

# 44. Botão favorito

Ao tocar:

```text
Favorite Preset
↓
Desired State
↓
generation++
↓
Strict Sync
```

---

# 45. Comportamento quando um lado estiver offline

Este é requisito crítico.

Exemplo:

```text
LEFT READY
RIGHT RECONNECTING
```

Usuário seleciona AZUL.

Strict Sync existente deve continuar valendo:

```text
Desired State = BLUE
generation++
```

mas:

```text
NÃO aplicar BLUE somente no LEFT.
```

A UI deve mostrar:

```text
Cor aguardando reconexão do RIGHT
```

Quando ambos estiverem READY:

```text
aplicar nos dois
↓
verify
↓
SYNCED
```

---

# 46. Estado visual da UI durante DEGRADED

Exemplo:

```text
⚠ Farol direito reconectando

Sua nova cor será aplicada quando
os dois faróis estiverem disponíveis.
```

Não precisa usar exatamente esse texto.

O comportamento é obrigatório.

---

# 47. Resync

Adicionar botão de diagnóstico discreto:

```text
Ressincronizar
```

Endpoint:

```text
POST /api/v1/resync
```

Ele deve pedir ao:

```text
State Reconciler
```

para consultar e reconciliar.

Não desconectar BLE sem necessidade.

---

# 48. Estado WebSocket

Eventos importantes:

```text
wifi_client_connected
group_status
controller_status
desired_state
observed_state
sync_complete
sync_failed
favorite_updated
```

Não enviar métricas constantemente se nada mudou.

---

# 49. Thread safety

Handlers HTTP e WebSocket rodam em contexto diferente das tasks BLE.

Portanto:

PROIBIDO acessar estruturas mutáveis compartilhadas sem proteção.

Handlers devem preferencialmente:

```text
parse request
↓
validate
↓
post event / call thread-safe Group API
↓
return
```

Não fazer GATT write diretamente dentro do HTTP handler.

---

# 50. API do Group Controller

Criar uma interface thread-safe reutilizável.

Algo conceitualmente como:

```c
group_controller_set_rgb(...)
group_controller_set_white(...)
group_controller_apply_preset(...)
group_controller_force_resync(...)
group_controller_get_snapshot(...)
```

A futura entrada 433 MHz deverá usar essas mesmas funções.

---

# 51. WebSocket thread safety

Mudanças BLE podem ocorrer fora da task HTTP.

Para push assíncrono WebSocket utilizar as APIs corretas do `esp_http_server`.

Não enviar frame usando estruturas temporárias que serão destruídas antes do envio.

Evitar use-after-free.

---

# 52. Clientes WebSocket

Não assumir apenas um cliente.

Suportar pelo menos:

```text
2 clientes
```

sem crash.

Por exemplo:

```text
iPhone
+
notebook de diagnóstico
```

Limpar sockets desconectados.

---

# 53. Sem CORS desnecessário

Como frontend e API estão no mesmo ESP32:

```text
http://192.168.4.1
```

utilizar same-origin.

Não habilitar:

```text
Access-Control-Allow-Origin: *
```

sem necessidade.

---

# 54. UI responsiva

Prioridade:

```text
iPhone portrait
```

Também deve funcionar razoavelmente em desktop.

Utilizar:

```text
safe-area-inset-top
safe-area-inset-bottom
```

quando apropriado.

---

# 55. Touch targets

Botões devem ser confortáveis no iPhone.

Evitar elementos minúsculos.

Manter aproximadamente:

```text
44px
```

ou mais para ações principais.

---

# 56. Cores e acessibilidade

Não indicar:

```text
conectado/desconectado
```

somente por verde/vermelho.

Mostrar texto.

Garantir contraste suficiente.

---

# 57. UX principal

O usuário deve conseguir abrir e:

```text
ver conexão
↓
escolher cor
```

em poucos segundos.

Evitar:

- menus excessivos;
- dashboards complexos;
- métricas técnicas na tela principal.

Métricas avançadas podem ficar em:

```text
Diagnóstico
```

ou seção recolhível.

---

# 58. Diagnóstico opcional na UI

Adicionar seção pequena:

```text
Diagnóstico
```

mostrando:

```text
Firmware
Uptime
LEFT RSSI
RIGHT RSSI
Generation
Reconnect count
```

Não poluir a tela principal.

---

# 59. Carregamento

Ao abrir:

```text
Conectando ao Civic Lights...
```

Depois:

```text
GET /api/v1/status
+
WebSocket connect
```

Não mostrar status falso antes dos dados reais.

---

# 60. Sem internet

Testar com:

```text
iPhone conectado ao Civic-Lights
sem internet
```

A interface deve continuar 100% funcional.

Não utilizar:

- Google Fonts;
- CDN;
- scripts externos;
- imagens externas;
- APIs externas.

TODOS os assets devem estar no ESP32.

---

# 61. Fontes

Utilizar system font stack.

Por exemplo:

```text
-apple-system
BlinkMacSystemFont
Segoe UI
sans-serif
```

Não baixar fonte pela internet.

---

# 62. Ícone

Criar ícone simples local para:

```text
Civic Lights
```

Não depender de arquivo remoto.

Pode ser SVG criado no próprio projeto.

---

# 63. Teste no navegador desktop

Antes do iPhone:

abrir no Windows:

```text
http://192.168.4.1
```

Testar:

- load;
- API;
- WebSocket;
- RGB;
- brilho;
- Branco;
- Favorita;
- reconnect visual.

---

# 64. Teste no iPhone

Obrigatório realizar teste físico quando o usuário estiver disponível.

Fluxo:

```text
Ajustes
↓
Wi-Fi
↓
Civic-Lights
↓
Safari
↓
http://192.168.4.1
```

Confirmar:

- página abre;
- status aparece;
- LEFT aparece conectado;
- RIGHT aparece conectado;
- color picker funciona por touch;
- ambos mudam de cor;
- brilho funciona;
- Branco funciona;
- Favorita funciona;
- WebSocket atualiza em tempo real.

---

# 65. Adicionar à Tela de Início

Depois validar:

```text
Safari
↓
Compartilhar
↓
Adicionar à Tela de Início
```

Abrir pelo novo ícone.

Confirmar:

```text
UI utilizável
HTTP API funciona
WebSocket funciona
```

Se alguma limitação do iOS impedir comportamento de app standalone sobre HTTP local, documentar o comportamento real sem simular sucesso.

---

# 66. Teste BLE + Wi-Fi

Muito importante.

Executar com:

```text
Wi-Fi AP ativo
iPhone conectado
WebSocket ativo
LEFT conectado
RIGHT conectado
```

Durante pelo menos:

```text
15 minutos
```

Medir:

```text
BLE disconnects
Wi-Fi disconnects
WebSocket reconnects
heap
minimum heap
watchdog
panic
```

---

# 67. Teste de uso RGB

Durante a sessão:

realizar pelo menos:

```text
50 alterações RGB
```

incluindo movimentos rápidos no picker.

Verificar:

```text
stale generations descartadas
queue não cresce indefinidamente
última cor correta
LEFT = RIGHT
GROUP = SYNCED
```

---

# 68. Teste reconnect com UI aberta

Com WebSocket ativo:

```text
GROUP SYNCED
↓
software disconnect RIGHT
↓
UI mostra RIGHT reconnecting
↓
Connection Manager recupera
↓
UI recebe atualização
↓
RIGHT READY
↓
GROUP SYNCED
```

Nenhum refresh manual da página.

Repetir LEFT.

---

# 69. Teste comando durante reconnect

Fluxo:

```text
RIGHT offline
↓
selecionar nova cor no iPhone
↓
Desired State atualiza
↓
LEFT NÃO muda sozinho
↓
RIGHT reconecta
↓
nova cor aplicada nos dois
↓
SYNCED
```

Este teste é obrigatório porque reproduz exatamente o problema original do BanlanX.

---

# 70. Teste Branco

Depois da validação inicial:

```text
tap Branco
↓
mode WHITE
↓
LEFT + RIGHT
↓
verify
↓
SYNCED
```

Depois selecionar uma cor RGB.

Confirmar transição:

```text
WHITE → RGB
```

Depois:

```text
RGB → WHITE
```

Sem side mismatch.

---

# 71. Teste Favorita

Default:

```text
RED
```

Testar:

```text
Branco
↓
Favorita
↓
Branco
↓
Favorita
```

Depois:

1. escolher roxo;
2. salvar como favorita;
3. reboot ESP32;
4. abrir interface;
5. confirmar favorita roxa persistida;
6. tocar;
7. confirmar dois faróis.

---

# 72. Reboot

Com Wi-Fi implementado:

```text
reboot ESP32
```

Confirmar:

```text
mapping NVS carregado
Wi-Fi sobe
web server sobe
LEFT conecta
RIGHT conecta
state query
GROUP chega a estado correto
```

Manter regra existente:

```text
restore_on_boot = false
```

Não mudar visualmente os faróis só por reiniciar o ESP32.

---

# 73. Performance

Medir:

```text
tempo boot → Wi-Fi disponível
tempo request → accepted
tempo request → SYNCED
tempo de carregamento inicial da UI
```

Não impor metas artificiais.

Registrar valores reais.

---

# 74. Memória

Comparar:

```text
free heap antes de Wi-Fi
free heap depois do Wi-Fi
free heap com WebSocket
minimum free heap
```

Não aceitar queda contínua.

---

# 75. Logs

Adicionar tags:

```text
WIFI
HTTP
WS
WEB_API
PRESET
```

Não logar cada frame de color picker em INFO.

Logs de alta frequência devem ser DEBUG.

---

# 76. Scripts

Criar/atualizar:

```text
scripts/build-web.ps1
scripts/flash-web.ps1
scripts/build.ps1
scripts/flash-monitor.ps1
scripts/doctor.ps1
```

Criar comando simples de desenvolvimento, idealmente:

```powershell
.\scripts\build-web.ps1
.\scripts\flash-monitor.ps1
```

ou um único:

```powershell
.\scripts\build-all.ps1
```

que faça:

```text
frontend
↓
filesystem
↓
firmware
```

---

# 77. README

Documentar:

## Firmware

```text
build
flash
monitor
```

## Frontend

```text
install
dev
build
```

## Uso no carro

```text
1. ESP32 liga
2. conectar ao Civic-Lights
3. abrir http://192.168.4.1
```

## Tela de Início do iPhone

Documentar processo observado no aparelho real.

---

# 78. AGENTS.md

Adicionar regras:

```text
Web API nunca escreve diretamente no BLE.

Toda mudança visual passa pelo Desired State.

WebSocket não é fonte de verdade.

Firmware é a fonte de verdade.

Frontend deve funcionar sem internet.

Nenhum asset externo.

Color picker deve ser throttled.

Não quebrar Strict Sync.

Não aplicar alteração somente em um lado por conveniência.
```

---

# 79. Documentação

Criar:

```text
docs/web-architecture.md
docs/http-api.md
docs/websocket.md
docs/iphone-usage.md
```

---

# 80. Testes frontend

Adicionar pelo menos testes para:

- RGB validation;
- RGB → HEX;
- HEX → RGB;
- brightness conversion;
- API serialization;
- WebSocket event parser;
- status mapping;
- debounce/throttle;
- favorite preset.

Utilizar stack de testes leve compatível com Vite.

---

# 81. Testes backend

Adicionar testes para:

- JSON parsing;
- ranges RGB;
- malformed payload;
- oversized payload;
- invalid mode;
- favorite persistence;
- desired state update;
- thread-safe group command;
- status serialization.

---

# 82. Segurança de payload

Definir limite de body HTTP.

Rejeitar payload excessivamente grande.

Não utilizar:

```text
strcpy
sprintf
memcpy
```

sem limites adequados.

Todos os JSONs devem ser validados.

---

# 83. API response codes

Utilizar adequadamente:

```text
200 OK
202 Accepted
400 Bad Request
404 Not Found
409 Conflict quando apropriado
500 apenas para erro interno real
503 quando sistema temporariamente indisponível
```

---

# 84. White capability

A API de status deve expor:

```json
{
  "capabilities": {
    "rgb": true,
    "white": true
  }
}
```

Somente marcar:

```text
white = true
```

depois de validação física bem-sucedida.

---

# 85. Fallback

Se white falhar:

frontend deve:

```text
desabilitar botão Branco
```

e mostrar de forma discreta:

```text
Canal branco indisponível
```

Não substituir silenciosamente por RGB branco.

---

# 86. Critérios de aceite

A tarefa somente estará concluída quando:

- [ ] firmware estiver em 0.5.0;
- [ ] Wi-Fi SoftAP funcionar;
- [ ] SSID Civic-Lights estiver acessível;
- [ ] iPhone conseguir conectar;
- [ ] HTTP Server funcionar;
- [ ] React build estiver hospedado no ESP32;
- [ ] nenhum asset depender de internet;
- [ ] `/api/v1/status` funcionar;
- [ ] `/api/v1/state` funcionar;
- [ ] WebSocket funcionar;
- [ ] LEFT status aparecer em tempo real;
- [ ] RIGHT status aparecer em tempo real;
- [ ] Group Status aparecer em tempo real;
- [ ] color picker funcionar no iPhone;
- [ ] RGB passar pelo Desired State;
- [ ] brilho funcionar;
- [ ] slider não gerar spam incontrolável;
- [ ] White for fisicamente validado antes de ser habilitado;
- [ ] botão Branco funcionar nos dois;
- [ ] transição RGB ↔ WHITE funcionar;
- [ ] Favorita default vermelha funcionar;
- [ ] usuário puder salvar outra favorita;
- [ ] Favorita persistir após reboot;
- [ ] Strict Sync continuar funcionando;
- [ ] comando feito enquanto um lado está offline não alterar somente o lado saudável;
- [ ] reconnect aparecer na UI sem refresh;
- [ ] reconnect automático continuar funcionando;
- [ ] teste BLE + Wi-Fi de 15 minutos passar;
- [ ] 50 alterações RGB passarem;
- [ ] nenhuma memory leak evidente;
- [ ] nenhum panic;
- [ ] nenhum watchdog;
- [ ] nenhum reboot inesperado;
- [ ] estado final LEFT == RIGHT;
- [ ] GROUP final == SYNCED;
- [ ] documentação atualizada.

---

# 87. Relatório final

Entregar:

```text
SP624E WEB CONTROLLER REPORT
============================

Firmware
--------
Version:
Build:
Flash:
Web partition:
Free firmware space:
Web bundle size:

Wi-Fi
-----
Mode:
SSID:
IP:
Security:
iPhone connected:
Internet required: NO

Web
---
React:
TypeScript:
Vite:
HTTP server:
Initial load:
Added to iPhone Home Screen:

REST API
--------
GET status:
GET state:
PUT state:
Preset API:
Resync API:

WebSocket
---------
Connection:
Reconnect:
LEFT updates:
RIGHT updates:
Group updates:

RGB
---
Color picker:
Brightness:
Requests during drag:
Generation/coalescing:
50-change test:
Final sync:

White
-----
15 01 CC:
21 02 LEVEL FF:
Individual LEFT:
Individual RIGHT:
Group test:
RGB → WHITE:
WHITE → RGB:
Capability enabled:

Favorite
--------
Default:
Update:
NVS:
Reboot persistence:
Apply result:

Strict Sync UI Test
-------------------
Disconnected side:
New RGB requested:
Healthy side remained unchanged:
Reconnect:
Both applied:
Final state:

BLE + Wi-Fi Stability
---------------------
Duration:
LEFT disconnects:
RIGHT disconnects:
Wi-Fi disconnects:
WebSocket reconnects:
Health checks:
Unexpected desyncs:

Memory
------
Initial free heap:
With Wi-Fi:
With WebSocket:
Final:
Minimum:

Safety
------
Panic:
Watchdog:
Unexpected reboot:
Persistent incorrect state:
Final LEFT:
Final RIGHT:
Final GROUP:

Conclusion
----------

Open Issues
-----------

Recommended Next Step
---------------------
```

---

# 88. Resultado esperado

O produto desta etapa deve permitir:

```text
Entrar no carro
↓
ESP32 liga
↓
Wi-Fi Civic-Lights aparece
↓
iPhone conecta
↓
abrir ícone Civic Lights
↓

LEFT     ● Conectado
RIGHT    ● Conectado

✓ SINCRONIZADO

[ SELETOR DE COR ]

Brilho ───────●─────

[ BRANCO ] [ FAVORITA ]

↓
selecionar roxo
↓
ESP32 recebe Desired State
↓
LEFT + RIGHT ficam roxos
↓
State Query
↓
verificação
↓
✓ SINCRONIZADO
```

Se RIGHT cair:

```text
RIGHT
◉ Reconectando...

⚠ DEGRADED
```

O usuário não precisa fazer nada.

Quando recuperar:

```text
RIGHT
● Conectado

✓ SINCRONIZADO
```

---

# 89. Próxima etapa

NÃO implementar nesta tarefa.

Se este goal passar, a próxima etapa será:

```text
Receptor RF 433 MHz
↓
controle remoto físico
↓
mesmo Desired State
↓
mesmo Group Controller
```

PWA/Web e controle 433 MHz deverão compartilhar exatamente a mesma arquitetura de controle.

---

# REGRA FINAL

Não construir apenas uma página bonita.

O objetivo é provar que:

```text
iPhone
↓
Wi-Fi
↓
HTTP / WebSocket
↓
Desired State
↓
Group Controller
↓
LEFT + RIGHT
↓
Verification
↓
SYNCED
```

funciona sem enfraquecer nenhuma das garantias de confiabilidade já obtidas nas versões anteriores.

Testar tudo no hardware real antes de considerar a tarefa concluída.