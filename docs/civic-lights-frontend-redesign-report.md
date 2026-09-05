# CIVIC LIGHTS FRONTEND REDESIGN REPORT

> HISTÓRICO: a rota `/animations` e a funcionalidade Welcome avaliadas neste
> relatório foram removidas por decisão do usuário na versão `0.7.6`.

Data: 2026-08-10. Firmware alvo: `0.6.0`.

## Frontend

| Item | Resultado |
|---|---|
| React | 19.2.8 |
| TypeScript | 5.9.3, strict, PASS |
| Vite | 7.3.6, production build PASS |
| Vitest | 18/18 PASS |
| Playwright + axe | 25/25 PASS sobre `dist` |
| Host C/Python | PASS |
| Contrato JSON REST/WS real | PASS (`main/web/json_codec.c`) |
| ESP-IDF full clean | PASS |

## Routes

| Rota | Resultado automatizado |
|---|---|
| Home `/` | PASS |
| Color `/color` | PASS |
| Animations `/animations` | PASS |
| Diagnostics `/diagnostics` | PASS |

Router usa History API. Fallback SPA foi preservado; caminhos desconhecidos de
`/api/` agora retornam 404 em vez de `index.html`.

## Home Layout

| Item | Resultado |
|---|---|
| Target | iPhone 16 Pro, `402 × 874` |
| Viewport real do teste | `402 × 874` |
| `scrollHeight` | 874 px, inferido e confirmado por screenshot full-page de 874 px + assertion DOM |
| No-scroll | PASS em 390×844, 393×852, 402×874, 430×932 |
| Header sem settings/IP | PASS |
| LEFT/RIGHT | PASS |
| Group Status | PASS |
| Quick access | PASS |
| Navegação | PASS |

## Headlight Visual

- Implementação: SVG inline original, espelhado por lado.
- Camadas: carcaça, lente/projetor, mask da assinatura, luz e glow.
- Raster por cor: zero.
- Asset remoto: zero.
- Fonte visual: `observed.left/right` + connection state do mesmo lado.
- RGB: canais observados convertidos para HEX.
- WHITE: `#F4F7FF`, intensidade de `observed.white`.
- Brilho: `0.2 + normalizedBrightness × 0.8` quando verificado.
- Offline/reconnect/unknown/power-off: glow zero.
- LEFT vermelho/RIGHT azul: PASS automatizado no componente final.

## Hardware Visual Tests

| Teste | Estado |
|---|---|
| RED físico → LEFT/RIGHT visual | PENDING |
| BLUE físico → LEFT/RIGHT visual | PENDING |
| PURPLE físico → LEFT/RIGHT visual | PENDING |
| WHITE físico → LEFT/RIGHT visual | PENDING |
| Disconnect/reconnect individual | PENDING |

Flash automático em `COM5` falhou antes de qualquer escrita com `Wrong boot
mode detected (0x13)`. Em 2026-08-11, entrada física por BOOT + EN permitiu
`flash.ps1 -ManualBoot`: bootloader, tabela, firmware e `web.bin` foram gravados
e verificados por hash pelo esptool. NVS não foi apagada.

Depois do power cycle, boot serial confirmou ESP-IDF 6.0.2, firmware `0.6.0`,
partição web em `0x210000`, SPIFFS montado e servidor HTTP pronto. LEFT e RIGHT,
porém, não anunciaram; ambos entraram em `BACKOFF` após scans direcionados.
Testes visuais continuam pendentes até os dois SP624E estarem energizados.

## Bundle

| Tipo | Bytes | Gzip estimado |
|---|---:|---:|
| HTML | 817 | 410 |
| JS | 222.965 | 70.604 |
| CSS | 21.573 | 5.070 |
| PNG | 25.410 | 23.358 |
| SVG | 443 | 257 |
| Manifest | 383 | 215 |
| **Total** | **271.591** | **99.914** |

Artefatos preparados:

```text
sp624e_controller.bin  1.061.536 bytes
web.bin                2.031.616 bytes (imagem fixa da partição)
app SHA-256            4652AD4043B26B0564C8B1BD2F96B22A1B9EAD45F1A7F2D64F209AC8E909CC64
web SHA-256            3A81D9E95AAC50B8D2536DD66C7B87EEAC22BD3E018AE1D8DE596FCCECAF7032
```

## ESP32

| Item | Estado |
|---|---|
| Web image gerada do source final | PASS |
| Firmware build | PASS |
| Flash app + web | PASS, esptool verificou todos os blocos |
| Boot firmware `0.6.0` | PASS |
| SPIFFS | PASS, `276351/1860161` bytes no boot |
| SoftAP | PASS, `Civic-Lights`, canal 6, BSSID `80:f3:da:54:37:fd` |
| `GET /api/v1/status` real | PASS, HTTP 200 em três amostras |
| Shell e rotas do novo bundle | PENDING smoke completo |
| WebSocket real | PENDING hardware |

## iPhone

| Item | Estado |
|---|---|
| Chrome emulado 402×874 | PASS |
| Safari físico | PENDING |
| Home Screen mode | PENDING |
| Home no-scroll físico | PENDING |
| Color/Animations/Diagnostics físicos | PENDING |

## Regression

| Função | Automatizado | Hardware pós-flash |
|---|---|---|
| RGB/throttle/final | PASS | PENDING |
| Brightness | PASS | PENDING |
| White | PASS | PENDING |
| Favorite/Save | PASS | PENDING |
| Welcome toggle/select/duration/preview/stop/save | PASS | PENDING |
| Resync | PASS | PENDING |
| Reconnect/Strict Sync | mapper PASS | PENDING |
| Geração concorrente entre clientes | PASS | PENDING |

Dois Observed States inválidos agora são comparados como iguais; isso evita
publicar snapshots WebSocket completos a cada 250 ms enquanto a leitura ainda
não é válida. O caso está coberto nos testes host.

## Stability

Teste de 15 minutos com novo bundle, BLE, Wi-Fi e WebSocket: PENDING. Flash e
boot passaram, mas LEFT/RIGHT não estavam anunciando. Heap, disconnects, panic
e watchdog só podem ser aceitos com ambos os controladores conectados.

Observação UART adicional de 45 s mostrou scans direcionados alternados e
tentativas 13–16 com backoff entre 9,6 e 10,4 s para os dois lados. Nenhum tight
loop, panic ou watchdog apareceu; isso comprova o backoff offline, não substitui
estabilidade com LEFT/RIGHT READY.

## Screenshots

- `docs/screenshots/home.png`
- `docs/screenshots/color.png`
- `docs/screenshots/animations.png`
- `docs/screenshots/diagnostics.png`

Todas têm `402 × 874` e foram capturadas do bundle de produção com controlador
simulado. Não são capturas do iPhone físico.

## Conclusion

Redesign, contratos, testes, documentação, firmware e imagem SPIFFS estão
gravados no ESP32. Boot `0.6.0`, montagem web, SoftAP e API HTTP foram observados
fisicamente. Aceite final ainda depende de smoke completo, SP624E energizados,
testes visuais, iPhone e estabilidade.

## Open Issues

1. Energizar LEFT e RIGHT; confirmar ambos READY/SYNCED.
2. Rodar smoke HTTP/WebSocket por iPhone ou em janela de Wi-Fi explicitamente
   autorizada, sem interromper a internet do notebook.
3. Validar RED, BLUE, PURPLE, WHITE e reconnect individual com ambos SP624E.
4. Rodar estabilidade por 15 minutos e substituir screenshots por capturas do
   iPhone físico.
5. Welcome progress não expõe Observed State por frame; UI não sintetiza frames.
