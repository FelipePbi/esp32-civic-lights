# GOAL — Controle remoto 433 MHz + RX480E + LED indicador + botão configurável pelo PWA

> Atualização 2026-08-14: todos os requisitos de Welcome descritos abaixo foram
> cancelados pelo usuário e removidos na versão 0.7.6. Police permanece.

## 1. Objetivo

Integrar definitivamente ao projeto Civic Lights:

```text
RX480E 433 MHz
+
controle remoto físico 1 / 2 / 3 / 4
+
LED indicador físico
```

utilizando a arquitetura já existente do ESP32.

O controle físico deve operar os dois SP624E através do mesmo fluxo utilizado pelo PWA:

```text
RF Remote
    ↓
Remote Controller
    ↓
Desired State / Animation Manager
    ↓
Group Controller
    ↓
LEFT + RIGHT queues
    ↓
SP624E LEFT + RIGHT
```

PROIBIDO fazer:

```text
GPIO RF
↓
BLE write direto
```

A confiabilidade e o Strict Sync existentes devem continuar sendo preservados.

---

# 2. Hardware já conectado

Considerar o hardware fisicamente conectado conforme abaixo.

## RX480E

```text
RX480E     ESP32

+V   →     3V3
GND  →     GND

D0   →     GPIO25
D1   →     GPIO26
D2   →     GPIO27
D3   →     GPIO32

VT   →     GPIO33
```

## LED indicador

```text
GPIO23
   ↓
resistor ~330 Ω
   ↓
LED
   ↓
GND
```

O LED já está fisicamente instalado.

Não alterar esse pinout sem necessidade comprovada.

---

# 3. Versão do firmware

Se a versão atual for:

```text
0.6.x
```

incrementar para uma versão apropriada.

Sugestão:

```text
0.7.0
```

---

# 4. Função dos quatro botões

O controle possui:

```text
1
2
3
4
```

Comportamento default:

```text
BOTÃO 1
→ Branco padrão

BOTÃO 2
→ Vermelho

BOTÃO 3
→ Police / Red-Blue Animation

BOTÃO 4
→ Preset configurável pelo PWA
```

---

# 5. Muito importante: descobrir D0/D1/D2/D3

NÃO assumir automaticamente:

```text
1 = D0
2 = D1
3 = D2
4 = D3
```

Antes da integração funcional, criar modo/teste que identifique fisicamente qual botão ativa qual saída.

Exemplo no monitor:

```text
RF INPUT
VT=1
D0=0
D1=1
D2=0
D3=0
```

E registrar:

```text
Button 1 → Dx
Button 2 → Dx
Button 3 → Dx
Button 4 → Dx
```

Somente depois definir o mapping.

---

# 6. Mapeamento persistente interno

Criar enum lógico independente dos GPIOs:

```c
typedef enum {
    REMOTE_BUTTON_1 = 0,
    REMOTE_BUTTON_2,
    REMOTE_BUTTON_3,
    REMOTE_BUTTON_4
} remote_button_t;
```

Não espalhar:

```text
GPIO25 == BUTTON1
```

pelo código.

Criar tabela/configuração central.

---

# 7. RX480E Manager

Criar módulo dedicado.

Estrutura sugerida:

```text
main/
└── remote/
    ├── rf_remote.c
    ├── rf_remote.h
    ├── rf_input.c
    ├── rf_input.h
    ├── rf_config.c
    └── rf_config.h
```

Responsabilidades:

```text
GPIO
↓
debounce
↓
edge/event detection
↓
physical channel mapping
↓
logical button event
↓
Remote Controller
```

---

# 8. GPIO configuration

Configurar:

```text
GPIO25 input
GPIO26 input
GPIO27 input
GPIO32 input
GPIO33 input
```

GPIO33:

```text
VT
```

Somente leitura.

Não utilizar interrupções complexas sem necessidade.

Pode usar:

```text
GPIO ISR
+
queue
```

ou polling leve.

Escolher a solução mais robusta e simples.

---

# 9. VT

Quando disponível:

```text
VT == valid transmission
```

usar como confirmação auxiliar.

