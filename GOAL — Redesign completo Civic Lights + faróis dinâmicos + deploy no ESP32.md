# GOAL — Redesign completo Civic Lights + faróis dinâmicos + deploy no ESP32

> Atualização 2026-08-14: a rota `/animations` e toda a funcionalidade Welcome
> foram removidas por decisão do usuário na versão 0.7.6.

## 1. Objetivo

Redesenhar completamente o frontend web do projeto **Civic Lights**, mantendo todas as funcionalidades existentes e alterando prioritariamente apresentação, navegação, UX e componentes visuais.

O novo frontend deve seguir o último conceito visual aprovado:

```text
HOME
├── status LEFT
├── status RIGHT
├── status do conjunto
├── acesso rápido
│   ├── Branco real
│   ├── Favorita
│   └── Salvar favorita
│
└── navegação
    ├── Cor e Brilho
    ├── Animações
    └── Diagnóstico
```

As funcionalidades mais avançadas ficam em páginas internas.

A HOME deve caber integralmente em um **iPhone 16 Pro**, sem scroll vertical.

Além disso, criar uma representação dinâmica dos faróis LEFT/RIGHT cuja iluminação acompanhe:

- cor real observada;
- modo RGB;
- modo WHITE;
- brilho;
- desconexão;
- reconexão;
- estado ainda não conhecido.

Ao final:

```text
frontend
↓
build Vite
↓
assets web
↓
filesystem/partição web
↓
flash no ESP32
↓
teste real no iPhone
```

---

# 2. Projeto existente

Projeto:

```text
C:\Projetos\ESP32
```

Não recriar o projeto.

Preservar:

- ESP-IDF;
- BLE;
- NimBLE;
- LEFT/RIGHT mapping;
- Connection Manager;
- Reconnection Manager;
- Desired State;
- Observed State;
- State Reconciler;
- Command Queues;
- Strict Sync;
- Wi-Fi SoftAP;
- HTTP API;
- WebSocket;
- presets;
- white real;
- favorite;
- Welcome Animations;
- NVS;
- scripts existentes.

Esta tarefa NÃO deve reescrever a arquitetura BLE.

---

# 3. Regra principal

Este é um:

```text
FRONTEND REDESIGN
```

e não uma reimplementação do firmware.

Não alterar comportamento de controle já validado apenas para acomodar o layout.

Se faltar alguma informação necessária ao frontend, por exemplo:

```text
Observed RGB LEFT
Observed RGB RIGHT
Observed brightness
Observed mode
```

é permitido adicionar **somente exposição read-only** dessa informação na API/WebSocket.

Não alterar a lógica BLE para isso.

---

# 4. Referência visual

Seguir o último layout aprovado da conversa:

```text
tema automotivo escuro
preto/grafite
vermelho esportivo
cards discretos
bordas finas
glow controlado
aparência Civic / painel automotivo
```

Evitar:

```text
dashboard administrativo
visual gamer exagerado
neon em excesso
gradientes aleatórios
cards gigantes
scroll longo
elementos decorativos sem função
```

Objetivo:

```text
OEM+
esportivo
premium
clean
automotivo
```

---

# 5. Target principal

Projetar prioritariamente para:

```text
iPhone 16 Pro
portrait
~402 × 874 CSS px
```

Também deve funcionar em:

```text
390px–430px largura
```

e razoavelmente em desktop.

Utilizar:

```css
100dvh
```

quando apropriado.

Considerar:

```css
env(safe-area-inset-top)
env(safe-area-inset-bottom)
```

---

# 6. Home sem scroll

A página principal deve caber completamente em:

```text
100dvh
```

Sem:

```text
scroll vertical
```

no modo instalado como Web App.

Se aberta no Safari com barras visíveis, pequenas diferenças são toleráveis, mas o design deve continuar funcional.

Não esconder conteúdo crítico.

---

# 7. Estrutura de rotas

Criar:

```text
/
```

Home.

```text
/color
```

Cor e brilho.

```text
/animations
```

Animações.

```text
/diagnostics
```

Diagnóstico.

Usar navegação client-side pequena.

Não adicionar biblioteca pesada se uma solução simples resolver.

---

# 8. HOME

A Home deve conter somente:

```text
Header
LEFT / RIGHT
Group Status
Acesso rápido
3 botões de navegação
```

Nada de:

```text
diagnóstico completo
color picker completo
lista de animações
sliders avançados
```

na Home.

---

# 9. Header Home

Layout:

```text
CIVIC LIGHTS                     ● AO VIVO
CONTROLE LOCAL
```

Não incluir:

```text
ícone de configurações
IP grande
rodapé
diagnóstico
```

O endereço `192.168.4.1` não precisa ocupar espaço principal.

---

# 10. Cards LEFT / RIGHT

Criar dois cards lado a lado:

```text
LEFT             RIGHT
```

Cada card mostra:

```text
nome
status
imagem/render do farol
RSSI
```

Exemplo:

```text
LEFT
● Conectado

[ FAROL DINÂMICO ]

▂▄▆ -82 dBm
```

---

# 11. Faróis visuais dinâmicos

Esse é um novo requisito importante.

A imagem do farol NÃO deve ser meramente estática.

Criar componente reutilizável:

```tsx
<HeadlightVisual />
```

API conceitual:

```tsx
<HeadlightVisual
  side="left"
  connectionState="ready"
  mode="rgb"
  color="#FF1414"
  brightness={0.65}
/>
```

ou equivalente.

---

# 12. Separação visual

Construir o farol conceitualmente em camadas:

```text
base/carcaça
+
assinatura luminosa
+
glow
```

A carcaça permanece visualmente igual.

Somente a iluminação muda.

---

# 13. Implementação preferida

Preferir:

```text
SVG
+
mask
+
paths
+
CSS variables
```

ou:

```text
imagem base
+
SVG mask/overlay
```

Não gerar uma nova imagem raster para cada cor.

Não armazenar:

```text
farol-red.png
farol-blue.png
farol-green.png
...
```

---

# 14. Cor do farol

Quando:

```text
Observed State RGB
```

for:

```text
255, 20, 20
```

a iluminação da representação deve ser:

```text
#FF1414
```

Quando for azul:

```text
#006EFF
```

deve parecer azul.

Quando roxo:

```text
#8B2CFF
```

deve parecer roxo.

---

# 15. Fonte de verdade visual

Muito importante:

```text
Headlight Visual
```

deve seguir:

```text
OBSERVED STATE
```

e não simplesmente:

```text
color picker local
```

Fluxo:

```text
usuário escolhe vermelho
↓
Desired State = vermelho
↓
ESP32 envia
↓
SP624E confirma
↓
Observed State atualizado
↓
WebSocket
↓
HeadlightVisual fica vermelho
```

---

# 16. Motivo

Isso transforma a UI também em diagnóstico.

Exemplo:

```text
Desired State:
RED
```

mas:

```text
LEFT observed = RED
RIGHT disconnected
```

UI:

```text
LEFT
farol vermelho
● Conectado

RIGHT
farol escurecido
◉ Reconectando
```

Não mostrar falsamente RIGHT vermelho confirmado.

---

# 17. Estado sem leitura

Quando:

```text
observed_state.valid == false
```

mostrar farol:

```text
escurecido
desaturado
glow desativado
```

Texto:

```text
Aguardando estado
```

---

# 18. Offline

Quando offline:

```text
opacity reduzida
glow = 0
cor neutra/grafite
```

Não representar visualmente como ligado.

---

# 19. Reconnecting

Quando:

```text
RECONNECTING
```

utilizar animação visual discreta:

```text
pulse opacity
```

somente no indicador/status.

Não piscar o farol constantemente.

---

# 20. WHITE real

Quando:

```text
Observed mode == WHITE
```

representar:

```text
branco LED neutro/frio
```

Sugestão visual:

```text
#F4F7FF
```

com glow branco.

Não usar a última cor RGB nesse caso.

---

# 21. Brightness visual

A intensidade visual deve variar aproximadamente com:

```text
Observed brightness
```

Mas evitar representação completamente invisível em brilho muito baixo.

Aplicar mapeamento perceptual.

Conceitualmente:

```ts
visualOpacity = 0.2 + normalizedBrightness * 0.8
```

quando ligado.

Glow também acompanha o brilho.

---

