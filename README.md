# Civic Lights — SP624E Controller

Firmware ESP-IDF 6.0.2 para um ESP32 controlar dois SP624E como um único grupo
LEFT/RIGHT. A versão `0.8.0` inclui controle RX480E 433 MHz, Police runtime,
LED indicador, recuperação automática e frontend automotivo local, mantendo
Strict Sync e ESP32 como fonte de verdade. A funcionalidade Welcome foi removida.

## Hardware e mapping

- ESP32-D0WD-V3 rev. 3.1, dual-core, flash 4 MB, target `esp32`.
- LEFT: `FF:FF:11:CD:AC:FA`, PUBLIC.
- RIGHT: `FF:FF:11:CD:A0:60`, PUBLIC.
- Serviço FFE0, characteristic FFE1 e CCCD `0x2902`.
- RX480E: D0 GPIO25, D1 GPIO26, D2 GPIO27, D3 GPIO32 e VT GPIO33.
- LED indicador: GPIO23, resistor ~330 Ω, ativo em nível baixo no hardware instalado.

## Acesso local

```text
SSID: Civic-Lights
Senha do ponto de acesso: zaq12wsx
URL: http://192.168.4.1
```

A senha está centralizada em `main/app_config.h`. Ela é pública e destinada
somente ao desenvolvimento; altere `APP_WIFI_AP_PASSWORD` antes da instalação
definitiva. O sistema não requer internet, roteador, nuvem ou computador ligado.

## Arquitetura de controle

```text
React ─┐
RF ────┴→ Control Intent → Group API / Animation Manager → LEFT + RIGHT
                    ← WebSocket ← snapshots verificados / RF / indicador
```

Handlers HTTP nunca fazem GATT write. Strict Sync continua obrigatório: se um
lado estiver fora, a nova geração é retida e o lado saudável não muda sozinho.
O comando é aplicado e verificado somente quando LEFT e RIGHT estão READY.

## Controle remoto 433 MHz

RX480E usa polling de 10 ms, debounce de 50 ms, detecção de release e trava de
pressionamento longo. Mapping começa vazio: firmware apenas registra D0–D3 até
identificação física e persistência explícita. Botões: 1 WHITE, 2 RED em 100%,
3 Police toggle, 4 preset configurável pelo PWA. Police expira em 30 s, cancela
por comando/disconnect e restaura Desired State verificado.

GPIO23 fica OFF em WHITE confirmado e ON para RGB/Police. Estado desconhecido
sem animação usa política conservadora OFF. Detalhes: [controle RF](docs/rf-remote.md).

## Branco real

WHITE usa `15 01 CC` e `21 02 LEVEL FF`, com modo explícito no Desired State.
O recurso começa desabilitado. Para liberar:

1. execute `test-white` no console;
2. observe LEFT isolado, RIGHT isolado, ambos em branco e a transição para RGB;
3. após `WHITE_TEST_RESULT=PASS`, confirme visualmente com
   `white-confirm yes` — ou mantenha bloqueado com `white-confirm no`.

A confirmação fica em NVS. A UI nunca apresenta um botão Branco funcional sem
esse aceite físico.

## Frontend Civic Lights

A interface React/Vite funciona integralmente offline e usa quatro rotas:

- `/`: LEFT/RIGHT, faróis dinâmicos, estado do conjunto, Branco/Favorita e
  navegação; cabe sem scroll em `402 × 874`.
- `/color`: picker touch, hue, brilho realtime e atalhos.
- `/remote`: módulo RF/Button 4, Police e indicador.
- `/diagnostics`: firmware, uptime, RSSI, reconnects, Wi-Fi, geração,
  WebSocket e resync.

Cada `HeadlightVisual` usa `observed.left` ou `observed.right`, combinado com o
estado de conexão do mesmo lado. Desired State alimenta os controles, mas nunca
é usado como confirmação visual. WHITE usa o canal observado `white`; lado
offline/reconectando fica escuro mesmo se houver leitura antiga em memória.

## Console serial

UART0, 115200 baud:

```text
status
state
metrics
rgb <r> <g> <b> <brightness>
resync
disconnect left|right
reconnect left|right
test-reconnect left|right
test-sync
test-stress
test-midfail
test-stability [seconds]
test-white
white-confirm yes|no
restore
```

O leitor de linha sobe no boot, então o console responde mesmo antes de os
faróis conectarem.

### Diagnóstico BLE

Ferramenta de engenharia reversa para investigar um terceiro dispositivo BLE
sem interferir nos SP624E. Começa desabilitada a cada boot e nunca envia bytes
que não tenham sido digitados.