Fluxo preferido:

```text
VT active
↓
sample D0-D3
↓
determine channel
↓
generate event
```

Não fazer a lógica depender cegamente de VT caso o comportamento real do RX480E seja diferente do esperado.

Validar fisicamente.

---

# 10. Debounce / duplicate suppression

Controles 433 podem gerar múltiplas leituras durante um único pressionamento.

Um clique físico deve normalmente virar:

```text
ONE logical event
```

Não:

```text
BUTTON 2
BUTTON 2
BUTTON 2
BUTTON 2
BUTTON 2
```

Implementar:

```text
edge detection
+
debounce
+
release detection
```

Sugestão inicial:

```text
debounce 30–80 ms
```

e bloqueio do mesmo botão até release.

Ajustar após teste real.

---

# 11. Long press

Nesta versão:

```text
não criar comandos diferentes para long press
```

Um pressionamento longo continua representando apenas um evento lógico.

Preparar arquitetura para futuro suporte, mas não implementar.

---

# 12. Remote Controller

Criar camada:

```text
Remote Controller
```

que converte:

```text
REMOTE_BUTTON_1
...
REMOTE_BUTTON_4
```

em ações do sistema.

Não acessar BLE diretamente.

---

# 13. Botão 1 — WHITE / padrão

BOTÃO 1 sempre representa:

```text
NORMAL / WHITE
```

Ao pressionar:

```text
cancelar qualquer animação ativa
↓
Desired State = WHITE
↓
brightness padrão apropriado
↓
Group Controller
↓
LEFT + RIGHT
↓
verify
↓
SYNCED
```

Usar canal WHITE real já validado pelo sistema.

Não utilizar:

```text
RGB 255,255,255
```

como substituto silencioso se WHITE real estiver disponível.

---

# 14. Botão 1 é fixo

O PWA NÃO poderá alterar a função do botão 1.

Sempre:

```text
BUTTON 1 = WHITE / NORMAL
```

---

# 15. Botão 2 — RED

Default:

```text
RGB
R=255
G=0
B=0
```

Brightness default:

utilizar valor seguro e coerente com o projeto.

Sugestão inicial:

```text
25%
```

ou valor atual usado como preset vermelho.

Não forçar 100% sem necessidade.

---

# 16. Botão 2 é fixo nesta versão

BUTTON 2:

```text
sempre RED
```

Não criar configuração via PWA ainda.

---

# 17. Botão 3 — Police Animation

Criar animação:

```text
POLICE
```

ou internamente:

```text
RED_BLUE_FLASH
```

Essa animação é destinada a:

```text
show / exposição / carro parado
```

e não uso normal em via pública.

---

# 18. Police Animation visual

Objetivo:

```text
RED
BLUE
RED
BLUE
```

com ritmo perceptível semelhante a assinatura de luz de emergência.

Porém:

```text
não exceder comandos BLE desnecessários
não criar backlog
não travar Group Controller
```

---

# 19. Padrão sugerido

Exemplo:

```text
RED 100 ms
OFF/DIM 40 ms
RED 100 ms

BLUE 100 ms
OFF/DIM 40 ms
BLUE 100 ms

repeat
```

OU um padrão mais visual:

```text
RED
RED
pause
BLUE
BLUE
pause
```

Testar inicialmente apenas em simulation/unit tests.

Ajustar a velocidade real somente no teste físico final.

---

# 20. Não utilizar o canal branco durante Police Animation

A animação usa:

```text
RGB RED
RGB BLUE
```

Não WHITE.

---

# 21. Police Animation deve usar Animation Manager

Integrar ao Animation Player já existente.

Não criar segunda arquitetura independente.

Exemplo:

```text
AnimationManager
├── Welcome Animations
└── Runtime Animations
    └── POLICE
```

---

# 22. Runtime animation

Diferenciar:

```text
WELCOME ANIMATION
```

de:

```text
MANUAL / RUNTIME ANIMATION
```

Police é:

```text
manual runtime animation
```

Não executar automaticamente no boot.

---

# 23. Comportamento do botão 3

Sugestão funcional:

```text
primeiro clique BUTTON 3
→ inicia POLICE

segundo clique BUTTON 3
→ para POLICE
→ restaura estado anterior
```

Implementar toggle lógico no ESP32.

---

# 24. Estado anterior

Antes de iniciar Police:

```text
snapshot current Desired State
```

Exemplo:

```text
antes = RED
BUTTON3
→ Police
BUTTON3 novamente
→ RED
```

Se antes:

```text
WHITE
```

então:

```text
Police stop
→ WHITE
```

---

# 25. Outro botão durante Police

Se Police estiver rodando e usuário pressionar:

```text
BUTTON1
```

resultado:

```text
cancel Police
↓
WHITE
```

Se pressionar:

```text
BUTTON2
```

resultado:

```text
cancel Police
↓
RED
```

Se pressionar:

```text
BUTTON4
```

resultado:

```text
cancel Police
↓
configured button4 action
```

A ação mais recente do usuário sempre vence.

---

# 26. PWA durante Police

Se o usuário selecionar uma cor pelo PWA durante Police:

```text
cancel Police
↓
apply new Desired State
```

Mesma regra das Welcome Animations.

---

# 27. Disconnect durante Police

Se LEFT ou RIGHT desconectar:

```text
cancel Police immediately
↓
GROUP DEGRADED
↓
normal Connection Manager recovery
```

Não continuar piscando somente um lado.

Depois do reconnect:

```text
restore pre-animation Desired State
↓
verify
↓
SYNCED
```

---

# 28. Botão 4 — configurável pelo PWA

BUTTON 4 representa:

```text
USER PRESET
```

O usuário pode escolher pelo PWA o que o botão executará.

---

# 29. Configurações permitidas para botão 4

Implementar inicialmente:

```text
Favorite Color
Specific RGB Color
White
Welcome Animation / selected animation
Police Animation
```

Porém default recomendado:

```text
Favorite Color
```

Se for mais simples e consistente com o sistema atual, começar com:

```text
Specific RGB / Favorite / Animation
```

desde que arquitetura seja extensível.

---

# 30. Configuração do botão 4

Persistir em NVS.

Estrutura conceitual:

```c
typedef enum {
    REMOTE_ACTION_FAVORITE,
    REMOTE_ACTION_RGB,
    REMOTE_ACTION_WHITE,
    REMOTE_ACTION_ANIMATION,
    REMOTE_ACTION_POLICE
} remote_action_type_t;
```

Com:

```c
typedef struct {
    remote_action_type_t type;

    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t brightness;

    animation_id_t animation_id;
} remote_button4_config_t;
```

---

# 31. Default botão 4

Em instalação nova:

```text
Button 4 = Favorite
```

Se já houver config NVS:

```text
preservar
```

---

# 32. PWA

Adicionar página/seção para:

```text
CONTROLE REMOTO
```

Pode ficar dentro:

```text
Diagnóstico
```

ou numa seção apropriada.

Não poluir Home.

---

# 33. Interface Remote Control

Exemplo:

```text
CONTROLE REMOTO

1   Branco padrão
    Fixo

2   Vermelho
    Fixo

3   Police
    Vermelho / Azul
    Fixo

4   Personalizado
    [ Favorita ▼ ]
```

---

# 34. Configuração botão 4

Ao tocar no botão 4:

```text
Ação do botão 4
```

Opções:

```text
Favorita
Cor personalizada
Animação
Branco
Police
```

---

# 35. Cor personalizada botão 4

Se:

```text
Cor personalizada
```

permitir selecionar:

```text
color
brightness
```

preferencialmente reutilizando componentes existentes.

Não criar segundo color picker completo desnecessariamente.

Pode permitir:

```text
Usar cor atual
```

---

# 36. Animation button 4

Se usuário escolher:

```text
Animation
```

listar animações disponíveis.

Exemplo:

```text
Red Welcome
OEM White
Red → White
Premium Pulse
Show Welcome
Police
```

---

# 37. Save

Ao salvar:

```text
PUT remote config
↓
NVS
↓
WebSocket update
```

---

