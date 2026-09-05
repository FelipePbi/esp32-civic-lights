# GOAL — SP624E Control Protocol + State Query + LEFT/RIGHT Provisioning + First Synchronized Test

## 1. Contexto confirmado

O projeto existente está em:

```text
C:\Projetos\ESP32
```

Não recrie o projeto.

Continue trabalhando sobre a implementação atual.

Hardware ESP32:

```text
Chip: ESP32-D0WD-V3
Revision: v3.1
Flash: 4 MB
CPU: dual-core
Target ESP-IDF: esp32
USB/Serial: WCH CH9102
Porta atual: COM5
```

Ambiente:

```text
Windows 11 Pro 64-bit
ESP-IDF: v6.0.2 stable
Python: 3.11.15
Git: 2.52.0
CMake: 4.0.3
Ninja: 1.12.1
esptool: 5.3.1
```

BLE:

```text
NimBLE: OK
BLE-only
Maximum connections: 3
```

Dois controladores SP624E já foram identificados e validados fisicamente.

## SP624E_1

```text
Address: FF:FF:11:CD:A0:60
Address type: PUBLIC

Manufacturer ID: 0x5053
Manufacturer data: 0F 00 FF FF 11 CD A0 60

FFE0:
handles 0x0001–0x0004

FFE1:
value handle 0x0003

Properties:
WRITE
WRITE_NO_RESPONSE
NOTIFY

CCCD:
UUID 0x2902
handle 0x0004
```

## SP624E_2

```text
Address: FF:FF:11:CD:AC:FA
Address type: PUBLIC

Manufacturer ID: 0x5053
Manufacturer data: 0F 00 FF FF 11 CD AC FA

FFE0:
handles 0x0001–0x0004

FFE1:
value handle 0x0003

Properties:
WRITE
WRITE_NO_RESPONSE
NOTIFY

CCCD:
UUID 0x2902
handle 0x0004
```

Ambos já ficaram simultaneamente:

```text
CONNECTED
GATT discovered
FFE0 FOUND
FFE1 FOUND
READY
```

por mais de 60 segundos sem desconexões.

---

# 2. Objetivo

Implementar a primeira versão real do protocolo SP624E/BanlanX v3.

Nesta etapa devemos provar:

```text
ESP32
 ↓
consulta estado do SP624E
 ↓
recebe resposta via NOTIFY
 ↓
decodifica estado
 ↓
salva snapshot
 ↓
envia comando controlado
 ↓
farol muda
 ↓
restaura estado anterior
```

Depois:

```text
identificar fisicamente LEFT / RIGHT
 ↓
persistir mapping
 ↓
enviar primeiro comando sincronizado aos dois
 ↓
restaurar ambos
```

Ainda NÃO implementar:

- PWA;
- Wi-Fi;
- HTTP;
- WebSocket;
- receptor 433 MHz;
- Reconnection Manager definitivo;
- efeitos completos;
- controle musical;
- OTA.

---

# 3. Princípio de segurança

Nesta etapa já haverá GATT WRITE real.

Portanto:

**NUNCA altere um controlador antes de conseguir obter e validar seu estado atual.**

O fluxo obrigatório é:

```text
SUBSCRIBE NOTIFY
 ↓
STATE QUERY
 ↓
STATE RESPONSE válida
 ↓
SNAPSHOT
 ↓
somente então permitir alteração visual
```

Se não for possível obter um snapshot válido:

```text
NÃO ALTERAR O FAROL.
```

Registrar o problema e interromper o teste visual daquele dispositivo.

---

# 4. Referência do protocolo

A implementação open source atual do UniLED define o SP624E como:

```text
Protocol: BanlanX v3
Model: SP624E
Colors: RGBW
Internal microphone: false
```

Utilize como referência técnica:

```text
repository:
monty68/uniled

file:
custom_components/uniled/lib/ble/banlanx3.py
```

Não copie cegamente implementação Python.

Reimplemente corretamente em C/C++ usando:

```text
ESP-IDF 6.0.2
NimBLE
```

---

# 5. Comandos conhecidos

Implemente inicialmente APENAS estes comandos.

## Query de estado

```text
1D 00
```

---

## Power

ON:

```text
0F 01 01
```

