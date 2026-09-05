# GOAL — Welcome Animations configuráveis + persistência + controle pela interface

> CANCELADO em 2026-08-14 por decisão do usuário. A funcionalidade foi removida
> do firmware, da API e do PWA na versão 0.7.6 porque a inicialização BLE fazia a
> animação começar vários segundos depois de os faróis serem ligados.

## 1. Objetivo

Evoluir o projeto atual adicionando um sistema completo de **animações de boas-vindas dos faróis**, executadas automaticamente quando o carro/ESP32 iniciar.

A funcionalidade deverá permitir pela interface web:

- ativar ou desativar animação de boas-vindas;
- escolher entre múltiplas animações;
- visualizar uma animação antes de selecionar;
- salvar a animação escolhida;
- carregar automaticamente essa escolha nos próximos boots;
- executar a animação somente quando LEFT e RIGHT estiverem disponíveis;
- manter os dois SP624E visualmente sincronizados;
- ao finalizar, retornar para o estado normal desejado;
- nunca executar a animação somente em um lado;
- nunca repetir a animação apenas porque um controlador BLE reconectou.

A opção recomendada/default será:

```text
RED WELCOME
```

Uma animação elegante de aproximadamente:

```text
1,5–2 segundos
```

com aparência automotiva premium e sem efeito RGB exagerado.

---

# 2. Contexto

Projeto existente:

```text
C:\Projetos\ESP32
```

Continuar sobre a implementação atual.

Não recriar o projeto.

A arquitetura já possui:

```text
ESP32
├── BLE
│   ├── LEFT SP624E
│   └── RIGHT SP624E
│
├── Connection Manager
├── Command Queues
├── Desired State
├── State Reconciler
├── Group Controller
├── Strict Sync
├── NVS
│
├── Wi-Fi
├── HTTP API
├── WebSocket
└── React / TypeScript Web UI
```

As animações devem utilizar essa arquitetura.

---

# 3. Princípio fundamental

PROIBIDO implementar animação assim:

```text
animation task
→ write diretamente LEFT
→ write diretamente RIGHT
```

Obrigatório utilizar uma abstração de grupo.

Fluxo:

```text
Welcome Animation Manager
        ↓
Animation Frame
        ↓
Group Controller
        ↓
LEFT Queue + RIGHT Queue
        ↓
SP624E LEFT + RIGHT
```

Nenhuma animação pode quebrar:

```text
Strict Sync
generation handling
stale command protection
reconnection logic
state verification
```

---

# 4. Separar Animation State de Desired State

Uma animação é temporária.

Ela NÃO representa necessariamente o estado normal que deve permanecer nos faróis.

Criar conceitos separados:

```text
Desired State
```

e:

```text
Animation State
```

Exemplo:

```text
Desired State:
WHITE 100%

Welcome Animation:
RED → WHITE
```

Durante a animação:

```text
Animation State controla temporariamente
```

Ao terminar:

```text
voltar para Desired State
```

Não persistir cada frame da animação como Desired State.

---

# 5. Welcome Animation Manager

Criar módulo específico.

Estrutura conceitual:

```text
main/
└── animation/
    ├── welcome_animation.c
    ├── welcome_animation.h
    ├── animation_player.c
    ├── animation_player.h
    ├── animation_presets.c
    ├── animation_presets.h
    ├── animation_config.c
    └── animation_config.h
```

Responsabilidades:

```text
Animation Config
↓
Animation Preset
↓
Animation Player
↓
Group Controller
```

---

# 6. Configuração persistida

Criar estrutura semelhante a:

```c
typedef struct {
    uint32_t version;

    bool enabled;

    welcome_animation_id_t animation_id;

} welcome_animation_config_t;
```

Persistir em NVS.

Namespace sugerido:

```text
welcome
```

Dados:

```text
version
enabled
animation_id
```

---

# 7. Opções iniciais

Implementar pelo menos estas opções:

```text
DISABLED
RED_WELCOME
OEM_WHITE_FADE
RED_TO_WHITE
PREMIUM_PULSE
SHOW_WELCOME
```

---

# 8. Opção 1 — Disabled

Nome na interface:

```text
Desativada
```

Comportamento:

```text
ESP32 boot
↓
LEFT + RIGHT READY
↓
nenhuma animação
↓
funcionamento normal
```

Nenhum frame deve ser enviado.

---

# 9. Opção 2 — Red Welcome

Nome:

```text
Red Welcome
```

Descrição:

```text
Entrada suave em vermelho,
pequeno destaque e transição elegante
para o estado normal.
```

Duração alvo:

```text
~1,5–2,0 segundos
```

É a opção recomendada.

Sequência conceitual:

```text
0 ms
vermelho 0%

↓ fade

300 ms
vermelho ~25%

↓ fade

600 ms
vermelho ~60%

↓ fade

850 ms
vermelho ~100%

↓ pequena pausa

1050 ms
vermelho ~100%

↓ fade rápido

1300 ms
vermelho ~35%

↓ transição final

1600–1900 ms
Desired State normal
```

Evitar:

```text
strobo
flash agressivo
pisca rápido
```

Objetivo:

```text
premium / OEM+
```

---

# 10. Red Welcome e branco final

Não assumir que o estado final sempre é branco.

O final da animação deve ser:

```text
Desired State existente
```

Exemplo:

Se Desired State for:

```text
WHITE
```

terminar em WHITE.

Se for:

```text
RGB vermelho
```

terminar em vermelho.

Se for:

```text
RGB roxo
```

terminar em roxo.

A animação não deve arbitrariamente mudar a configuração permanente.

---

# 11. Opção 3 — OEM White Fade

Nome:

```text
OEM White
```

Descrição:

```text
Acendimento progressivo e suave
do branco, semelhante a uma
assinatura luminosa original.
```

Sequência:

```text
white 0%
↓
white 10%
↓
white 25%
↓
white 45%
↓
white 70%
↓
white 100%
```

Duração:

```text
~1,2–1,8 s
```

Depois:

```text
Desired State
```

Caso o Desired State já seja WHITE, terminar sem transição perceptível.

---

# 12. Branco real

Quando utilizar WHITE:

usar o canal branco real do SP624E.

Não utilizar:

```text
RGB 255,255,255
```

como substituto silencioso.

Somente habilitar animações que dependem de WHITE se:

```text
white capability == CONFIRMED
```

Caso não esteja disponível:

```text
OEM_WHITE_FADE
RED_TO_WHITE
```

devem aparecer desabilitadas na UI.

---

# 13. Opção 4 — Red to White

Nome:

```text
Red → White
```

Descrição:

```text
Fade vermelho curto seguido por
uma entrada limpa no branco.
```

Sequência aproximada:

```text
RED 0 → 70%
~500 ms

RED 70 → 100%
~250 ms

RED → WHITE
~400 ms

WHITE 60 → 100%
~400 ms
```

Duração total:

```text
~1,5–2 s
```

Depois:

```text
Desired State
```

---

# 14. Opção 5 — Premium Pulse

Nome:

```text
Premium Pulse
```

Descrição:

```text
Um pulso suave de intensidade,
sem alternância exagerada de cores.
```

Exemplo:

```text
vermelho 0 → 80%
↓
80 → 35%
↓
35 → 100%
↓
Desired State
```

Duração:

```text
~1,5 s
```

Não utilizar pisca instantâneo.

Tudo deve ser baseado em fade.

---

# 15. Opção 6 — Show Welcome

Nome:

```text
Show Welcome
```

Descrição:

```text
Animação mais chamativa,
mas ainda curta.
```

Exemplo:

```text
RED
↓
PURPLE
↓
BLUE
↓
WHITE ou Desired State
```

Duração:

```text
~2,5–3 s
```

Usar fades.

Não utilizar strobo.

Essa opção não será a default.

---

# 16. Frames

Definir estrutura semelhante:

```c
typedef struct {
    desired_light_mode_t mode;

    uint8_t r;
    uint8_t g;
    uint8_t b;

    uint8_t brightness;
    uint8_t white;

    uint32_t duration_ms;

    animation_easing_t easing;
} animation_keyframe_t;
```

---

# 17. Keyframes em vez de frames fixos

Não armazenar centenas de frames manualmente.

Utilizar:

```text
keyframes
+
interpolação
```

Exemplo:

```text
RED 0%
duration 0

RED 100%
duration 600ms
```

O Animation Player gera frames intermediários.

---

# 18. Interpolação

Implementar inicialmente:

```text
LINEAR
EASE_IN_OUT
EASE_OUT
```

Para as animações premium, preferir:

```text
EASE_IN_OUT
```

ou:

```text
EASE_OUT
```

em vez de mudanças lineares quando visualmente ficar melhor.

---

# 19. Frame rate

Não enviar comandos BLE excessivamente.

Meta inicial:

```text
20 FPS
```

aproximadamente:

```text
1 frame / 50 ms
```

Permitir ajuste interno entre:

```text
15–25 FPS
```

caso os testes mostrem comportamento melhor.

Nunca:

```text
100+ FPS
```

---

# 20. Coalescing

A arquitetura existente já possui:

```text
generation
stale commands
coalescing
```

Utilizar isso.

Se frames ficarem acumulados:

```text
não tentar reproduzir frames antigos atrasados
```

Preferir avançar para o estado atual da animação.

---

# 21. Generation de animação

A animação deve possuir sua própria identificação:

```text
animation_generation
```

ou mecanismo equivalente.

Objetivo:

```text
animação A iniciou
↓
usuário pediu animação B / cancelou
↓
frames antigos de A são descartados
```

---

# 22. Cancelamento

Animation Player deve suportar:

```text
CANCEL
```

Casos:

```text
usuário toca Stop
configuração muda
um controlador cai
sistema entra ERROR
novo preview é iniciado
```

---

# 23. LEFT/RIGHT obrigatório

A animação só pode iniciar automaticamente se:

```text
LEFT == READY
AND
RIGHT == READY
```

Nunca:

```text
LEFT anima
RIGHT offline
```

---

# 24. Timeout no startup

No boot:

```text
ESP32 inicia
↓
Connection Manager inicia
↓
aguarda ambos READY
```

Definir timeout configurável.

Sugestão:

```text
20 segundos
```

Quando ambos concluírem State Query:

```text
welcome animation
```

Enquanto permanecerem desligados:

```text
keep welcome animation armed
```

Ao conectar, executar antes da reconciliação padrão.

Não bloquear outras tarefas enquanto aguarda os controladores.

---

# 25. Executar uma vez por ciclo de alimentação dos faróis

Criar flag runtime:

```text
welcome_animation_attempted
```

Depois que a animação executar, somente rearmar quando os dois controladores
desconectarem dentro da janela de confirmação de power cycle.

Depois que:

```text
animação executou
```

ou:

```text
animação foi cancelada por timeout
```

não tentar novamente até o próximo ciclo confirmado dos dois faróis.

---

# 26. Reconexão isolada NÃO dispara animação

Exemplo:

```text
carro já ligado
↓
RIGHT perde BLE
↓
RIGHT reconecta
```

Resultado:

```text
NÃO executar Welcome Animation
```

Somente:

```text
reconcile Desired State
```

Essa regra é obrigatória.

---

# 27. Power-cycle dos dois controladores

Mesmo se ambos os SP624E perderem energia momentaneamente durante o mesmo boot do ESP32:

```text
não repetir automaticamente welcome
```

A animação pertence ao:

```text
boot/session do ESP32
```

e não a todo evento BLE.

---

# 28. Preview

Na interface adicionar:

```text
Testar animação
```

Fluxo:

```text
usuário seleciona uma animação
↓
Testar
↓
snapshot Desired State
↓
executa animação
↓
retorna ao Desired State
```

Preview NÃO precisa salvar a opção.

---

# 29. Stop Preview

Durante preview mostrar:

```text
Parar
```

Se usuário parar:

```text
cancel animation
↓
restore Desired State
↓
verify
```

---

# 30. Preview somente com grupo pronto

Se:

```text
GROUP != SYNCED
```

o botão:

```text
Testar animação
```

deve ficar desabilitado.

Texto:

```text
Aguardando os dois faróis
```

ou equivalente.

---

# 31. Interface

Adicionar seção:

```text
Animação de boas-vindas
```

Layout sugerido:

```text
┌──────────────────────────────┐
│ Animação de boas-vindas      │
│                              │
│ [✓] Ativada                  │
│                              │
│ Estilo                       │
│                              │
│ ● Red Welcome                │
│   Suave e esportiva          │
│                              │
│ ○ OEM White                  │
│   Fade branco elegante       │
│                              │
│ ○ Red → White                │
│   Vermelho seguido de branco │
│                              │
│ ○ Premium Pulse              │
│   Pulso suave                │
│                              │
│ ○ Show Welcome               │
│   Mais chamativa             │
│                              │
│ [ Testar animação ]          │
│                              │
│          [ Salvar ]          │
└──────────────────────────────┘
```

Pode adaptar o design.

---

# 32. Toggle

Adicionar toggle:

```text
Animação de boas-vindas
ON / OFF
```

Quando OFF:

```text
enabled=false
```

Persistir.

No próximo boot:

```text
não executar nenhuma animação
```

---

# 33. Seleção

Cards devem ser touch-friendly.

Cada opção mostrar:

```text
nome
descrição curta
duração aproximada
```

Exemplo:

```text
Red Welcome
~1,8 s

Entrada vermelha suave,
com acabamento premium.
```

---

# 34. Indicador Recommended

Marcar:

```text
Red Welcome
RECOMENDADA
```

de forma discreta.

---

# 35. Configuração default

Para instalação limpa:

```text
enabled = true
animation = RED_WELCOME
```

Porém, se já existir configuração válida na NVS:

```text
NÃO sobrescrever.
```

---

# 36. API

Criar:

```text
GET /api/v1/welcome-animation
```

Resposta:

```json
{
  "enabled": true,
  "selected": "red_welcome",
  "available": [
    {
      "id": "red_welcome",
      "name": "Red Welcome",
      "duration_ms": 1800,
      "supported": true
    }
  ]
}
```

---

# 37. Atualizar configuração

Endpoint:

```text
PUT /api/v1/welcome-animation
```

Payload:

```json
{
  "enabled": true,
  "selected": "red_welcome"
}
```

Validar ID.

Persistir somente depois de payload válido.

---

# 38. Preview API

Endpoint:

```text
POST /api/v1/welcome-animation/preview
```

Exemplo:

```json
{
  "animation": "red_welcome"
}
```

Retorno:

```json
{
  "accepted": true
}
```

---

# 39. Cancel preview API

Endpoint:

```text
POST /api/v1/welcome-animation/stop
```

Deve:

```text
cancelar
↓
restore Desired State
↓
verify
```

---

# 40. WebSocket

Adicionar eventos:

```text
welcome_animation_started
welcome_animation_progress
welcome_animation_completed
welcome_animation_cancelled
welcome_animation_failed
welcome_animation_config_updated
```

---

# 41. Progress

Não enviar WebSocket em todos os frames.

Atualizações de progresso podem ser:

```text
0%
25%
50%
75%
100%
```

ou somente:

```text
START
END
```

Evitar tráfego inútil.

---

# 42. Startup sequence

Fluxo final:

```text
ESP32 BOOT
↓
NVS
↓
load mapping
↓
load Desired State
↓
load Welcome Animation Config
↓
BLE
↓
connect LEFT
↓
connect RIGHT
↓
State Query
↓
both READY
↓
determine initial state
↓
welcome enabled?
        │
   ┌────┴────┐
   NO       YES
   │          │
normal     play welcome
              │
              ↓
         restore Desired State
              │
              ↓
           verify
              │
              ↓
            SYNCED
```

---

# 43. Momento de captura do Desired State

Antes de iniciar a animação:

```text
capture startup desired state
```

Não permitir que a sequência destrua esse estado.

---

# 44. Alteração durante animação

Se um comando real do usuário chegar pela interface DURANTE a Welcome Animation:

Exemplo:

```text
welcome rodando
↓
usuário seleciona BLUE
```

Comportamento esperado:

```text
cancelar welcome
↓
novo Desired State = BLUE
↓
reconcile
↓
BLUE nos dois
```

A preferência do usuário tem prioridade sobre a animação.

---

# 45. Receptor 433 futuro

Preparar arquitetura para que futuramente:

```text
controle 433
```

também possa cancelar animação ao receber uma ação do usuário.

Não implementar RF nesta tarefa.

---