# 38. API

Adicionar:

```text
GET /api/v1/remote
```

Resposta conceitual:

```json
{
  "connected": true,
  "last_button": 2,
  "button4": {
    "type": "favorite"
  }
}
```

---

# 39. Atualizar button4

Endpoint:

```text
PUT /api/v1/remote/button4
```

Exemplo:

```json
{
  "type": "rgb",
  "r": 128,
  "g": 0,
  "b": 255,
  "brightness": 64
}
```

---

# 40. WebSocket RF

Adicionar eventos:

```text
remote_button
remote_action_started
remote_action_completed
remote_config_updated
```

Não enviar spam enquanto botão permanece pressionado.

---

# 41. Last button

Diagnóstico pode mostrar:

```text
Último botão
2
```

e:

```text
Último evento
há 5 s
```

Somente para debug/diagnóstico.

---

# 42. LED indicador físico

GPIO:

```text
GPIO23
```

LED deve indicar:

```text
NON-STANDARD LIGHTING ACTIVE
```

---

# 43. Regra principal LED

LED físico:

```text
WHITE padrão
→ OFF
```

Qualquer estado diferente do padrão:

```text
RGB RED
RGB BLUE
RGB PURPLE
qualquer outra cor
qualquer animação runtime
Police
Welcome Animation
```

→

```text
LED ON
```

---

# 44. Fonte de verdade do LED

Não acender simplesmente quando o botão é pressionado.

Preferir:

```text
confirmed / effective system state
```

Sempre que possível.

Fluxo:

```text
RED requested
↓
LEFT + RIGHT verified RED
↓
LED ON
```

---

# 45. Durante transições

Durante:

```text
reconciling
```

não piscar o LED desnecessariamente.

Definir política previsível.

Sugestão:

```text
animation active
→ LED ON imediatamente

non-white desired state being applied
→ LED ON

WHITE desired state being applied
→ LED OFF somente quando WHITE estiver confirmado
```

Isso evita apagar o indicador antes de o retorno ao padrão realmente acontecer.

---

# 46. Offline / unknown

Se:

```text
LEFT/RIGHT states unknown
```

e nenhum efeito runtime estiver ativo:

usar política conservadora:

```text
LED OFF
```

Não indicar colorido confirmado se o sistema não sabe.

---

# 47. Police e LED

Enquanto:

```text
POLICE active
```

LED físico deve ficar:

```text
ON contínuo
```

Não piscar acompanhando vermelho/azul.

Objetivo do LED:

```text
indicar modo especial ativo
```

e não reproduzir a animação.

---

# 48. Welcome e LED

Durante qualquer Welcome Animation:

```text
LED ON
```

Ao terminar:

se estado final for:

```text
WHITE
```

→ OFF.

Se estado final for:

```text
RGB
```

→ permanece ON.

---

# 49. GPIO23 init

No boot:

```text
GPIO23 output
```

Estado inicial:

```text
LOW
```

Não permitir flash involuntário longo durante boot.

---

# 50. Indicator Manager

Não espalhar:

```text
gpio_set_level(GPIO23, ...)
```

pelo sistema.

Criar módulo:

```text
indicator/
├── indicator.c
└── indicator.h
```

ou equivalente.

Interface conceitual:

```c
indicator_update(system_state);
```

---

# 51. Remote commands e Desired State

BUTTON 1/2/4 devem utilizar exatamente a API interna que já recebe comandos do PWA.

Ideal:

```text
PWA
RF Remote
future headlight sensor
```

todos convergem para:

```text
Control Intent
↓
Desired State / Animation
```

---

# 52. Source metadata

Opcionalmente registrar origem:

```text
CONTROL_SOURCE_WEB
CONTROL_SOURCE_RF
CONTROL_SOURCE_STARTUP
```

para logs.

Não alterar sem necessidade o significado de Desired State.

---

# 53. Logs

Criar logs claros:

```text
RF: physical channel D2
RF: mapped to BUTTON 3
RF: action POLICE_START
```

E:

```text
INDICATOR: ON reason=POLICE
```

Evitar logs por frame de animação.