OFF:

```text
0F 01 00
```

---

## Brightness RGB / global

Formato:

```text
12 01 LEVEL
```

onde:

```text
LEVEL = 0x00–0xFF
```

---

## Selecionar efeito

Formato:

```text
15 01 EFFECT
```

Solid RGB:

```text
15 01 63
```

White:

```text
15 01 CC
```

---

## RGB + nível

Formato:

```text
13 04 RR GG BB LEVEL
```

Exemplo vermelho a aproximadamente 25%:

```text
13 04 FF 00 00 40
```

---

## White brightness

Formato:

```text
21 02 LEVEL FF
```

---

## Effect Speed

Formato:

```text
14 01 SPEED
```

Valores esperados:

```text
1–10
```

---

## Light Mode

Formato:

```text
16 01 MODE
```

Valores conhecidos:

```text
00 = single effect
01 = auto dynamic
02 = auto sound
```

Para SP624E sem microfone interno, não assuma que todos os modos serão utilizados.

---

# 6. Criar módulo de protocolo

Criar algo conceitualmente semelhante a:

```text
main/sp624e/
├── sp624e_protocol.c
├── sp624e_protocol.h
├── sp624e_state.c
├── sp624e_state.h
├── sp624e_controller.c
├── sp624e_controller.h
└── sp624e_provisioning.c
```

Pode adaptar à arquitetura existente.

Separar claramente:

```text
BLE transport
≠
SP624E protocol
≠
state management
≠
provisioning
```

O código de protocolo não deve conhecer detalhes de UI futura.

---

# 7. Estrutura de estado

Criar estrutura semelhante a:

```c
typedef struct {
    bool valid;

    bool power;

    uint8_t brightness;
    uint8_t speed;
    uint8_t chip_order;
    uint8_t effect;
    uint8_t mode;

    uint8_t red;
    uint8_t green;
    uint8_t blue;

    uint8_t gain;
    uint8_t input;

    uint8_t white;

    uint8_t raw_payload[64];
    size_t raw_payload_len;

    int64_t received_at_ms;
} sp624e_light_state_t;
```

Ajustar tamanho e tipos conforme necessário.

---

# 8. Notifications

A FFE1 possui propriedade:

```text
NOTIFY
```

e descriptor:

```text
CCCD 0x2902
handle 0x0004
```

Após GATT discovery:

```text
FFE1 FOUND
CCCD FOUND
```

habilitar notifications.

O CCCD deve receber:

```text
01 00
```

para habilitar Notification.

Utilize a API correta do NimBLE disponível no ESP-IDF 6.0.2.

Não invente APIs.

Inspecione headers e exemplos instalados localmente quando necessário.

Depois registrar:

```text
[SP624E] Notifications enabled
Device: ...
FFE1: 0x0003
CCCD: 0x0004
```

---

# 9. Ordem obrigatória

Para cada controlador:

```text
CONNECT
 ↓
GATT DISCOVERY
 ↓
FFE1 FOUND
 ↓
CCCD FOUND
 ↓
ENABLE NOTIFY
 ↓
STATE QUERY
 ↓
WAIT RESPONSE
 ↓
PARSE RESPONSE
 ↓
STATE VALID
 ↓
READY_FOR_CONTROL
```

Criar um estado adicional:

```text
SP624E_STATE_READY_FOR_CONTROL
```

Ele somente pode ser atingido depois de uma resposta de estado válida.

---

# 10. State Query

Enviar:

```text
1D 00
```

pela característica FFE1.

Para os primeiros testes, prefira:

```text
WRITE WITH RESPONSE
```

já que a FFE1 confirmou suporte a WRITE.

Isso fornece confirmação GATT da operação.

Não utilizar `WRITE_NO_RESPONSE` nesta primeira implementação salvo se houver evidência de que o dispositivo exige.

Log:

```text
[SP624E_CMD] TX
Device: ...
Command: STATE_QUERY
Payload: 1D 00
```

Depois esperar uma notification.

Timeout inicial:

```text
2000 ms
```

Pode ajustar se os testes reais mostrarem necessidade.

---

# 11. Reassembly das notifications

BanlanX v3 pode fragmentar a resposta em múltiplas notifications.