# 22. Group Status

Abaixo dos dois cards:

```text
✓ SINCRONIZADO
Ambos os faróis estão sincronizados
```

Outros estados:

```text
RECONCILIANDO
DEGRADADO
DESCONECTADO
INICIALIZANDO
```

Esse card deve ser compacto.

---

# 23. Acesso rápido

Na HOME:

```text
ACESSO RÁPIDO
```

com:

```text
[ Branco real ]
[ Favorita ]
```

e:

```text
Salvar cor atual como favorita
```

Manter funcionalidade atual.

---

# 24. Feedback dos quick actions

Ao tocar:

```text
Branco Real
```

não assumir imediatamente aplicado.

Mostrar:

```text
Aplicando...
```

até:

```text
GROUP == SYNCED
```

Depois:

```text
✓ Aplicado
```

Mesmo comportamento para Favorita.

---

# 25. Navegação Home

Na parte inferior:

```text
[ COR E BRILHO ]
[ ANIMAÇÕES ]
[ DIAGNÓSTICO ]
```

Cards compactos com:

```text
ícone
nome
descrição curta
chevron
```

Exemplo:

```text
🎨
COR E BRILHO
Escolha a cor
```

Não usar emoji reais se SVG icons forem mais coerentes.

---

# 26. Página Cor e Brilho

Rota:

```text
/color
```

Header compacto:

```text
←        COR E BRILHO        ● AO VIVO
```

---

# 27. Status compacto nessa página

Não repetir cards gigantes LEFT/RIGHT.

Utilizar barra compacta:

```text
LEFT ● -82 dBm | ✓ SINCRONIZADO | RIGHT ● -85 dBm
```

---

# 28. Color Picker

Manter todas as funcionalidades existentes.

Criar:

```text
saturation/value area
hue spectrum
```

otimizado para touch.

Exibir:

```text
preview
HEX
RGB
```

---

# 29. Brightness

Colocar controle de brilho ao lado do color picker quando houver espaço.

No mobile:

```text
color picker ~65%
brightness ~35%
```

para aproveitar largura.

Ou outra composição que caiba no viewport.

---

# 30. Página Color sem scroll

Objetivo:

```text
header
status
picker
brightness
quick presets
```

cabendo em uma tela.

Não duplicar informações desnecessárias.

---

# 31. Não adicionar botão Aplicar se não necessário

O sistema atual possui realtime/throttled updates.

Se isso já está validado:

```text
não adicionar fluxo Apply
```

sem necessidade.

Manter comportamento atual.

---

# 32. Página Animações

Rota:

```text
/animations
```

Header:

```text
←          ANIMAÇÕES          ● AO VIVO
```

---

# 33. Welcome toggle

Topo:

```text
ANIMAÇÃO DE BOAS-VINDAS    [ON]
```

Manter:

```text
enabled / disabled
```

persistido.

---

# 34. Lista de animações

Manter:

```text
Red Welcome
OEM White
Red → White
Premium Pulse
Show Welcome
```

---

# 35. Layout compacto das animações

Não utilizar cards gigantes empilhados como no frontend anterior.

Preferir:

```text
lista compacta
```

ou cards com altura reduzida.

Exemplo:

```text
● Red Welcome        RECOMENDADA    ~1,8s
  Entrada vermelha suave

○ OEM White                        ~1,7s
  Fade branco
```

---

# 36. Preview visual

Cada animação pode conter uma pequena miniatura estilizada do farol.

A miniatura pode reutilizar:

```text
HeadlightVisual
```

em tamanho reduzido.

---

# 37. Duração

Manter slider:

```text
Duração
1,0s ─────────●──── 5,0s
```

ou range já implementado.

---

# 38. Actions

Parte inferior:

```text
[ Testar animação ] [ Salvar ]
```

Manter:

```text
Preview
Stop
Save
```

conforme estado.

---

# 39. Página Diagnóstico

Rota:

```text
/diagnostics
```

Essa é a única tela em que dados técnicos podem aparecer com mais destaque.

Mostrar:

```text
firmware
uptime
LEFT RSSI
RIGHT RSSI
reconnects
Wi-Fi clients
generation
group state
WebSocket
```

---

# 40. Diagnóstico visual