# 46. Disconnect durante animação

Se LEFT ou RIGHT desconectar:

```text
animation cancel
↓
GROUP DEGRADED
↓
normal Connection Manager recovery
```

Quando voltar:

```text
reconcile Desired State
```

NÃO retomar a animação do ponto interrompido.

NÃO reiniciar Welcome Animation.

---

# 47. Final da animação

Depois do último frame:

```text
restore Desired State
↓
STATE QUERY LEFT
↓
STATE QUERY RIGHT
↓
verify
↓
GROUP SYNCED
```

Somente então:

```text
welcome_animation_completed
```

---

# 48. Falha de restauração

Caso:

```text
animation terminou
```

mas um lado não voltou ao Desired State:

```text
não declarar completed
```

Usar:

```text
welcome_animation_failed
```

e deixar State Reconciler executar recuperação normal.

---

# 49. Não persistir frames

PROIBIDO:

```text
cada frame → NVS
```

Animation Frames vivem apenas na RAM/flash como configuração estática.

NVS contém apenas:

```text
enabled
animation_id
version
```

---

# 50. Custom animation futura

Estruturar enum/configuração para permitir futuramente:

```text
CUSTOM
```

mas NÃO implementar editor de animação nesta tarefa.

---

# 51. UI e animação visual

Durante Preview:

mostrar na interface algo semelhante:

```text
Reproduzindo Red Welcome...
████████████░░░░
```

Opcional.

Não bloquear navegação da página inteira.

---

# 52. Disable UI

Se o usuário desligar o toggle:

```text
Animação desativada
```

mostrar:

```text
Os faróis iniciarão diretamente
no estado normal.
```

---

# 53. Testes unitários

Criar testes para:

```text
animation config parsing
animation ID validation
NVS serialization
NVS restore
keyframe interpolation
easing
frame timing
cancel
user override
disconnect cancel
one-run-per-boot
```

---

# 54. Interpolation tests

Validar matematicamente:

```text
start RGB
end RGB
progress 0
progress 0.5
progress 1
```

Bounds:

```text
0–255
```

Sem overflow.

---

# 55. Runtime timing

Não depender de:

```text
vTaskDelay fixo acumulado
```

como relógio absoluto da animação.

Utilizar tempo monotônico.

Se um frame atrasar:

```text
calcular estado correto para o tempo atual
```

em vez de atrasar toda a animação indefinidamente.

---

# 56. Não bloquear Connection Manager

Animation Player deve rodar de forma assíncrona.

Não bloquear:

```text
BLE callbacks
Wi-Fi
HTTP
WebSocket
Connection Manager
Health Checks
```

durante 2 segundos.

---

# 57. Watchdog

Nenhuma sequência deve usar busy-loop.

Sempre cooperar com FreeRTOS.

---

# 58. Teste Red Welcome

Obrigatório no hardware.

Fluxo:

```text
GROUP SYNCED
↓
snapshot
↓
preview RED_WELCOME
↓
LEFT + RIGHT sincronizados
↓
fim
↓
restore
↓
verify
↓
SYNCED
```

Confirmar visualmente.

---

# 59. Qualidade visual

Durante teste real, avaliar:

```text
suavidade
sincronização
duração
mudanças abruptas
flicker
command backlog
```

Ajustar keyframes se necessário.

A versão final da `RED_WELCOME` deve priorizar:

```text
elegância
suavidade
curta duração
```

e não espetáculo RGB.

---

# 60. Teste OEM White

Se WHITE estiver disponível:

executar.

Confirmar:

```text
fade realmente suave
```

Sem pulos perceptíveis de intensidade.

---

# 61. Teste Red → White

Executar:

```text
RGB RED
↓
WHITE real
↓
restore Desired State
```

Verificar transição entre:

```text
RGB effect 0x63
```

e:

```text
WHITE effect 0xCC
```

---

# 62. Teste Premium Pulse

Confirmar que não há:

```text
flash agressivo
flicker
```

Apenas pulso suave.

---

# 63. Teste Show Welcome

Confirmar:

```text
RED
PURPLE
BLUE
Desired State
```

e que ambos acompanham juntos.

---

# 64. Startup test

Depois de salvar:

```text
Red Welcome
```