Portanto NÃO assumir que:

```text
1 notification == 1 mensagem completa
```

Implementar reassembly.

Conceitualmente, os pacotes utilizam:

## Primeiro pacote

```text
BYTE 0 = packet number
BYTE 1 = total message length
BYTE 2 = payload length deste packet
BYTE 3... = payload
```

Quando:

```text
packet number == 1
```

iniciar nova mensagem.

---

## Pacotes seguintes

Conceitualmente:

```text
BYTE 0 = packet number
BYTE 1 = payload length deste packet
BYTE 2... = payload
```

Validar:

```text
packet_number atual == anterior + 1
```

Acumular até:

```text
payload_received == total_message_length
```

Se:

```text
payload_received > total
```

ou:

```text
packet fora de sequência
```

descartar mensagem e registrar erro.

Nunca fazer memcpy sem checagem de limites.

---

# 12. Parser do status

Quando a mensagem completa for remontada, interpretar o payload do BanlanX v3.

Campos conhecidos:

```text
payload[0] = power
payload[1] = brightness / level
payload[2] = speed
payload[3] = chip order
payload[4] = effect
payload[5] = mode

payload[6] = red
payload[7] = green
payload[8] = blue

payload[9] = gain
```

Os últimos campos contêm informações adicionais.

Na referência atual:

```text
third byte from end = audio/input
second byte from end = white/cold-white
last byte = warm-white / reservado dependendo do modelo
```

Para SP624E RGBW:

interpretar o penúltimo valor como:

```text
white
```

mas manter também o payload bruto para auditoria.

Não assumir que campos desconhecidos podem ser descartados.

---

# 13. Log do estado

Depois de receber e decodificar:

```text
========================================
SP624E STATE
========================================

Device:
Address:

Power:
Brightness:
Speed:
Chip order:
Effect:
Mode:

RGB:
R:
G:
B:

White:

Raw:
...

========================================
```

Exemplo:

```text
[SP624E_STATE] FF:FF:11:CD:A0:60
Power: ON
Brightness: 255
Effect: 0x63
Mode: 0
RGB: 255,0,0
White: 0
```

---

# 14. Snapshot

Antes de QUALQUER teste visual:

```text
snapshot = current_state
```

Manter snapshot separado por dispositivo.

```c
sp624e_light_state_t original_state;
```

O snapshot deve permanecer intacto até a restauração ser confirmada.

Não atualizar `original_state` quando o teste alterar temporariamente o dispositivo.

---

# 15. Funções de comando

Criar builders puros.

Exemplos conceituais:

```c
sp624e_build_state_query(...)
sp624e_build_power(...)
sp624e_build_brightness(...)
sp624e_build_rgb(...)
sp624e_build_effect(...)
sp624e_build_white(...)
sp624e_build_speed(...)
sp624e_build_mode(...)
```

Builders:

- não acessam BLE;
- apenas criam payload;
- validam valores;
- retornam erro para valores inválidos.

Separar:

```text
BUILD COMMAND
```

de:

```text
SEND COMMAND
```

---

# 16. Command sender

Criar função central:

```c
sp624e_send_command(...)
```

Ela deve:

1. validar conexão;
2. validar `READY_FOR_CONTROL`;
3. validar handle FFE1;
4. impedir duas operações GATT simultâneas no mesmo dispositivo;
5. usar WRITE WITH RESPONSE inicialmente;
6. registrar TX;
7. registrar resultado;
8. aplicar timeout;
9. retornar erro real.

Não disparar múltiplos writes concorrentes para o mesmo SP624E.

---

# 17. Nenhum loop agressivo

Não implementar:

```text
while (true) {
    write(...)
}
```

Comandos devem ocorrer somente:

- quando necessário;
- durante provisioning;
- durante teste controlado;
- posteriormente quando o usuário solicitar.

---

# 18. Restaurar estado

Implementar:

```c
sp624e_restore_state(...)
```

Essa função é crítica.

Deve tentar restaurar o estado observado ANTES do teste.

Considere:

```text
power
effect
mode
RGB
brightness
white
speed
```

A sequência pode depender do efeito.

## Solid RGB

Conceitualmente:

```text
select effect 0x63
set RGB + level
restore mode
restore power
```