Criar aparência semelhante a:

```text
painel de telemetria automotiva
```

e não dashboard corporativo.

---

# 41. Ressincronizar

Manter:

```text
Ressincronizar
```

nessa página.

Não precisa existir na Home.

---

# 42. Assets de farol

Criar assets originais no projeto.

Não buscar imagens externas em runtime.

Não utilizar URL remota.

Arquivos devem entrar no bundle do ESP32.

---

# 43. Otimização dos assets

Muito importante por causa dos 4 MB de flash.

Se usar raster:

```text
WebP
```

bem comprimido.

Meta:

```text
< 100 KB
```

por asset grande sempre que visualmente aceitável.

SVG deve ser otimizado.

---

# 44. Bundle

Antes:

```text
npm run build
```

Depois medir:

```text
HTML
CSS
JS
SVG
images
total bundle
```

Documentar.

Evitar bundle gigantesco.

---

# 45. Sem internet

Continuar obrigatório:

```text
ZERO external dependencies at runtime
```

Não utilizar:

```text
Google Fonts
CDN
remote icons
remote images
analytics
external API
```

---

# 46. Tipografia

Usar:

```css
-apple-system,
BlinkMacSystemFont,
"Segoe UI",
sans-serif
```

Pode utilizar uma segunda família monospace somente se já embutida ou system font.

Não adicionar uma fonte pesada só pela estética.

---

# 47. Iconografia

Usar SVG local.

Criar conjunto consistente:

```text
palette
sparkles
pulse
wifi
signal
chevron
star
sync
back
```

Evitar misturar várias bibliotecas de ícones.

---

# 48. CSS Design Tokens

Criar tokens.

Exemplo:

```css
--bg: #070809;
--surface: #101216;
--surface-raised: #15171c;

--border: rgba(255,255,255,.10);

--accent: #ff3b30;
--accent-strong: #ff2419;

--success: #9ae637;
--warning: #ffb020;
--danger: #ff453a;

--text: #f5f5f7;
--text-secondary: #9a9ca3;
```

Centralizar essas decisões.

---

# 49. Componentes

Criar/reutilizar:

```text
AppHeader
LiveBadge
HeadlightCard
HeadlightVisual
GroupStatus
QuickAction
NavigationCard
CompactConnectionStatus
ColorPicker
BrightnessControl
AnimationOption
TelemetryCard
```

Evitar HTML duplicado.

---

# 50. HeadlightVisual API

Criar interface clara.

Exemplo:

```ts
interface HeadlightVisualProps {
  side: 'left' | 'right';

  connected: boolean;

  state:
    | 'unknown'
    | 'connecting'
    | 'ready'
    | 'reconnecting'
    | 'offline'
    | 'error';

  mode?: 'rgb' | 'white';

  rgb?: {
    r: number;
    g: number;
    b: number;
  };

  brightness?: number;
}
```

---

# 51. Conversão Observed State → Headlight Visual

Criar função pura:

```ts
mapObservedStateToHeadlightVisual()
```

Testável.

Não espalhar a lógica por componentes.

---

# 52. Estado por lado

Não usar apenas o estado global.

LEFT visual deve utilizar:

```text
LEFT observed state
```

RIGHT:

```text
RIGHT observed state
```

Isso é requisito.

---

# 53. WebSocket

Reutilizar conexão existente.

Se os eventos atuais já possuem:

```text
observed_state
```

usar diretamente.

Se só existir:

```text
desired_state
```

expandir snapshot/evento read-only para expor:

```json
{
  "left": {
    "observed": {
      "valid": true,
      "mode": "rgb",
      "r": 255,
      "g": 20,
      "b": 20,
      "brightness": 64
    }
  },

  "right": {
    "observed": {
      "valid": true,
      "mode": "rgb",
      "r": 255,
      "g": 20,
      "b": 20,
      "brightness": 64
    }
  }
}
```

---

# 54. Não adicionar polling agressivo

Updates visuais devem vir principalmente do:

```text
WebSocket
```

GET status apenas para:

```text
initial bootstrap
fallback eventual
```

---

# 55. Loading

Ao abrir:

```text
Civic Lights
Conectando...
```

Não mostrar dados inventados.

---