```text
diag help
diag status
diag enable
diag disable
diag scan start [seconds]
diag scan stop
diag scan list [address]
diag scan clear
diag target <address>|clear
diag connect
diag disconnect
diag gatt
diag read <service_uuid> <characteristic_uuid>
diag subscribe <service_uuid> <characteristic_uuid>
diag unsubscribe <service_uuid> <characteristic_uuid>
diag write <service_uuid> <characteristic_uuid> <hex>
diag write_nr <service_uuid> <characteristic_uuid> <hex>
```

A recuperação dos SP624E tem prioridade absoluta: ela aborta scan e tentativa
de conexão do diagnóstico antes de pedir o rádio. Detalhes em
[diagnóstico BLE](docs/ble-diagnostics.md).

## Iluminação interna

O kit ambiente `LEDCAR-00-1900` acompanha automaticamente a cor dos faróis,
sempre em **best effort**: falha, ausência ou reconexão do interior nunca
retém, degrada ou bloqueia LEFT e RIGHT, nem o PWA.

```text
Desired State branco padrão  → interior RGB 0,0,0 (visual off)
Desired State RGB ativo      → interior recebe o mesmo RGB
```

A condição de branco usa `SP624E_LIGHT_MODE_WHITE`, a mesma semântica que já
apaga o LED indicador — não uma comparação de bytes. Brilho não é propagado e
animações não são espelhadas: os dois comandos ainda são desconhecidos no
controlador interno.

O transporte conecta sob demanda, mantém o link por 60 s de ociosidade e então
desconecta, devolvendo o controlador ao aplicativo original. O interior só pede
o papel master do BLE com LEFT e RIGHT em `READY`, e cede imediatamente quando a
recuperação dos faróis precisa do rádio. Detalhes e evidência de hardware:
[pesquisa do LED interno](docs/interior-led-ble-research.md).

## Build, testes e flash

```powershell
.\scripts\doctor.ps1
.\scripts\build-web.ps1
.\tests\run-host-tests.ps1
.\scripts\build.ps1 -FullClean
.\scripts\flash-monitor.ps1
```

Para atualizar somente o SPIFFS da interface:

```powershell
.\scripts\flash-web.ps1
```

Se a placa exigir entrada manual no bootloader, segure BOOT, toque EN/RESET e
use `.\scripts\flash-web.ps1 -ManualBoot`, soltando BOOT somente após o término.

Com o notebook conectado ao `Civic-Lights`, o smoke test HTTP/WebSocket é:

```powershell
.\tests\run-web-hardware-tests.ps1
.\tests\run-rf-bench-tests.ps1 # RX480E/LED, exige faróis não READY
.\tests\run-rf-bench-tests.ps1 -VerifyReboot
```

Proteção da bateria: preparar tudo com faróis desligados. Runs visuais reais
devem ser curtos e executados somente depois dos testes de bancada.

Depois de ambos os lados estarem `READY` e com observação visual disponível, o
teste exigido de 50 mudanças, confirmação WebSocket `SYNCED` e restauração é:

```powershell
.\tests\run-web-hardware-tests.ps1 -RgbCount 50
```

`build.ps1` executa testes do frontend, testes C de host, build React e build do
firmware. A tabela de 4 MB reserva 2 MB para o app e `0x1f0000` para a web.

Desenvolvimento isolado do frontend:

```powershell
cd web
npm install
npm run dev
npm test
npm run test:e2e
npm run build
```

No carro: ligue o ESP32, conecte o iPhone à rede `Civic-Lights` e abra
`http://192.168.4.1`. Para instalar o atalho observado no aparelho, use no
Safari **Compartilhar → Adicionar à Tela de Início**. O aviso de rede sem
internet é esperado.

Documentação: [arquitetura web](docs/web-architecture.md),
[redesign do frontend](docs/frontend-redesign.md),
[API HTTP](docs/http-api.md), [WebSocket](docs/websocket.md),
[controle RF e indicador](docs/rf-remote.md),
[uso no iPhone](docs/iphone-usage.md),
[runtime BLE](docs/runtime-architecture.md),
[reconexão](docs/reconnection.md),
[reconciliação](docs/state-reconciliation.md),
[protocolo](docs/sp624e-protocol.md),
[diagnóstico BLE](docs/ble-diagnostics.md) e
[pesquisa do LED interno](docs/interior-led-ble-research.md).
O [relatório de validação web](docs/sp624e-web-controller-report.md)
mantém separados os resultados comprovados e os testes físicos pendentes.