---

## White

Conceitualmente:

```text
select effect 0xCC
set white brightness
restore mode
restore power
```

---

## Dynamic effect

Conceitualmente:

```text
select original effect
restore speed
restore mode
restore brightness quando aplicável
restore power
```

Não envie comandos desnecessários.

---

# 19. Verificação pós-restauração

Depois de restaurar:

```text
STATE_QUERY
```

novamente.

Comparar o novo estado com o snapshot.

Classificar:

```text
RESTORE_OK
RESTORE_MISMATCH
RESTORE_FAILED
```

Se mismatch:

fazer no máximo:

```text
1 retry
```

Depois consultar novamente.

Se ainda diferente:

```text
ABORTAR TESTES ADICIONAIS
```

e imprimir exatamente quais campos divergiram.

---

# 20. Primeira identificação visual

Quando:

```text
SP624E_1 == READY_FOR_CONTROL
SP624E_2 == READY_FOR_CONTROL
```

e snapshots válidos existirem para ambos:

começar provisionamento.

---

# 21. Teste visual do SP624E_1

Para evitar brilho excessivo, utilizar aproximadamente 25%.

Payload:

```text
15 01 63
```

seguido de:

```text
13 04 FF 00 00 40
```

Resultado esperado:

```text
SP624E_1 temporariamente vermelho
SP624E_2 permanece como estava
```

Manter por aproximadamente:

```text
2 segundos
```

Depois:

```text
RESTORE SP624E_1
STATE QUERY
VERIFY RESTORE
```

O farol deve sempre ser restaurado ANTES de pedir identificação ao usuário.

---

# 22. Identificação LEFT/RIGHT

Depois que SP624E_1 tiver sido restaurado com sucesso, o Codex pode fazer UMA pergunta física necessária:

```text
Qual lado ficou vermelho durante o teste do SP624E_1?

[L] Esquerdo
[R] Direito
```

Se o Codex conseguir receber a resposta interativamente, continuar.

Se não conseguir:

criar um modo de provisioning serial ou fornecer instrução clara e parar neste ponto sem inventar LEFT/RIGHT.

NUNCA deduzir LEFT/RIGHT pelo RSSI.

---

# 23. Inferência do segundo

Se:

```text
SP624E_1 = LEFT
```

então:

```text
SP624E_2 = RIGHT
```

Se:

```text
SP624E_1 = RIGHT
```

então:

```text
SP624E_2 = LEFT
```

Mas ainda devemos validar visualmente o segundo.

---

# 24. Validação SP624E_2

Realizar teste com azul:

```text
15 01 63
```

seguido de:

```text
13 04 00 00 FF 40
```

Manter:

```text
2 segundos
```

Depois:

```text
RESTORE
STATE QUERY
VERIFY
```

Solicitar confirmação simples de que o lado observado correspondeu ao lado inferido.

Se não corresponder:

```text
NÃO persistir mapping.
```

Investigar.

---

# 25. Persistência LEFT / RIGHT

Depois da validação dos dois lados, persistir mapping usando:

```text
ESP-IDF NVS
```

Namespace sugerido:

```text
sp624e
```

Dados:

```text
mapping_version
left_address
left_address_type
right_address
right_address_type
```

Pode armazenar como blob de 6 bytes para endereço.

Não armazenar apenas string quando uma representação binária for mais adequada.

---

# 26. Modelo de configuração

Criar algo semelhante a:

```c
typedef struct {
    bool valid;

    ble_addr_t left;
    ble_addr_t right;

    uint32_t version;
} sp624e_mapping_t;
```

---

# 27. Inicialização futura

No boot seguinte:

```text
NVS
 ↓
mapping existe?
```

Se sim:

```text
load LEFT
load RIGHT
```

Depois, durante scan:

```text
encontrou address LEFT → controller LEFT
encontrou address RIGHT → controller RIGHT
```

Não depender mais da ordem em que os advertising packets aparecem.

---

# 28. Logs do mapping

Exemplo:

```text
========================================
SP624E MAPPING
========================================

LEFT
FF:FF:11:CD:A0:60

RIGHT
FF:FF:11:CD:AC:FA

Source:
NVS

========================================
```

Os valores reais dependerão do teste.