---

# 54. Testes SEM carro primeiro

REGRA CRÍTICA:

O objetivo é minimizar o tempo que os faróis reais ficam ligados.

A bateria do carro já foi descarregada anteriormente durante testes prolongados.

Portanto:

```text
hardware car tests MUST BE LAST
```

---

# 55. Ordem obrigatória de testes

Seguir:

```text
1. Host/unit tests
2. GPIO simulation
3. ESP32 + RX480E only
4. ESP32 + LED only
5. ESP32 + RX480E + LED
6. Wi-Fi/PWA
7. Firmware integration without physical SP624E dependency where possible
8. ONLY THEN brief real-car SP624E tests
```

---

# 56. Host tests

Criar testes para:

```text
D0-D3 mapping
button debounce
duplicate suppression
button release
button 1 action
button 2 action
button 3 toggle
button 4 config
NVS button4
Police start/stop
Police cancellation
user override
indicator policy
```

---

# 57. Fake Group Controller

Quando possível usar fake/mock:

```text
Fake Group Controller
```

para testar:

```text
BUTTON1
→ expected WHITE intent

BUTTON2
→ expected RED intent

BUTTON3
→ expected POLICE animation

BUTTON4
→ expected configured intent
```

Sem BLE real.

---

# 58. Police unit test

Testar timeline usando relógio simulado.

Não esperar segundos reais nos testes.

Validar:

```text
t=0 red
t=x blue
...
```

e cancel.

---

# 59. Queue stress simulation

Simular Police durante:

```text
30 seconds
```

com tempo virtual/acelerado.

Confirmar:

```text
no growing queue
no stale frame accumulation
coalescing works
```

Sem usar farol físico.

---

# 60. LED unit tests

Tabela:

```text
WHITE confirmed
→ OFF

RGB RED confirmed
→ ON

RGB BLUE confirmed
→ ON

Police active
→ ON

Welcome active
→ ON

Offline/unknown
→ OFF
```

---

# 61. Teste ESP32 + receptor sem faróis

Desabilitar ou não depender de conexões BLE para a primeira leitura RF.

Abrir monitor.

Pressionar:

```text
1
2
3
4
```

Registrar:

```text
physical GPIO/channel
logical button
```

Confirmar exatamente um evento por clique.

---

# 62. Descoberta de mapping

Produzir relatório:

```text
BUTTON 1 → D?
BUTTON 2 → D?
BUTTON 3 → D?
BUTTON 4 → D?
```

Depois configurar firmware.

---

# 63. Testar press-and-hold

Pressionar cada botão por:

```text
~2 s
```

Confirmar:

```text
ONE logical press
```

ou comportamento conhecido/documentado.

---

# 64. LED físico sem SP624E

Criar modo de teste controlado que altere:

```text
indicator ON
indicator OFF
```

por poucos segundos.

Exemplo:

```text
OFF 1s
ON 1s
OFF
```

Confirmar visualmente.

Não deixar LED em loop infinito.

---

# 65. Receiver + LED integration

Ainda sem carro/faróis:

```text
BUTTON1
→ intended WHITE
→ indicator predicted OFF

BUTTON2
→ intended RED
→ indicator predicted ON

BUTTON3
→ Police active
→ indicator ON

BUTTON3
→ Police stop / restore WHITE
→ indicator OFF
```

Usar mocks onde necessário.

---

# 66. Testes PWA

Testar:

```text
Remote configuration page
```

sem BLE real quando possível.

Salvar Button4.

Reboot ESP32.

Confirmar persistência.

---

# 67. Button4 persistence

Fluxo:

```text
BUTTON4 = PURPLE
↓
save
↓
reboot
↓
GET /api/v1/remote
```

deve retornar:

```text
PURPLE
```

---

# 68. Button4 physical input test sem carro

Depois do reboot:

```text
pressionar BUTTON4
```

logs devem mostrar:

```text
REMOTE_BUTTON_4
↓
RGB PURPLE intent
```

Sem necessariamente enviar aos SP624E.

---

# 69. Teste WebSocket

Com navegador aberto:

pressionar controle.