reiniciar fisicamente o ESP32.

Confirmar:

```text
boot
↓
connect both
↓
READY
↓
Red Welcome executa UMA vez
↓
Desired State
↓
SYNCED
```

---

# 65. Persistência

Depois:

```text
selecionar Premium Pulse
↓
Salvar
↓
reboot
```

Confirmar:

```text
Premium Pulse executa
```

e não Red Welcome.

---

# 66. Disable test

Na UI:

```text
OFF
↓
Salvar
↓
reboot
```

Resultado:

```text
nenhuma animação
```

---

# 67. Re-enable test

```text
ON
↓
Red Welcome
↓
Salvar
↓
reboot
```

Resultado:

```text
Red Welcome
```

---

# 68. Reconexão test

Depois de Welcome já ter executado:

```text
software disconnect RIGHT
```

Resultado esperado:

```text
RIGHT reconnect
↓
resync
```

Mas:

```text
Welcome Animation NÃO executa novamente
```

---

# 69. Command-during-animation test

Durante preview:

```text
selecionar outra cor na UI
```

Resultado:

```text
preview cancelado
↓
nova cor vira Desired State
↓
LEFT + RIGHT recebem
↓
SYNCED
```

---

# 70. Disconnect-during-animation test

Durante preview:

```text
disconnect RIGHT
```

Resultado:

```text
animação cancela
↓
LEFT para de seguir animation
↓
normal recovery
↓
RIGHT reconnect
↓
Desired State nos dois
↓
SYNCED
```

---

# 71. Teste de 20 boots

Realizar teste automatizado ou semiautomatizado quando razoável:

```text
20 startup cycles
```

Objetivo:

```text
animation exactly once per boot
```

Registrar:

```text
run
skip
fail
restore
```

Se power-cycle físico repetido não for prático:

utilizar reinícios de firmware como validação principal e documentar a limitação.

---

# 72. Teste Wi-Fi durante animação

Com iPhone conectado:

```text
boot
↓
Wi-Fi disponível
↓
welcome roda
```

A UI não pode travar.

WebSocket deve refletir:

```text
WELCOME_RUNNING
```

e depois:

```text
SYNCED
```

---

# 73. Performance BLE

Medir:

```text
frames gerados
frames efetivamente enviados
frames coalescidos
máximo queue depth
```

Uma animação não pode criar backlog crescente.

---

# 74. Frame dropping

Se necessário:

```text
drop intermediate frame
```

é melhor que:

```text
executar animação atrasada por vários segundos
```

A animação deve acompanhar tempo real.

---

# 75. Limite de duração

Nenhuma animação default deve exceder:

```text
3 segundos
```

Objetivo:

```text
carro liga → assinatura visual curta → operação normal
```

---

# 76. Documentação

Criar:

```text
docs/welcome-animations.md
```

Documentar:

```text
architecture
animation lifecycle
presets
NVS
startup behavior
cancel rules
disconnect behavior
user override
timings reais
```

---

# 77. README

Adicionar:

```text
Welcome Animations
```

com instrução para selecionar e testar pela UI.

---

# 78. AGENTS.md

Adicionar regras:

```text
Welcome Animation is temporary.

Never persist animation frames as Desired State.

Never run an animation on only one SP624E.

Never replay welcome because of BLE reconnect.

User commands always override an active animation.

Disconnect cancels animation.

Animation completion requires final state verification.

One automatic welcome attempt per boot.
```

---

# 79. Firmware version

Incrementar versão para a próxima versão apropriada após o Web Controller.

Se o firmware anterior for:

```text
0.5.0
```

usar:

```text
0.6.0
```

---

# 80. API status

Adicionar no snapshot:

```json
{
  "welcome_animation": {
    "enabled": true,
    "selected": "red_welcome",
    "state": "idle"
  }
}
```

Estados possíveis:

```text
idle
waiting
running
cancelling
completed
failed
disabled
```

---

# 81. UI status

Durante startup:

```text
● LEFT conectado
● RIGHT conectado

Boas-vindas
Red Welcome
Reproduzindo...
```

Depois:

```text
✓ Sincronizado
```

---

# 82. Critérios de aceite

Somente concluir quando:

- [ ] Welcome Animation Manager existir;
- [ ] animação for separada de Desired State;
- [ ] configuração persistir em NVS;
- [ ] toggle ON/OFF funcionar;
- [ ] Red Welcome existir;
- [ ] OEM White existir quando suportado;
- [ ] Red → White existir quando suportado;
- [ ] Premium Pulse existir;
- [ ] Show Welcome existir;
- [ ] Red Welcome for marcada como recomendada;
- [ ] preview funcionar;
- [ ] preview não persistir seleção automaticamente;
- [ ] Stop funcionar;
- [ ] Save funcionar;
- [ ] reboot carregar seleção;
- [ ] animação rodar automaticamente uma vez por ciclo de alimentação dos dois faróis;
- [ ] animação esperar LEFT + RIGHT;
- [ ] espera não bloquear o sistema enquanto os faróis estiverem desligados;
- [ ] reconnect BLE isolado não repetir Welcome;
- [ ] user command cancelar animação;
- [ ] disconnect cancelar animação;
- [ ] Desired State ser restaurado depois da animação;
- [ ] restore ser verificado;
- [ ] frames não serem persistidos em NVS;
- [ ] nenhuma fila crescer indefinidamente;
- [ ] não houver panic;
- [ ] não houver watchdog;
- [ ] não houver reboot inesperado;
- [ ] LEFT e RIGHT permanecerem sincronizados visualmente;
- [ ] interface funcionar no iPhone;
- [ ] documentação estiver atualizada.

---

# 83. Relatório final

Entregar:

```text
SP624E WELCOME ANIMATION REPORT
===============================

Firmware
--------
Version:
Build:
Flash:

Configuration
-------------
Enabled:
Selected:
NVS:
Persisted after reboot:

Available Animations
--------------------
Disabled:
Red Welcome:
OEM White:
Red → White:
Premium Pulse:
Show Welcome:

Red Welcome
-----------
Duration:
Generated frames:
Sent frames:
Dropped/coalesced frames:
Max queue depth:
LEFT/RIGHT sync:
Visual result:
Restore:
Final verification:

OEM White
---------
Supported:
Result:
Duration:
Restore:

Red → White
-----------
Supported:
Result:
Duration:
Restore:

Premium Pulse
-------------
Result:
Duration:
Restore:

Show Welcome
------------
Result:
Duration:
Restore:

Startup Test
------------
Both READY before start:
Executed once:
Execution delay after READY:
Animation:
Final Desired State:
GROUP final:

Persistence Test
----------------
Selected before reboot:
Loaded after reboot:
Executed correct animation:

Disabled Test
-------------
Saved disabled:
Reboot:
Animation executed:
Expected: NO

Reconnect Test
--------------
Welcome already completed:
Side disconnected:
Reconnect:
Welcome replayed:
Expected: NO

User Override Test
------------------
Animation running:
User command:
Animation cancelled:
New Desired State applied:
Final GROUP:

Disconnect During Animation
---------------------------
Disconnected side:
Animation cancelled:
Reconnect:
Desired State restored:
Final GROUP:

Web UI
------
Selector:
Toggle:
Preview:
Stop:
Save:
Realtime status:
iPhone:

BLE
---
Disconnects:
Unexpected desync:
Command failures:

Memory
------
Initial heap:
Final heap:
Minimum heap:

Safety
------
Panic:
Watchdog:
Unexpected reboot:
Incorrect persistent state:

Conclusion
----------

Open Issues
-----------

Recommended Next Step
---------------------
```

---

# REGRA FINAL

A animação de boas-vindas é um efeito temporário e secundário.

A prioridade continua sendo:

```text
conectividade
↓
sincronização
↓
Desired State correto
↓
segurança
↓
animação
```

Se houver qualquer conflito entre:

```text
animação bonita
```

e:

```text
manter LEFT e RIGHT corretamente sincronizados
```

a confiabilidade sempre vence.

O objetivo final é:

```text
Carro liga
↓
ESP32 inicia
↓
LEFT + RIGHT conectam
↓
Red Welcome suave
↓
~1,8 segundos
↓
estado normal
↓
SYNCED
```

com possibilidade de selecionar outra animação ou desativar completamente o recurso pela interface.