# 56. Realtime

Quando WebSocket estiver funcionando:

```text
● AO VIVO
```

Quando cair:

```text
○ RECONECTANDO
```

Não confundir WebSocket offline com SP624E offline.

---

# 57. Transição visual de cor

Quando o Observed State mudar:

```text
old color
→
new color
```

permitir transição CSS curta:

```text
100–200ms
```

no glow.

Não interpolar por vários segundos.

---

# 58. Welcome Animation na UI

Quando Welcome Animation estiver executando no hardware, os componentes dos faróis podem acompanhar visualmente os estados/eventos recebidos.

Não criar uma animação independente no browser que possa divergir do hardware.

Fonte:

```text
ESP32 events / observed animation state
```

---

# 59. Não mostrar farol branco quando vermelho

Esse é um critério explícito.

Se:

```text
LEFT observed = RED
```

não pode permanecer visualmente:

```text
farol branco
```

Se:

```text
RIGHT observed = BLUE
```

mostrar BLUE.

---

# 60. Home offline example

Quando ambos estiverem offline:

```text
LEFT
○ Offline
farol escuro

RIGHT
○ Offline
farol escuro

ESTADO DO CONJUNTO
Inicializando / Offline
```

---

# 61. Home connected example

Quando:

```text
LEFT RGB RED
RIGHT RGB RED
```

Home:

```text
LEFT [farol vermelho]
● Conectado

RIGHT [farol vermelho]
● Conectado

✓ SINCRONIZADO
```

---

# 62. Accessibility

Usar:

```text
aria-label
button semantic
focus
contrast
```

Não depender exclusivamente de cor para status.

---

# 63. Touch

Touch target mínimo aproximado:

```text
44 × 44px
```

---

# 64. Overscroll

No modo instalado:

```css
overscroll-behavior: none;
```

quando apropriado.

Evitar bounce causando sensação de página web mal ajustada.

---

# 65. User-select

Em controles:

```css
user-select: none;
-webkit-user-select: none;
```

quando apropriado.

Não bloquear seleção global sem motivo.

---

# 66. Safe area

Garantir:

```text
Dynamic Island
home indicator
```

não sobreponham conteúdo.

---

# 67. Testes frontend

Adicionar testes para:

```text
Observed RGB → visual color
WHITE → white visual
brightness mapping
offline → no glow
unknown → neutral
LEFT/RIGHT independence
WebSocket payload mapping
routing
quick actions
animation route
diagnostic route
```

---

# 68. Teste fundamental LEFT/RIGHT

Simular:

```text
LEFT observed:
RED

RIGHT observed:
BLUE
```

Resultado obrigatório na UI:

```text
LEFT visual RED
RIGHT visual BLUE
```

Isso prova que os dois cards não estão simplesmente usando Desired State global.

---

# 69. Teste WHITE

Simular:

```text
LEFT mode WHITE
RIGHT mode WHITE
```

Ambos:

```text
white glow
```

---

# 70. Teste reconnect

Simular:

```text
LEFT READY RED
RIGHT RECONNECTING
```

Resultado:

```text
LEFT = vermelho

RIGHT =
farol dark
status Reconectando
```

---

# 71. Teste Home viewport

Utilizar viewport:

```text
402 × 874
```

Verificar:

```text
document.documentElement.scrollHeight
<=
viewport height + pequena tolerância
```

Home não deve exigir scroll.

---

# 72. Outras páginas

Idealmente também evitar scroll.

Porém se alguma tela de diagnóstico excepcionalmente precisar de alguns pixels extras, priorizar legibilidade.

A HOME é requisito absoluto de zero scroll.

Cor/Brilho e Animações devem ser projetadas para caber sem scroll no iPhone 16 Pro sempre que possível.

---

# 73. Responsividade

Testar:

```text
390 × 844
393 × 852
402 × 874
430 × 932
```

---

# 74. Desktop

Em desktop:

```text
max-width
```

centralizado.

Não esticar interface infinitamente.

Sugestão:

```text
max-width: 480px
```

ou pouco maior.

---

# 75. Build

Executar:

```powershell
cd C:\Projetos\ESP32\web
npm install
npm run build
```

Corrigir:

```text
TypeScript errors
lint errors relevantes
tests
```

---

# 76. Bundle audit

Registrar:

```text
JS:
CSS:
assets:
total:
gzip estimate:
```

---

# 77. Integrar bundle no ESP32

Executar pipeline existente:

```text
build frontend
↓
web filesystem
↓
ESP-IDF build
```

Não copiar manualmente arquivos de forma não reproduzível.

---

# 78. Flash

Gravar o novo frontend fisicamente no ESP32.

Se firmware não tiver mudança:

usar atualização da partição web quando disponível.

Caso seja necessária pequena mudança read-only na API:

```text
build firmware
↓
flash firmware
+
web partition
```

---

# 79. Teste desktop real

Conectar Windows:

```text
Civic-Lights
```

Abrir:

```text
http://192.168.4.1
```

Testar todas as rotas.

---

# 80. Teste iPhone real

Conectar iPhone:

```text
Civic-Lights
```

Abrir:

```text
http://192.168.4.1
```

Testar:

```text
HOME
Cor e Brilho
Animações
Diagnóstico
voltar
```

---

# 81. Tela de Início

Também testar pelo ícone instalado no iPhone.

Prioridade visual é:

```text
standalone Web App
```

---

# 82. Teste funcional real de cor

Com ambos conectados:

Selecionar:

```text
RED
```

Esperado:

```text
faróis físicos → RED
↓
Observed State → RED
↓
WebSocket
↓
HeadlightVisual LEFT → RED
HeadlightVisual RIGHT → RED
```

---

# 83. Teste BLUE

Repetir:

```text
BLUE
```

Esperado:

```text
LEFT image → BLUE
RIGHT image → BLUE
```

---

# 84. Teste PURPLE

Repetir com:

```text
PURPLE
```

---

# 85. Teste WHITE REAL

Tocar:

```text
Branco Real
```

Esperado:

```text
faróis físicos white
↓
observed mode WHITE
↓
UI HEADLIGHTS white
```

---

# 86. Teste individual de reconnect

Com RED aplicado:

```text
disconnect RIGHT
```

Esperado UI:

```text
LEFT:
RED
CONNECTED

RIGHT:
RECONNECTING
DARK
```

Após recuperar:

```text
RIGHT:
RED
CONNECTED
```

E:

```text
GROUP = SYNCED
```

---

# 87. Animações

Abrir:

```text
/animations
```

Testar Preview.

O farol visual na Home ou página, quando conectado aos eventos reais, deve refletir a sequência quando tecnicamente disponível.

Não bloquear a entrega do redesign caso os eventos atuais de preview não exponham cada frame; nesse caso documentar.

---

# 88. Não regressão

Testar que continuam funcionando:

```text
RGB
brightness
White
Favorite
Save Favorite
Welcome Animation toggle
Welcome Animation selection
Duration
Preview
Stop
Save
Diagnostics
Resync
Reconnect
Strict Sync
```

---

# 89. Não tocar na estabilidade BLE

Executar pelo menos teste rápido:

```text
15 min
Wi-Fi + WebSocket
LEFT + RIGHT
```

Confirmar:

```text
sem disconnect inesperado causado pelo redesign
sem heap leak
sem watchdog
sem panic
```

---

# 90. Screenshot final

Depois de validar fisicamente, produzir screenshots reais da interface no viewport do iPhone 16 Pro:

```text
Home
Cor e Brilho
Animações
Diagnóstico
```

Salvar em:

```text
docs/screenshots/
```

---

# 91. Documentação

Atualizar:

```text
README.md
AGENTS.md
docs/web-architecture.md
```

Criar:

```text
docs/frontend-redesign.md
```

---

# 92. frontend-redesign.md

Documentar:

```text
routes
layout
design tokens
HeadlightVisual
Observed State mapping
responsive targets
asset sizes
bundle sizes
viewport tests
```

---

# 93. AGENTS.md

Adicionar regras:

```text
Headlight visuals must reflect Observed State.

Never use Desired State as proof that a light has changed.

LEFT and RIGHT visuals are independent.

Home must remain no-scroll on iPhone 16 Pro.

Do not place advanced controls on Home.

Do not add external runtime dependencies.

Do not reintroduce settings button unless a settings feature actually exists.
```