NÃO hardcode qual endereço é LEFT antes da confirmação física.

---

# 29. Primeiro teste sincronizado

Somente executar se:

```text
LEFT known
RIGHT known

LEFT READY_FOR_CONTROL
RIGHT READY_FOR_CONTROL

snapshot LEFT valid
snapshot RIGHT valid
```

---

# 30. Estado temporário do teste

Usar cor claramente visível, porém com brilho baixo/moderado.

Sugestão:

```text
GREEN
brightness 25%
```

Para cada dispositivo:

```text
15 01 63
13 04 00 FF 00 40
```

---

# 31. Aplicar nos dois

Enviar para:

```text
LEFT
RIGHT
```

com a menor diferença temporal razoável.

Registrar timestamp monotônico antes/depois de cada comando.

Exemplo:

```text
[SYNC_TEST]
LEFT RGB write:  123456789 us
RIGHT RGB write: 123460221 us

delta: 3432 us
```

Não é necessário atingir sincronização de microssegundos.

O objetivo é apenas medir e conhecer a diferença.

---

# 32. Manter temporariamente

Deixar ambos verdes por aproximadamente:

```text
2 segundos
```

Depois restaurar os dois.

---

# 33. Restaurar sincronizado

Restaurar:

```text
LEFT → original snapshot LEFT
RIGHT → original snapshot RIGHT
```

Depois consultar novamente ambos.

Resultado obrigatório:

```text
LEFT RESTORE_OK
RIGHT RESTORE_OK
```

Caso um falhe:

```text
retry de restauração apenas do lado que falhou
```

Não deixar deliberadamente os dois em estados diferentes.

---

# 34. Estado desejado x estado observado

Começar a estruturar dois conceitos distintos.

## Observed State

O que foi recebido efetivamente do controlador.

```text
observed_state
```

## Desired State

O que o sistema quer que os dois controladores estejam exibindo.

```text
desired_state
```

Nesta etapa o `desired_state` ainda não precisa comandar automaticamente o sistema.

Mas a arquitetura deve estar preparada para a próxima etapa.

---

# 35. Não implementar reconciliação automática ainda

Não fazer ainda:

```text
if observed != desired:
    write continuously
```

A próxima etapa implementará:

```text
State Reconciler
Reconnection Manager
Command Queue
```

Nesta etapa apenas introduzir os conceitos.

---

# 36. Diagnóstico de notifications

Para cada notification:

registrar em DEBUG:

```text
device
conn_handle
attr_handle
packet number
raw bytes
```

Depois da reassembly:

```text
complete message
decoded state
```

Não deixar logs extremamente verbosos habilitados em INFO permanentemente.

---

# 37. Proteção contra dispositivo errado

Antes de enviar qualquer comando, validar:

```text
manufacturer signature previously confirmed
AND
FFE0 found
AND
FFE1 found
AND
address belongs to registered SP624E
AND
state query succeeded
```

Se qualquer condição falhar:

```text
CONTROL DENIED
```

---

# 38. Limite de writes

Durante testes:

não enviar mais que o necessário.

Adicionar intervalo pequeno quando dois comandos dependentes forem enviados em sequência.

Não bombardear o SP624E.

Inicialmente pode utilizar algo como:

```text
30–100 ms
```

entre comandos dependentes caso testes revelem necessidade.

Não adicionar delays arbitrariamente enormes.

---

# 39. Testes unitários

Criar testes onde razoável para:

- command builders;
- bounds;
- state parser;
- fragmented notification reassembly;
- NVS mapping serialization;
- state comparison.

Não exigir hardware para testar lógica pura.

Exemplos:

```text
state query → 1D 00

power ON → 0F 01 01

power OFF → 0F 01 00

solid → 15 01 63

red 25% → 13 04 FF 00 00 40
```

---

# 40. Teste de parser

Criar fixtures sintéticas para:

```text
single-packet state response
multi-packet state response
packet out of order
packet duplicated
payload oversized
payload truncated
```

Nenhum dado malformado deve causar:

```text
buffer overflow
panic
Guru Meditation
```

---

# 41. NVS

Inicializar NVS corretamente.

Tratar:

```text
ESP_ERR_NVS_NO_FREE_PAGES
ESP_ERR_NVS_NEW_VERSION_FOUND
```

quando aplicável conforme padrão ESP-IDF.

Não apagar NVS automaticamente por qualquer erro genérico.

---

# 42. Reset de mapping

Criar função interna:

```c
sp624e_mapping_clear()
```

Não precisa ainda expor por UI.

Será usada posteriormente em provisioning.

---

# 43. Firmware version

Atualizar firmware para:

```text
0.3.0
```

Boot esperado:

```text
SP624E Controller
Firmware: 0.3.0
```

---

# 44. Startup summary

Depois das conexões e leitura de NVS:

```text
========================================
SP624E CONTROLLER 0.3.0
========================================

LEFT:
address:
connection:
state:

RIGHT:
address:
connection:
state:

Mapping:
PROVISIONED / NOT PROVISIONED

Notifications:
LEFT:
RIGHT:

Control:
READY / NOT READY
========================================
```

---

# 45. Atualizar documentação

Atualizar:

```text
README.md
AGENTS.md
docs/ble-discovery.md
```

Criar:

```text
docs/sp624e-protocol.md
docs/provisioning.md
```

---

# 46. sp624e-protocol.md

Documentar separadamente:

## Reference-derived

Informações obtidas do UniLED:

```text
STATE QUERY
POWER
BRIGHTNESS
RGB
EFFECT
WHITE
SPEED
MODE
```

## Hardware-confirmed

Após teste real:

```text
command worked?
notification worked?
actual response bytes?
state decode?
restore worked?
```

Nunca misturar:

```text
assumption
```

com:

```text
confirmed experimentally
```

---

# 47. provisioning.md

Registrar:

```text
SP624E_1 address
lado físico confirmado

SP624E_2 address
lado físico confirmado

data do provisionamento
firmware utilizado
```

Não registrar estado temporário como estado permanente.

---

# 48. Build

Executar:

```powershell
.\scripts\build.ps1
```

ou equivalente.

Build limpo obrigatório.

Resolver todos os warnings introduzidos.

---

# 49. Flash

Gravar fisicamente no ESP32.

Usar:

```powershell
.\scripts\flash-monitor.ps1
```

ou:

```powershell
.\scripts\flash-monitor.ps1 -ManualBoot
```

se necessário.

---

# 50. Teste físico obrigatório — Fase A

Antes de qualquer mudança visual:

validar para os DOIS:

```text
CONNECTED
FFE1 FOUND
NOTIFY ENABLED
STATE QUERY TX
NOTIFICATION RX
STATE PARSED
SNAPSHOT SAVED
READY_FOR_CONTROL
```

Se algum não atingir isso:

```text
ABORT VISUAL TEST
```

---

# 51. Teste físico obrigatório — Fase B

SP624E_1:

```text
temporary RED 25%
hold ~2 sec
restore
state query
restore verified
```

Usuário identifica LEFT/RIGHT.

---

# 52. Teste físico obrigatório — Fase C

SP624E_2:

```text
temporary BLUE 25%
hold ~2 sec
restore
verify
```

Confirmar mapping.

Persistir NVS.

---

# 53. Teste físico obrigatório — Fase D

Ambos:

```text
temporary GREEN 25%
apply both
hold ~2 sec
restore both
verify both
```

Registrar diferença temporal dos writes.

---

# 54. Segurança de rollback

Se qualquer etapa falhar depois de alterar um farol:

a prioridade número 1 passa a ser:

```text
RESTORE ORIGINAL STATE
```

antes de:

- logs;
- documentação;
- novos testes;
- retries de conexão.

---

# 55. Se conexão cair durante teste

Se um controlador desconectar enquanto está no estado temporário:

```text
não alterar o outro novamente
```

Registrar:

```text
RESTORE_PENDING
```

Fazer uma tentativa controlada de reconectar aquele dispositivo.

Se reconectar:

```text
GATT discovery se necessário
enable notifications
restore snapshot
verify
```

Não implementar ainda loop infinito.

No máximo algumas tentativas controladas para recuperação do teste.

---

# 56. Não deixar teste rodando sozinho indefinidamente

Todos os testes visuais devem:

```text
ter timeout
ter restore
ter saída
```

