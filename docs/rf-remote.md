# Controle remoto RX480E 433 MHz e LED indicador

## Hardware

| Função | RX480E/LED | ESP32 |
|---|---|---|
| Canal físico 0 | D0 | GPIO25 |
| Canal físico 1 | D1 | GPIO26 |
| Canal físico 2 | D2 | GPIO27 |
| Canal físico 3 | D3 | GPIO32 |
| Transmissão válida | VT | GPIO33 |
| Indicador especial | LED + resistor ~330 Ω | GPIO23 |

RX480E usa 3V3 e GND comum. LED instalado mostrou polaridade ativa LOW;
GPIO23 inicia HIGH (inativo) para permanecer apagado no boot.

## Arquitetura

```text
GPIO D0-D3 + VT
  → polling 10 ms
  → debounce 50 ms + release latch
  → physical channel
  → mapping NVS
  → logical button event
  → Remote Controller
  → Desired State ou Animation Manager
  → Group Controller
  → filas LEFT + RIGHT
  → query/Observed State/verificação
```

RF nunca chama GATT nem fila de dispositivo diretamente. Callback de entrada só
enfileira evento; ação roda em task dedicada.

## Descoberta e mapping

O mapping foi descoberto fisicamente na bancada antes de habilitar ações:

```text
RF INPUT VT=1 D0=0 D1=1 D2=0 D3=0
RF: physical channel D3 mapped to BUTTON_A
```

Mapping confirmado do controle A/B/C/D:

```text
A → D3
B → D2
C → D1
D → D0
```

Instalação nova começa sem mapping lógico. No ESP instalado, esse mapping foi
salvo explicitamente em NVS após descoberta física. Configuração NVS nunca é
inferida, substituída nem “corrigida” automaticamente pelo firmware.

Antes, ative modo seguro com `POST /api/v1/remote/discovery/start`. Ele não
apaga nem altera mapping NVS; somente suprime ações lógicas até
`POST /api/v1/remote/discovery/stop` ou reboot.

```http
PUT /api/v1/remote/mapping
Content-Type: application/json

{"button1":"d3","button2":"d2","button3":"d1","button4":"d0"}
```

Exemplo é ilustrativo; nunca copie sem observar hardware. NVS preserva mapping.
Firmware não substitui nem “corrige” mapping automaticamente.

## Debounce

- polling: 10 ms;
- estabilidade mínima: 50 ms;
- somente máscara com um bit ativo gera evento;
- mesmo canal fica travado até release estável;
- long press gera um evento, sem ação adicional;
- VT é registrado e usado como confirmação auxiliar, nunca como gate obrigatório.

## Botões

| Botão | Ação |
|---|---|
| A (lógico 1) | WHITE real, brilho 255; fixo |
| B (lógico 2) | RGB 255/0/0, brilho 255 (100%); fixo |
| C (lógico 3) | Police runtime toggle; fixo |
| D (lógico 4) | Favorita por padrão; configurável e persistido |

Botão D aceita `favorite`, `rgb`, `white` e `police`. A Home contém
somente o atalho **Controle remoto**; o editor fica na página própria `/remote`.

## Police

Padrão sem mistura cromática: RED 100% → apagado → RED 100% → apagado → BLUE
100% → apagado. Todas as fases mantêm o canal de brilho em 255; o apagão usa
RGB 0/0/0 para não provocar uma rampa de brilho 0 → 255 no flash seguinte. Cada
transição Police é barreira FIFO, portanto o apagão não pode ser removido pela
coalescência de frames. Timeout fixo: 30 s.

Velocidade persistida em NVS:

| Perfil | Cor / apagado | Ciclo completo |
|---|---:|---:|
| `slow` | 400 / 200 ms | 1800 ms |
| `normal` | 300 / 150 ms | 1350 ms |
| `fast` (default) | 220 / 150 ms | 1110 ms |
| `very_fast` | 150 / 150 ms | 900 ms |

No perfil `very_fast`, 150 ms por fase é o limite prático observado para cada
write GATT. O sequenciador espera as duas filas esvaziarem antes de admitir a
próxima cor, preservando cada apagão e impedindo backlog/mistura roxa.

```http
PUT /api/v1/remote/police
Content-Type: application/json

{"speed":"very_fast"}
```

Config NVS v1 migra para v2 mantendo mapping e botão D; velocidade recebe
default `fast`.

Fases Police usam fluxo adaptativo: novo frame só entra quando filas LEFT e
RIGHT estão livres. Isso preserva RED/OFF/RED/OFF/BLUE/OFF, impede backlog e respeita o
limite físico observado de ~150 ms por write GATT.

Desired State não muda durante Police. Stop, timeout ou disconnect encerra sessão,
descarta frames antigos e força restauração/reconciliação do Desired State atual.
Comando web/RF mais recente cancela animação temporária. Police nunca inicia no
boot e estado ativo nunca é persistido.

## LED indicador

- Police ativo com LEFT/RIGHT READY: ON imediato e contínuo;
- RGB confirmado: ON;
- RGB Desired em reconciliação: ON;
- retorno a WHITE: mantém valor anterior até WHITE confirmado;
- WHITE confirmado: OFF;
- grupo offline/desconhecido, inclusive tentativa de animação: OFF conservador.

Task do indicador avalia política a cada 100 ms. Somente `indicator.c` escreve
GPIO23.

## REST e WebSocket

`GET /api/v1/remote` retorna receptor, VT, mapping, último botão, Button 4,
Police/velocidade e indicador. `PUT /api/v1/remote/button4` persiste Button D;
`PUT /api/v1/remote/police` persiste velocidade Police.

Eventos: `remote_button`, `remote_action_started`, `remote_action_completed`,
`remote_action_failed`, `remote_config_updated`, `police_*`,
`runtime_animation` e `indicator_status`. Frames continuam snapshots completos;
WebSocket não é fonte de verdade.

## Ordem segura de testes

1. `./tests/run-host-tests.ps1`.
2. Build do frontend e firmware.
3. Flash em bancada, sem SP624E/faróis.
4. Rodar `./tests/run-rf-bench-tests.ps1`; descobrir mapping e validar um evento
   por clique/long press.
5. Validar GPIO23 OFF → ON por ação controlada, ainda sem faróis.
6. Validar PWA, NVS e reboot.
7. Só então executar teste real curto: A, B, A, C por 2–4 s, A, D, A.

Pare imediatamente se LEFT != RIGHT, houver backlog, disconnect ou LED incorreto.
Tempo colorido físico deve ficar abaixo de um minuto. Nunca usar faróis do carro
como ambiente de desenvolvimento ou stress test.

Teste LED em bancada: `POST /api/v1/indicator/test`. Sequência única OFF 1 s,
ON 1 s, OFF; não cria loop nem escreve BLE. O mesmo comando está disponível em
**Home → Controle remoto → Testar LED indicador**.

O runner de bancada bloqueia por padrão se LEFT ou RIGHT estiver READY. Após
salvar Button 4 PURPLE, faça power cycle e execute
`./tests/run-rf-bench-tests.ps1 -VerifyReboot` para provar NVS.

## Próximo passo

Sensor do farol original e ROAD MODE: sinal original ON força WHITE e bloqueia
ações coloridas 2/3/4. Não implementado nesta versão.
