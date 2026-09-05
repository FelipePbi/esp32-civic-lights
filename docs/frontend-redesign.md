# Civic Lights — frontend redesign 0.6.0

> Nota de versão: este documento registra o redesign original. A rota
> `/animations` e a funcionalidade Welcome foram removidas em `0.7.6`.

## Direção

Interface OEM+ automotiva: fundo preto/grafite, superfícies técnicas, vermelho
esportivo controlado, bordas finas e glow restrito à assinatura luminosa. Usa
somente system fonts, React, CSS e SVG inline. Zero recurso externo em runtime.

Target primário: iPhone 16 Pro portrait, `402 × 874` CSS px, com safe areas,
`100dvh` e `overscroll-behavior: none`. Larguras automatizadas: `390`, `393`,
`402` e `430` px. Desktop fica centralizado em 480 px.

## Rotas e layout

| Rota | Conteúdo |
|---|---|
| `/` | Header, LEFT/RIGHT, Group Status, Acesso Rápido, três destinos |
| `/color` | Header compacto, status, SV/hue, brilho vertical, Branco/Favorita |
| `/animations` | Toggle, cinco estilos, thumbnails, duração, preview/stop/save |
| `/diagnostics` | Oito cartões de telemetria e resync |

Home usa grid travado em `100dvh` e `overflow: hidden`. Não contém picker,
animações completas, diagnóstico completo, IP, settings ou footer.

## Design tokens

Tokens ficam em `web/src/styles.css`:

```css
--bg: #070809;
--surface: #101216;
--surface-raised: #15181d;
--border: rgba(255, 255, 255, .1);
--accent: #ff3b30;
--success: #9ae637;
--warning: #ffb020;
--danger: #ff453a;
--text: #f5f5f7;
--text-secondary: #9a9ca3;
--text-tertiary: #81858e;
```

## HeadlightVisual

`HeadlightVisual` é SVG inline original, espelhado por `side`. Camadas:

1. sombra e carcaça;
2. bezel, lente e projetor;
3. máscara da assinatura luminosa;
4. assinatura sólida e blur/glow com CSS variables.

Não existem variantes raster por cor. Mudança de cor/glow usa transição CSS de
160 ms. `prefers-reduced-motion` reduz animações.

`mapObservedStateToHeadlightVisual()` é função pura. Ordem:

1. connection state do lado;
2. `observed.valid` e `power`;
3. `mode` RGB/WHITE;
4. cor e intensidade observadas.

RGB converte `r/g/b` em HEX e usa `brightness`. WHITE fixa `#F4F7FF` e usa
`white`. Opacidade ativa é `0.2 + brightness/255 × 0.8`; glow acompanha o nível.
Offline, reconnect, connecting, erro, leitura inválida ou power off resultam em
glow zero e farol neutro. LEFT/RIGHT nunca compartilham cor observada.

## Realtime e ações

Bootstrap usa REST. Atualização principal usa snapshot WebSocket completo; HTTP
faz fallback apenas enquanto socket está fora. Reconexão do WebSocket usa 500 ms,
1 s, 2 s e 5 s. Indicador do header descreve navegador ↔ ESP32, separado do BLE.

Picker continua throttled em 100 ms e envia evento final imediatamente. Branco
e Favorita mostram `Aplicando…` após HTTP 202 e só mostram `Aplicado` quando
snapshot chega com grupo `SYNCED` e geração correspondente verificada.

## Responsividade e acessibilidade

- Home verificada sem scroll em `390×844`, `393×852`, `402×874`, `430×932`.
- controles touch principais com cerca de 44 px;
- elementos nativos (`button`, `a`, `input`, `fieldset`);
- labels ARIA no picker, brilho, switch, faróis e status;
- skip link e foco visível;
- contraste WCAG AA automatizado nas quatro rotas;
- status sempre combina texto/ícone com cor.

## Testes

Vitest: 18 testes. Cobertura de conversão de cor, payloads, parser WebSocket,
throttle, routing, RGB independente, WHITE, brilho, offline, unknown e reconnect.

Playwright + axe-core: 25 testes sobre o bundle de produção. Cobertura de quatro
viewports sem scroll, rotas diretas/client-side (incluindo barra final), histórico,
conteúdo da Home, feedback `Aplicando/Aplicado`, ação supersedida por outro
cliente, payloads Favorite/Save, Welcome preview/save/stop, Resync, LEFT
vermelho/RIGHT azul, WHITE, unknown/offline/reconnect e WCAG A/AA nas quatro
rotas. REST e WebSocket são simulados com snapshots completos nesses testes;
isso não substitui smoke no ESP32.

O runner host também compila o `main/web/json_codec.c` real e valida os
contratos REST/WS: conexão e Observed State independentes, LEFT vermelho, RIGHT
azul, WHITE usando o canal `white` e lado inválido sem campos inventados.

Comandos:

```powershell
cd C:\Projetos\ESP32\web
npm test
npm run test:e2e
npm run build
```

## Bundle de produção

Build Vite de 2026-08-10:

| Tipo | Bytes | Gzip estimado |
|---|---:|---:|
| HTML | 817 | 410 |
| JS | 222.965 | 70.604 |
| CSS | 21.573 | 5.070 |
| PNG local/PWA | 25.410 | 23.358 |
| SVG local | 443 | 257 |
| Manifest | 383 | 215 |
| **Total** | **271.591** | **99.914** |

`build/web.bin` ocupa o tamanho fixo da partição SPIFFS: 2.031.616 bytes. O
conteúdo bruto usa cerca de 13,4% dessa capacidade.

Hashes do build final preparado:

```text
sp624e_controller.bin  4652AD4043B26B0564C8B1BD2F96B22A1B9EAD45F1A7F2D64F209AC8E909CC64
web.bin                3A81D9E95AAC50B8D2536DD66C7B87EEAC22BD3E018AE1D8DE596FCCECAF7032
```

## Screenshots

Arquivos em `docs/screenshots/`:

- `home.png`
- `color.png`
- `animations.png`
- `diagnostics.png`

As imagens atuais foram geradas automaticamente do bundle de produção em Chrome
no viewport `402 × 874`, com controlador REST/WebSocket simulado. Servem como
referência visual reproduzível. Validação Safari/Web App e substituição por
capturas do iPhone físico devem ser registradas quando o aparelho estiver
acessível.

## Limitação conhecida de preview

Welcome Animation não executa uma animação paralela no navegador. As miniaturas
da lista são apenas identificação visual. O firmware publica progresso e
snapshots, mas não consulta/expõe Observed State para cada frame temporário;
portanto a interface não promete reproduzir cada frame. Estado final continua
dependendo do restore e da verificação reais do firmware.