Nunca deixar:

```text
while(true) color_test();
```

---

# 57. Relatório final

Ao concluir, apresentar exatamente:

```text
SP624E CONTROL & PROVISIONING REPORT
===================================

Firmware
--------
Version:
Build:
Flash:
Boot:

BLE
---
LEFT connection:
RIGHT connection:
Simultaneous:

Notifications
-------------
LEFT CCCD:
LEFT notify:
RIGHT CCCD:
RIGHT notify:

Initial State Query
-------------------
LEFT raw response:
LEFT decoded:

RIGHT raw response:
RIGHT decoded:

SP624E_1 Visual Test
--------------------
Address:
Temporary color:
Command result:
Restore:
Physical side:

SP624E_2 Visual Test
--------------------
Address:
Temporary color:
Command result:
Restore:
Physical side:

Persisted Mapping
-----------------
LEFT:
RIGHT:
NVS:
Mapping version:

Synchronized Test
-----------------
Temporary color:
LEFT write:
RIGHT write:
Write delta:
Both changed:
LEFT restored:
RIGHT restored:

Protocol Hardware Confirmation
------------------------------
1D 00 state query:
0F 01 power:
15 01 63 solid:
13 04 RGB:
12 01 brightness:
21 02 white:
14 01 speed:
16 01 mode:

Mark each:
CONFIRMED
NOT_TESTED
FAILED

Safety
------
Original LEFT restored:
Original RIGHT restored:
Unexpected persistent changes:
Disconnects:
Panic:
Watchdog:
Unexpected reboot:

Conclusion
----------

Open Issues
-----------

Recommended Next Step
---------------------
```

---

# 58. Critérios de aceite

A tarefa somente pode ser considerada completamente concluída quando:

- [ ] FFE1 notifications estiverem habilitadas;
- [ ] state query `1D 00` funcionar;
- [ ] response fragmentation estiver tratada;
- [ ] state parser estiver funcionando;
- [ ] snapshot dos dois dispositivos for obtido;
- [ ] nenhum visual test ocorrer sem snapshot;
- [ ] SP624E_1 responder a primeiro teste real;
- [ ] SP624E_1 for restaurado;
- [ ] LEFT/RIGHT for identificado fisicamente;
- [ ] SP624E_2 for validado visualmente;
- [ ] SP624E_2 for restaurado;
- [ ] mapping LEFT/RIGHT for persistido em NVS;
- [ ] mapping sobreviver a reboot;
- [ ] primeiro teste sincronizado nos dois funcionar;
- [ ] ambos forem restaurados após teste;
- [ ] nenhum panic ocorrer;
- [ ] nenhum watchdog ocorrer;
- [ ] documentação for atualizada;
- [ ] relatório final contiver bytes reais recebidos do hardware.

---

# 59. Se não for possível completar fisicamente

Não simular.

Marcar claramente:

```text
BLOCKED_BY_HARDWARE
```

ou:

```text
BLOCKED_BY_USER_CONFIRMATION
```

conforme apropriado.

Compile e valide tudo que puder antes disso.

---

# 60. Próxima arquitetura

Não implementar ainda, mas deixar o código preparado para a próxima etapa:

```text
                  Desired State
                       │
              State Reconciler
                       │
            ┌──────────┴──────────┐
            ▼                     ▼
      LEFT Command Queue    RIGHT Command Queue
            │                     │
            ▼                     ▼
          LEFT BLE             RIGHT BLE
            │                     │
            ▼                     ▼
         SP624E                 SP624E
```

Próxima etapa, após este goal:

```text
Connection Manager definitivo
Reconnection Manager
Command Queue
Desired State
State Reconciliation
automatic resync
connection health
long-running stability test
```

Ainda sem PWA.

---

# 61. Regra final

Não apenas implementar os comandos.

**Prove no hardware.**

A meta desta etapa é sair de:

```text
ESP32 consegue conectar aos dois SP624E
```

para:

```text
ESP32 consegue:
- receber estado real
- controlar cada lado
- saber qual é LEFT/RIGHT
- restaurar o estado
- controlar os dois juntos
```

Somente depois disso o protocolo será considerado suficientemente validado para iniciar o gerenciador automático de reconexão e sincronização.