---

# 94. Critérios de aceite

A tarefa só está concluída quando:

- [ ] novo design estiver implementado;
- [ ] Home não tiver scroll em 402×874;
- [ ] Home mostrar LEFT;
- [ ] Home mostrar RIGHT;
- [ ] Home mostrar Group Status;
- [ ] Home mostrar Acesso Rápido;
- [ ] Home possuir Cor e Brilho;
- [ ] Home possuir Animações;
- [ ] Home possuir Diagnóstico;
- [ ] configurações não aparecerem no header;
- [ ] rodapé antigo não existir;
- [ ] diagnóstico completo não estiver na Home;
- [ ] `/color` funcionar;
- [ ] `/animations` funcionar;
- [ ] `/diagnostics` funcionar;
- [ ] LEFT HeadlightVisual existir;
- [ ] RIGHT HeadlightVisual existir;
- [ ] visual usar Observed State;
- [ ] RGB alterar visual;
- [ ] WHITE alterar visual;
- [ ] brightness alterar intensidade visual;
- [ ] offline remover glow;
- [ ] reconnect mostrar estado correto;
- [ ] LEFT/RIGHT puderem visualmente ter cores diferentes;
- [ ] quick actions continuarem funcionando;
- [ ] favorite continuar funcionando;
- [ ] animation settings continuarem funcionando;
- [ ] diagnostics continuar funcionando;
- [ ] Resync continuar funcionando;
- [ ] WebSocket continuar funcionando;
- [ ] bundle estiver armazenado no ESP32;
- [ ] página abrir sem internet;
- [ ] teste no iPhone passar;
- [ ] nenhuma regressão BLE ocorrer;
- [ ] screenshots finais forem gerados;
- [ ] documentação atualizada.

---

# 95. Relatório final

Entregar:

```text
CIVIC LIGHTS FRONTEND REDESIGN REPORT
=====================================

Frontend
--------
React:
TypeScript:
Vite:
Build:
Tests:

Routes
------
Home:
Color:
Animations:
Diagnostics:

Home Layout
-----------
Target viewport:
Actual dimensions:
Scroll height:
No-scroll:
Header:
LEFT/RIGHT:
Quick access:
Navigation:

Headlight Visual
----------------
Implementation:
SVG/mask:
Asset size:

Observed RGB mapping:
Observed WHITE mapping:
Brightness mapping:
Offline state:
Reconnect state:

Hardware Visual Tests
---------------------
RED:
LEFT visual:
RIGHT visual:

BLUE:
LEFT visual:
RIGHT visual:

PURPLE:
LEFT visual:
RIGHT visual:

WHITE:
LEFT visual:
RIGHT visual:

Reconnect Visual Test
---------------------
Disconnected side:
Healthy side:
Disconnected visual:
Recovered visual:
Final GROUP:

Bundle
------
JS:
CSS:
Images:
SVG:
Total:

ESP32
-----
Web image:
Flash:
HTTP:
WebSocket:

iPhone
------
Safari:
Home Screen mode:
Home no-scroll:
Color page:
Animations page:
Diagnostics page:

Regression
----------
RGB:
Brightness:
White:
Favorite:
Welcome:
Reconnect:
Strict Sync:

Stability
---------
Duration:
BLE disconnects:
Wi-Fi disconnects:
WebSocket disconnects:
Heap:
Panic:
Watchdog:

Screenshots
-----------
...

Conclusion
----------

Open Issues
-----------
...
```

---

# REGRA FINAL

O redesign deve transformar o Civic Lights de uma página técnica extensa em uma interface de controle automotivo compacta.

A Home deve responder imediatamente:

```text
Os dois faróis estão conectados?
↓
Qual estado visual cada um possui?
↓
Eles estão sincronizados?
↓
Quero Branco ou Favorita?
↓
Quero acessar Cor, Animações ou Diagnóstico?
```

Tudo isso sem scroll.

E a representação visual deve corresponder ao hardware real:

```text
SP624E
↓
Observed State
↓
WebSocket
↓
HeadlightVisual
```

Não apenas:

```text
Color Picker
↓
imagem bonita
```

O ESP32 continua sendo a fonte de verdade.