UI/diagnóstico deve refletir:

```text
Button 1
Button 2
Button 3
Button 4
```

sem reload.

---

# 70. Build

Executar:

```text
clean build
```

e:

```text
host tests
frontend build
firmware build
```

Todos PASS antes de qualquer teste no carro.

---

# 71. Flash antes do carro

Gravar versão final candidata no ESP32.

Executar receptor + PWA + LED na bancada.

Somente depois levar ao carro.

---

# 72. TESTES NO CARRO — REGRA DE ECONOMIA DE BATERIA

A bateria NÃO deve ser usada para testes longos.

O teste físico será:

```text
curto
controlado
por último
```

---

# 73. Não fazer long-run com faróis

Nesta tarefa:

```text
NÃO executar teste de 15 minutos com faróis ligados
NÃO executar 20 boots físicos
NÃO executar stress BLE longo no carro
NÃO deixar Police rodando por minutos
```

Essas áreas já possuem arquitetura de confiabilidade validada anteriormente.

---

# 74. Preparação para teste físico

Antes:

```text
laptop pronto
serial aberto
PWA pronto
controle pronto
checklist definido
```

Somente então alimentar/ligar os faróis.

Evitar perder minutos configurando ferramentas com faróis ligados.

---

# 75. Teste físico 1 — Button 1

Duração máxima aproximada:

```text
5–10 segundos
```

Pressionar:

```text
BUTTON 1
```

Confirmar:

```text
LEFT white
RIGHT white
GROUP SYNCED
LED indicator OFF
```

---

# 76. Teste físico 2 — Button 2

Pressionar:

```text
BUTTON 2
```

Confirmar rapidamente:

```text
LEFT RED
RIGHT RED
GROUP SYNCED
LED ON
```

Depois:

```text
BUTTON1
```

Confirmar:

```text
WHITE
LED OFF
```

---

# 77. Teste físico 3 — Police

Apenas uma execução curta.

```text
BUTTON3
```

Rodar por aproximadamente:

```text
2–4 segundos
```

Confirmar:

```text
RED/BLUE
LEFT + RIGHT visually synchronized
LED ON continuously
```

Depois:

```text
BUTTON3
```

ou:

```text
BUTTON1
```

para parar imediatamente.

Confirmar:

```text
WHITE
LED OFF
```

Não deixar a animação rodando além do necessário.

---

# 78. Teste físico 4 — Button4

Configurar no PWA antes do teste:

```text
BUTTON4 = PURPLE
```

Depois:

```text
BUTTON4
```

Confirmar:

```text
LEFT PURPLE
RIGHT PURPLE
LED ON
```

Imediatamente:

```text
BUTTON1
```

Confirmar retorno:

```text
WHITE
LED OFF
```

---

# 79. Teste físico total

Meta:

```text
tempo efetivo de luz colorida:
< 1 minuto total
```

idealmente muito menos.

Não repetir visual tests se:

```text
logs + primeira confirmação
```

já forem suficientes.

---

# 80. Em caso de qualquer comportamento errado

Se:

```text
LEFT != RIGHT
```

ou:

```text
queue backlog
disconnect
wrong button
LED incorrect
```

parar o teste físico.

Voltar para:

```text
bench/mock test
```

Não depurar durante vários minutos com faróis ligados.

---

# 81. Police safety behavior

Police deve possuir limite de segurança opcional.

Sugestão:

```text
auto timeout = 30 seconds
```

Depois:

```text
cancel Police
↓
restore previous Desired State
```

Isso evita deixar a animação ligada indefinidamente por acidente.

---

# 82. Configurabilidade do timeout

Pode ser constante interna nesta versão.

Não precisa PWA.

---

# 83. Boot

No boot:

```text
Police = OFF
```

Sempre.

Nunca persistir:

```text
Police currently active
```

---

# 84. Button state persistence

Persistir somente:

```text
Button4 configuration
```

Não:

```text
last pressed button
Police running
```

---

# 85. Button1 after reboot

Sempre continua:

```text
WHITE
```

---

# 86. Button2 after reboot

Sempre continua:

```text
RED
```

---

# 87. Button3 after reboot

Sempre continua:

```text
Police
```

---

# 88. Button4 after reboot

Carrega:

```text
NVS configuration
```

---

# 89. Welcome animation interaction

Se Welcome Animation estiver rodando e usuário pressionar qualquer botão:

```text
cancel Welcome
↓
execute remote action
```

Remote input representa intenção explícita do usuário e tem prioridade.

---

# 90. Future headlight sensor

Preparar arquitetura para futuro:

```text
ROAD MODE
```

onde sinal do farol original poderá forçar:

```text
WHITE
```

e bloquear efeitos coloridos.

Não implementar sensor nesta tarefa.

Mas evitar arquitetura que dificulte isso.

---

# 91. Futuro override de segurança

Idealmente a arquitetura permitirá:

```text
Headlight original ON
↓
ROAD MODE lock
↓
BUTTON2/BUTTON3/BUTTON4 color actions ignored
↓
BUTTON1 WHITE
```

Não implementar agora.

Documentar como próximo passo.

---

# 92. PWA visual state

Se botão físico alterar a cor:

```text
RF BUTTON2
↓
RED
```

PWA deve atualizar automaticamente via WebSocket.

Não exigir reload.

---

# 93. Dynamic headlights

Como o frontend já possui HeadlightVisual:

```text
BUTTON2
↓
hardware RED confirmed
↓
Observed State
↓
WebSocket
↓
LEFT visual RED
RIGHT visual RED
```

Police:

quando houver observed state/eventos suficientes:

```text
red ↔ blue
```

pode refletir na UI.

Não criar animação fake independente se o hardware não confirmar.

---

# 94. PWA LED indicator status

Opcionalmente mostrar:

```text
Modo especial ativo
```

mas não é obrigatório se não houver espaço.

Não adicionar clutter à Home.

---

# 95. Diagnostics

Adicionar:

```text
RF Receiver
READY

Last Button
2

Button4
Favorite

Indicator
ON
```

Pode ficar na tela de diagnóstico.

---

# 96. Documentation

Criar:

```text
docs/rf-remote.md
```

Documentar:

```text
pinout
RX480E
physical mapping
button actions
debounce
Button4 config
Police behavior
LED indicator
test procedure
battery-saving strategy
```

---

# 97. Pinout documentation

Registrar explicitamente:

```text
GPIO23 = LED indicator

GPIO25 = RX480E D0
GPIO26 = RX480E D1
GPIO27 = RX480E D2
GPIO32 = RX480E D3
GPIO33 = RX480E VT
```

E após descoberta:

```text
Button1 = Dx
Button2 = Dx
Button3 = Dx
Button4 = Dx
```

---

# 98. AGENTS.md

Adicionar invariantes:

```text
RF remote never writes BLE directly.

Button 1 is always WHITE.

Button 2 is always RED.

Button 3 is always POLICE runtime animation.

Button 4 is user configurable and persisted.

Any explicit user action cancels active temporary animation when required.

Police must never continue on only one SP624E.

Disconnect cancels Police.

Police is never restored after reboot.

Physical LED indicates non-standard lighting / animation active.

Physical LED is OFF for confirmed normal WHITE state.

Do not use long physical headlight tests unless explicitly required.
```

---

# 99. Critérios de aceite

Somente concluir quando:

- [ ] RX480E GPIOs configurados;
- [ ] VT lido;
- [ ] mapping físico dos quatro botões identificado;
- [ ] debounce funcionar;
- [ ] um clique gerar um evento;
- [ ] long press não gerar spam;
- [ ] Button1 = WHITE;
- [ ] Button2 = RED;
- [ ] Button3 = Police;
- [ ] Button4 configurável;
- [ ] Button4 persistir em NVS;
- [ ] Police integrar Animation Manager;
- [ ] Police ser sincronizado LEFT/RIGHT;
- [ ] Police poder ser cancelado;
- [ ] Police possuir timeout de segurança;
- [ ] outro botão cancelar Police corretamente;
- [ ] PWA command cancelar Police corretamente;
- [ ] disconnect cancelar Police;
- [ ] Welcome poder ser interrompido por RF;
- [ ] GPIO23 controlar LED;
- [ ] WHITE → LED OFF;
- [ ] RGB → LED ON;
- [ ] animation → LED ON;
- [ ] Police → LED ON contínuo;
- [ ] PWA refletir comandos RF;
- [ ] diagnostics mostrar estado RF;
- [ ] testes host PASS;
- [ ] testes mock PASS;
- [ ] teste RX480E bancada PASS;
- [ ] teste LED bancada PASS;
- [ ] PWA PASS;
- [ ] firmware build PASS;
- [ ] frontend build PASS;
- [ ] teste físico real ser executado por último;
- [ ] teste físico ser curto;
- [ ] Button1 hardware PASS;
- [ ] Button2 hardware PASS;
- [ ] Button3 hardware PASS;
- [ ] Button4 hardware PASS;
- [ ] LEFT/RIGHT continuar Strict Sync;
- [ ] nenhum panic;
- [ ] nenhum watchdog;
- [ ] nenhum reboot inesperado;
- [ ] documentação atualizada.

---

# 100. Relatório final

Entregar:

```text
SP624E RF REMOTE + INDICATOR REPORT
===================================

Firmware
--------
Version:
Build:
Flash:

RX480E
------
GPIO25 / D0:
GPIO26 / D1:
GPIO27 / D2:
GPIO32 / D3:
GPIO33 / VT:

Physical Mapping
----------------
Button 1:
Button 2:
Button 3:
Button 4:

Input Tests
-----------
Button1 single press:
Button2 single press:
Button3 single press:
Button4 single press:

Long press:
Duplicate events:
Debounce:

Button 1
--------
Action:
Expected: WHITE
Mock test:
Hardware test:
Final group:
Indicator:

Button 2
--------
Action:
Expected: RED
Brightness:
Mock test:
Hardware test:
Final group:
Indicator:

Button 3 / Police
-----------------
Pattern:
Frame timing:
Queue behavior:
Toggle:
Cancel:
Timeout:
Mock duration tested:
Physical test duration:
LEFT/RIGHT sync:
Indicator:
Restore:

Button 4
--------
Default:
Saved config:
NVS:
Reboot restore:
Configured test:
Hardware result:
Indicator:

LED Indicator
-------------
GPIO:
Resistor:
Boot state:
WHITE:
RED:
BLUE:
PURPLE:
Welcome:
Police:
Offline:

PWA
---
Remote settings:
Button4 editor:
Save:
Realtime RF updates:
Diagnostics:
WebSocket:

Mock / Host Tests
-----------------
Protocol:
Remote mapping:
Debounce:
Duplicate suppression:
Police:
Cancel:
Indicator:
NVS:
PASS/FAIL:

Physical Test
-------------
Performed last:
Total approximate headlight-on time:
White:
Red:
Police:
Button4:
Returned to white:
Battery-saving procedure followed:

BLE Regression
--------------
LEFT:
RIGHT:
Strict Sync:
Unexpected disconnect:
Command failures:

Safety
------
Police timeout:
Police after reboot:
Single-side Police prevented:
LED correct:
Panic:
Watchdog:
Unexpected reboot:

Conclusion
----------

Open Issues
-----------

Recommended Next Step
---------------------
Headlight ON/OFF sensing + automatic ROAD / AMBIENT mode.
```

---

# 101. Regra final de segurança e teste

A prioridade desta tarefa é:

```text
1. Testar lógica sem carro
2. Testar RX480E na bancada
3. Testar LED na bancada
4. Testar PWA
5. Build e flash
6. SOMENTE ENTÃO ligar os faróis reais
```

Não utilizar os faróis do carro como ambiente de desenvolvimento.

O teste físico deve servir apenas para confirmar:

```text
1 → WHITE
2 → RED
3 → RED/BLUE POLICE
4 → USER CONFIG

LED:
WHITE → OFF
SPECIAL → ON
```

e terminar imediatamente após confirmação.

Não executar stress test longo alimentado pela bateria do veículo.
