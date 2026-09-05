# GOAL — Connection Manager + Auto Reconnect + Command Queues + State Reconciler + Stability Test

## 1. Objetivo

Evoluir o firmware `0.3.0` existente para uma arquitetura robusta capaz de manter os dois SP624E sincronizados durante uso prolongado.

O objetivo principal deste projeto é resolver o problema observado no aplicativo BanlanX:

```text
um dos SP624E perde conexão
↓
um farol deixa de receber comandos
↓
os dois lados ficam com estados diferentes
```

O ESP32 deve resolver isso automaticamente.

Ao final desta tarefa, o comportamento desejado deve ser:

```text
                    DESIRED STATE
                         │
                  GROUP CONTROLLER
                         │
              ┌──────────┴──────────┐
              │                     │
              ▼                     ▼
          LEFT QUEUE           RIGHT QUEUE
              │                     │
              ▼                     ▼
          LEFT BLE              RIGHT BLE
              │                     │
              ▼                     ▼
          SP624E LEFT           SP624E RIGHT
```

Se uma conexão cair:

```text
DISCONNECT
↓
detecção imediata
↓
reconnect automático
↓
GATT discovery
↓
notifications
↓
state query
↓
comparação com Desired State
↓
correção somente se necessário
↓
state query
↓
verificação
↓
SYNCED
```

Ainda NÃO implementar:

- PWA;
- Wi-Fi;
- HTTP;
- WebSocket;
- receptor 433 MHz;
- OTA;
- sincronização musical.

---

# 2. Contexto confirmado

Projeto:

```text
C:\Projetos\ESP32
```

Firmware atual:

```text
0.3.0
```

Hardware:

```text
ESP32-D0WD-V3
Revision v3.1
4 MB flash
dual-core
ESP-IDF 6.0.2
NimBLE
BLE-only
máximo configurado: 3 conexões
```

Mapping persistido:

```text
LEFT
FF:FF:11:CD:AC:FA

RIGHT
FF:FF:11:CD:A0:60
```

SP624E:

```text
Protocol: BanlanX v3
Service: FFE0
Characteristic: FFE1
Properties:
- WRITE
- WRITE_NO_RESPONSE
- NOTIFY

CCCD:
0x2902
```

Já comprovado fisicamente:

```text
State Query       1D 00       CONFIRMED
Solid Effect      15 01 63    CONFIRMED
RGB               13 04 ...   CONFIRMED
Brightness        12 01 ...   CONFIRMED
Notifications                 CONFIRMED
Fragmentation                 CONFIRMED
NVS mapping                   CONFIRMED
Simultaneous BLE              CONFIRMED
State restore                 CONFIRMED
```

---

# 3. Não quebrar funcionalidades existentes

Preservar:

- BLE scanner;
- identificação estrutural SP624E;
- registry;
- GATT discovery;
- FFE0/FFE1;
- notifications;
- fragment reassembly;
- state query;
- state parser;
- LEFT/RIGHT provisioning;
- NVS;
- state snapshot;
- state restore;
- scripts PowerShell;
- documentação existente.

Não reescrever módulos funcionais sem necessidade comprovada.

Preferir refatoração incremental.

---

# 4. Firmware

Atualizar para:

```text
0.4.0
```

Boot:

```text
SP624E Controller
Firmware: 0.4.0
```

---

# 5. Arquitetura

Implementar aproximadamente:

```text
main/
├── ble/
│   ├── ble_scanner.*
│   ├── ble_registry.*
│   ├── ble_connection.*
│   └── ble_connection_manager.*
│
├── sp624e/
│   ├── sp624e_protocol.*
│   ├── sp624e_state.*
│   ├── sp624e_controller.*
│   ├── sp624e_mapping.*
│   └── sp624e_command_queue.*
│
├── sync/
│   ├── desired_state.*
│   ├── state_reconciler.*
│   └── group_controller.*
│
└── diagnostics/
    ├── connection_metrics.*
    └── runtime_status.*
```

Adapte nomes à arquitetura existente quando houver uma solução melhor.

Não criar arquivos vazios ou abstrações sem uso real.

---

# 6. Máquina de estados da conexão

Cada SP624E deverá possuir uma máquina de estados independente.

Estados conceituais:

```text
UNKNOWN
DISCONNECTED
WAITING_FOR_ADV
CONNECTING
CONNECTED
DISCOVERING
SUBSCRIBING
QUERYING_STATE
RECONCILING
READY
BACKOFF
ERROR
```

Utilizar enum apropriado.

Toda transição deve possuir motivo.

Exemplo:

```text
READY
↓ GAP disconnect
DISCONNECTED
↓
BACKOFF
↓
WAITING_FOR_ADV
↓
CONNECTING
↓
DISCOVERING
↓
SUBSCRIBING
↓
QUERYING_STATE
↓
RECONCILING
↓
READY
```

---

# 7. Connection Manager

Criar um Connection Manager responsável por:

- LEFT;
- RIGHT;
- conexão;
- desconexão;
- retries;
- backoff;
- GATT discovery;
- notification subscription;
- state query após conexão;
- métricas;
- informar readiness ao Group Controller.

Ele NÃO deve decidir cor ou brilho.

Connection Manager cuida apenas da conectividade.

---

# 8. Nunca fazer reconnect loop agressivo

PROIBIDO:

```c
while (!connected) {
    connect();
}
```

Implementar backoff.

Sugestão inicial:

```text
attempt 1 → 500 ms
attempt 2 → 1 s
attempt 3 → 2 s
attempt 4 → 4 s
attempt 5 → 8 s
demais    → 10 s
```

Adicionar pequeno jitter quando razoável:

```text
± 10%
```

para evitar que LEFT e RIGHT façam tentativas perfeitamente simultâneas continuamente.

---

# 9. Reset do backoff

Depois que um controlador permanecer READY por período razoável, por exemplo:

```text
10 segundos
```

resetar:

```text
reconnect_attempt = 0
backoff = inicial
```

Não carregar para sempre penalidade de uma falha antiga.

---

# 10. Eventos devem dirigir reconexão

A reconexão normal deve ser disparada por:

```text
GAP disconnect
connection establishment failure
GATT failure grave
health check failure confirmado
peripheral encontrado novamente
```

NÃO desconectar/reconectar simplesmente porque o RSSI está baixo.

RSSI baixo é diagnóstico.

Não é por si só falha.

---

# 11. Scanner durante reconexão

Como os endereços LEFT/RIGHT já estão persistidos:

```text
LEFT  = FF:FF:11:CD:AC:FA
RIGHT = FF:FF:11:CD:A0:60
```

não realizar scan global contínuo sem necessidade.

No boot:

```text
scan
↓
localizar endereços conhecidos
↓
conectar
```

Durante desconexão de um lado:

```text
procurar especificamente o endereço ausente
```

Quando ambos estiverem READY:

```text
scanner deve ficar parado
```

a menos que exista motivo técnico claro.

---

# 12. Não perder a conexão saudável

Exemplo:

```text
LEFT desconecta
RIGHT continua READY
```

O firmware NÃO deve desconectar RIGHT apenas para reconstruir tudo.

Resultado:

```text
LEFT  → recovering
RIGHT → continua conectado
```

---

# 13. Recovery pipeline

Quando LEFT ou RIGHT reconectar:

```text
CONNECTED
↓
GATT discovery
↓
FFE0
↓
FFE1
↓
CCCD
↓
enable notifications
↓
state query
↓
state valid
↓
reconcile
↓
verify
↓
READY
```

Nunca considerar:

```text
BLE CONNECTED
```

equivalente a:

```text
READY
```

---

# 14. Command Queue independente

Cada controlador deve possuir uma fila independente:

```text
LEFT COMMAND QUEUE

RIGHT COMMAND QUEUE
```

Regra fundamental:

```text
um único GATT write ativo por controlador
```

Não disparar operações concorrentes na mesma FFE1.

---

# 15. Estrutura de comando

Criar estrutura semelhante a:

```c
typedef struct {
    uint32_t id;
    uint32_t generation;

    sp624e_command_type_t type;

    uint8_t payload[...];
    size_t payload_len;

    int retry_count;

    bool requires_verification;
} sp624e_command_t;
```

---

# 16. Generation ID

Toda alteração do Desired State deve incrementar:

```text
generation
```

Exemplo:

```text
generation 100 → vermelho
generation 101 → azul
generation 102 → branco
```

Comandos antigos na fila devem poder ser descartados quando forem substituídos por uma geração mais nova.

Isto será muito importante futuramente quando a PWA tiver sliders.

Exemplo:

```text
brightness 10
brightness 20
brightness 30
brightness 40
...
```

não precisamos necessariamente aplicar todos.

Precisamos chegar corretamente ao último estado desejado.

---

# 17. Coalescing

Para comandos que representam estado substituível, permitir coalescing.

Exemplo:

```text
RGB #1
RGB #2
RGB #3
```

Se nenhum ainda foi enviado:

```text
descartar #1 e #2
manter #3
```

Não aplicar isso a qualquer comando indiscriminadamente.

---

# 18. Desired State

Criar estado global representando:

> O que os DOIS faróis deveriam estar exibindo.

Conceitualmente:

```c
typedef struct {
    bool valid;

    uint32_t generation;

    bool power;

    uint8_t effect;

    uint8_t mode;

    uint8_t red;
    uint8_t green;
    uint8_t blue;

    uint8_t brightness;
    uint8_t white;
    uint8_t speed;
} sp624e_desired_state_t;
```

Nesta etapa somente utilizar automaticamente os campos cujo comportamento estiver confirmado.

Não inventar suporte para comandos ainda não testados.

---

# 19. Observed State

Cada controlador continua com seu próprio:

```text
observed_state
```

Portanto:

```text
desired_state

LEFT observed_state
RIGHT observed_state
```

são entidades diferentes.

---

# 20. Estado de sincronização do grupo

Criar estado:

```text
UNINITIALIZED
SYNCED
DEGRADED
RECONCILING
UNSYNCED
ERROR
```

Exemplo:

```text
LEFT READY
RIGHT READY
ambos confirmam generation 50
→ SYNCED
```

---

# 21. Applied/Verified Generation

Cada controlador deve possuir:

```text
last_applied_generation
last_verified_generation
```

O grupo somente será:

```text
SYNCED
```

quando:

```text
LEFT.last_verified_generation  == desired.generation
AND
RIGHT.last_verified_generation == desired.generation
```

---

# 22. Strict Group Mode

Implementar por padrão:

```text
STRICT_SYNC_MODE = true
```

Quando um NOVO comando de usuário chegar e apenas um controlador estiver READY:

```text
NÃO aplicar apenas naquele lado.
```

Exemplo:

```text
LEFT READY
RIGHT reconnecting

usuário pede BLUE
```

Resultado correto:

```text
desired_state = BLUE
generation++

LEFT continua no estado atual temporariamente
RIGHT continua reconnecting

quando ambos READY:
    aplicar BLUE aos dois
```

Isso evita o problema original:

```text
um lado recebe comando
outro não
```

---

# 23. Exceção importante: recovery

Se nenhum novo comando chegou e um lado simplesmente caiu:

```text
desired = GREEN

LEFT GREEN / READY
RIGHT desconecta
```

NÃO alterar LEFT.

Quando RIGHT voltar:

```text
query RIGHT
↓
se RIGHT != GREEN
↓
aplicar GREEN somente em RIGHT
↓
verificar
```

Depois:

```text
LEFT verify
RIGHT verify
↓
SYNCED
```

---

# 24. Mid-transaction failure

Considere:

```text
novo desired = BLUE

write LEFT → sucesso
write RIGHT → conexão cai
```

Agora existe temporariamente:

```text
LEFT BLUE
RIGHT estado antigo
```

O sistema deve:

```text
GROUP = DEGRADED
```

manter:

```text
desired = BLUE
```

e priorizar reconexão de RIGHT.

Assim que RIGHT voltar:

```text
reaplicar BLUE
↓
verify RIGHT
↓
verify LEFT
↓
SYNCED
```

Registrar quanto tempo o grupo ficou visualmente potencialmente divergente.

---

# 25. State Reconciler

Criar:

```text
State Reconciler
```

Responsabilidade:

```text
Observed State
vs
Desired State
```

Determinar se algo precisa ser corrigido.

---

# 26. Não escrever se já estiver correto

Se:

```text
observed == desired
```

não enviar comandos redundantes.

Log:

```text
RECONCILE: already synchronized
```

---

# 27. Reconciliation mínimo

Quando possível, enviar somente os comandos necessários.

Exemplo:

```text
desired:
RGB 255,0,0
brightness 64

observed:
RGB 255,0,0
brightness 32
```

Se o protocolo permitir:

```text
alterar apenas brightness
```

Não reenviar toda a configuração sem necessidade.

---

# 28. Hardware-confirmed commands

Pode utilizar automaticamente nesta etapa:

```text
1D 00
state query

15 01 63
solid mode

13 04 R G B LEVEL
RGB

12 01 LEVEL
brightness
```

Continuar tratando:

```text
power
white
speed
mode
```

como não completamente validados para transições isoladas.

Não utilizar automaticamente se não for necessário ao teste.

---

# 29. Verification

Depois de reconciliação:

```text
STATE QUERY
```

e verificar.

Não considerar:

```text
GATT write success
```

como confirmação suficiente de estado.

Precisamos:

```text
WRITE
↓
NOTIFICATION / STATE QUERY
↓
PARSED STATE
↓
COMPARE
```

---

# 30. Retry de comando

Um write individual pode ter:

```text
1 retry
```

quando houver falha transitória e conexão ainda parecer válida.

Não fazer retries infinitos.

Se falhar novamente:

```text
mark connection unhealthy
```

e deixar Connection Manager tomar a decisão.

---

# 31. Timeout

Todos os estados assíncronos precisam de timeout:

- connect;
- discovery;
- CCCD;
- write;
- state query;
- reconciliation.

Nenhuma operação pode ficar esperando indefinidamente.

Definir valores razoáveis e documentá-los.

---

# 32. Health Check

Com os dois READY, implementar health check leve.

Sugestão inicial:

```text
STATE QUERY a cada 10 segundos
```

para cada controlador.

Pode escalonar:

```text
LEFT  t=0
RIGHT t=500ms
```

para evitar duas operações exatamente juntas.

Não consultar centenas de vezes por segundo.

---

# 33. Heartbeat não deve reenviar cor

PROIBIDO utilizar heartbeat assim:

```text
a cada 5 segundos:
    send RGB
```

Heartbeat deve preferencialmente:

```text
STATE QUERY
```

e somente corrigir estado se houver divergência real.

---

# 34. Detectar alteração externa

Um caso importante:

```text
ESP32 conectado
↓
outro dispositivo altera o SP624E
```

Se notification ou health query indicar:

```text
observed != desired
```

registrar:

```text
EXTERNAL_STATE_CHANGE
```

Nesta versão:

se o grupo possuir Desired State válido e estiver em modo de autoridade:

```text
reconciliar de volta ao Desired State
```

Mas evitar loops rápidos.

Aplicar debounce/cooldown curto quando necessário.

---

# 35. Desired State inicial

No boot, existem três casos.

## Caso A — Desired State persistido e válido

Carregar.

Mas nesta versão:

```text
restore_on_boot = false
```

por padrão.

Não alterar automaticamente os faróis apenas porque o ESP32 reiniciou.

Ainda estamos em fase de validação.

---

## Caso B — Sem Desired State persistido

Consultar LEFT e RIGHT.

Se ambos possuírem estado visual equivalente:

```text
adotar esse estado como Desired State inicial
```

sem enviar nenhum comando.

---

## Caso C — LEFT e RIGHT diferentes

Não escolher arbitrariamente.

Marcar:

```text
GROUP_UNSYNCED
```

e registrar os dois estados.

Não modificar automaticamente nesta versão.

---

# 36. Persistência

Preparar NVS para:

```text
desired_state
desired_state_version
restore_on_boot
```

Não gravar a cada pequena alteração.

Usar debounce.

Somente persistir um Desired State normal quando:

```text
LEFT verified
AND
RIGHT verified
AND
GROUP == SYNCED
```

---

# 37. Testes temporários não são persistidos

Comandos executados pelo harness de teste:

```text
temporary/test
```

não devem virar automaticamente o estado persistido.

Isso evita gravar:

```text
verde de teste
```

como configuração definitiva.

---

# 38. Métricas

Criar métricas por controlador.

No mínimo:

```text
connect_attempts
successful_connections
disconnect_count
reconnect_success_count
reconnect_failure_count

last_disconnect_reason
last_disconnect_timestamp

last_connect_timestamp

current_connection_uptime

total_connected_time

current_backoff_ms

last_rssi

state_query_count
state_query_failures

reconcile_count
reconcile_failures

command_count
command_failures

queue_depth
```

---

# 39. Métricas do grupo

Registrar:

```text
group_sync_count
group_desync_count
group_degraded_count

last_sync_time

current_desync_duration
max_desync_duration

desired_generation
left_verified_generation
right_verified_generation
```

---

# 40. Runtime Status

A cada aproximadamente:

```text
10 segundos
```

imprimir resumo compacto:

```text
========================================
SP624E RUNTIME STATUS
========================================

GROUP
state: SYNCED
generation: 42

LEFT
state: READY
connected: YES
RSSI: -XX
uptime: 125s
queue: 0
verified generation: 42
reconnects: 1

RIGHT
state: READY
connected: YES
RSSI: -XX
uptime: 118s
queue: 0
verified generation: 42
reconnects: 2

========================================
```

Não imprimir centenas de linhas por segundo.

---

# 41. RSSI

RSSI atual observado historicamente foi baixo.

Continuar medindo.

Entretanto:

```text
RSSI baixo ≠ disconnect
```

Não iniciar reconnect exclusivamente por:

```text
RSSI < valor arbitrário
```

Registrar apenas para diagnóstico.

---

# 42. Connection Parameters

Registrar parâmetros BLE negociados quando possível:

```text
connection interval
latency
supervision timeout
```

Não alterar parâmetros agressivamente sem necessidade.

Primeiro medir os valores utilizados pelos SP624E.

Se posteriormente houver evidência de instabilidade associada aos parâmetros, documentar antes de modificar.

---

# 43. Test Harness

Criar um pequeno mecanismo de testes no firmware.

Idealmente via console serial.

Pode usar:

```text
esp_console
```

ou solução equivalente apropriada ao ESP-IDF 6.0.2.

Comandos desejados:

```text
status

state

metrics

resync

rgb <r> <g> <b> <brightness>

disconnect left

disconnect right

reconnect left

reconnect right

test-reconnect left

test-reconnect right

test-sync
```

Se `esp_console` adicionar complexidade excessiva, implementar outra interface serial simples e documentada.

---

# 44. `status`

Exemplo:

```text
> status

GROUP: SYNCED

LEFT:
READY
FF:FF:11:CD:AC:FA

RIGHT:
READY
FF:FF:11:CD:A0:60
```

---

# 45. `rgb`

Exemplo:

```text
> rgb 255 0 0 64
```

Isso NÃO deve mandar diretamente para LEFT e RIGHT.

Deve:

```text
update Desired State
↓
generation++
↓
Group Controller
↓
queues
↓
verification
```

Assim o console utiliza exatamente a arquitetura futura da PWA.

---

# 46. `disconnect left/right`

Usar uma terminação BLE controlada através da API NimBLE correta disponível no ESP-IDF 6.0.2.

Objetivo:

```text
simular perda da conexão
```

sem desligar fisicamente o SP624E.

Depois:

```text
Connection Manager deve recuperar automaticamente.
```

---

# 47. Software Reconnect Test

Implementar:

```text
test-reconnect left
```

Fluxo:

```text
confirmar SYNCED
↓
snapshot
↓
software disconnect LEFT
↓
detectar disconnect
↓
auto reconnect
↓
GATT discovery
↓
notifications
↓
state query
↓
reconcile
↓
verify
↓
SYNCED
```

Medir:

```text
disconnect → READY
disconnect → SYNCED
```

---

# 48. Alternating Reconnect Stress Test

Realizar automaticamente:

```text
5 ciclos LEFT
5 ciclos RIGHT
```

alternados.

Exemplo:

```text
LEFT disconnect
recover
SYNCED

RIGHT disconnect
recover
SYNCED

LEFT disconnect
...
```

Nunca iniciar próximo ciclo antes do grupo voltar a:

```text
SYNCED
```

Intervalo razoável entre ciclos.

Não bombardear o hardware.

---

# 49. Critério do stress test

Para cada ciclo registrar:

```text
cycle
side
disconnect reason
reconnect attempts
time to connected
time to READY
time to SYNCED
commands required
verification result
```

Exemplo:

```text
Cycle 4
Side: RIGHT
Disconnect detected: 0 ms
Connected: 1240 ms
READY: 1580 ms
SYNCED: 1695 ms
Attempts: 1
Result: PASS
```

---

# 50. Teste de perda durante comando

Criar teste controlado.

Fluxo:

```text
desired = RED
```

Durante aplicação controlada, forçar disconnect de um lado em um ponto seguro do teste.

Queremos provar que:

```text
grupo entra DEGRADED
↓
desired continua RED
↓
lado ausente reconecta
↓
reconcile
↓
ambos RED
↓
SYNCED
```

Depois restaurar o estado original.

O teste NÃO pode deixar os faróis permanentemente alterados.

---

# 51. Teste físico opcional de power-cycle

Depois dos testes por software, se houver maneira prática de desligar apenas um SP624E fisicamente, solicitar uma ação do usuário:

```text
Desligue somente o SP624E LEFT agora.
```

Detectar desaparecimento.

Depois:

```text
Ligue novamente.
```

O firmware deve:

```text
encontrar
conectar
descobrir GATT
subscribe
query
reconcile
verify
SYNCED
```

Se não for possível desligar individualmente:

```text
SKIPPED_PHYSICAL_POWER_CYCLE
```

Não bloquear sucesso geral da tarefa.

---

# 52. Tempo de recuperação

Não hardcode promessa de recuperação instantânea.

Medir.

Objetivo de bancada:

```text
reconectar e sincronizar automaticamente
sem intervenção manual
```

Registrar tempo real.

Se após o dispositivo voltar a anunciar não houver recuperação em:

```text
30 segundos
```

considerar falha do teste e investigar.

---

# 53. Long-running Stability Test

Depois dos testes de reconnect:

executar teste contínuo de pelo menos:

```text
15 minutos
```

com:

```text
LEFT conectado
RIGHT conectado
health checks ativos
State Reconciler ativo
```

Durante esse período:

- não alterar cores continuamente;
- observar disconnects;
- observar memory usage;
- observar heap;
- observar queue depth;
- observar watchdog;
- observar stack;
- observar reconnects.

---

# 54. Monitorar heap

Registrar periodicamente:

```text
free heap
minimum free heap
```

Não deve existir queda contínua indicativa de memory leak.

Exemplo:

```text
start free heap:
end free heap:
minimum free heap:
```

---

# 55. Watchdog / Panic

O teste somente pode passar se houver:

```text
Panic: NONE
Guru Meditation: NONE
Watchdog: NONE
Unexpected reboot: NONE
```

---

# 56. Recuperação após falha

Se um teste causar estado temporário nos faróis:

prioridade máxima:

```text
RESTORE ORIGINAL STATE
```

antes de:

- documentação;
- próximo teste;
- build adicional;
- encerramento.

---

# 57. Segurança contra stale commands

Após reconnect:

comandos antigos que pertencem a uma geração ultrapassada NÃO devem ser aplicados.

Exemplo:

```text
generation 10 → RED
generation 11 → BLUE

RIGHT desconectou durante generation 10
```

Quando voltar:

```text
NÃO aplicar RED
aplicar somente BLUE
```

---

# 58. Concorrência

Proteja corretamente:

- registry;
- controller state;
- desired state;
- command queues;
- notification buffers;
- metrics.

Utilizar primitivas apropriadas do FreeRTOS/ESP-IDF.

Não segurar mutex durante operações BLE longas ou callbacks quando desnecessário.

Evitar deadlocks.

---

# 59. Callbacks

Callbacks NimBLE devem fazer o mínimo necessário.

Não executar:

- sleeps longos;
- loops;
- reconciliação complexa;
- writes encadeados pesados;

diretamente dentro do callback quando uma task/fila for mais apropriada.

Preferir:

```text
BLE callback
↓
event queue
↓
Connection Manager task
```

---

# 60. Event Queue

Considere criar eventos internos:

```text
BLE_CONNECTED
BLE_DISCONNECTED
GATT_READY
NOTIFY_RECEIVED
STATE_RECEIVED
COMMAND_OK
COMMAND_FAILED
RECONNECT_TIMER
HEALTH_CHECK
DESIRED_STATE_CHANGED
```

Isso deve reduzir acoplamento entre callbacks e lógica de controle.

---

# 61. Tasks

Evitar criar uma quantidade exagerada de tasks.

Arquitetura sugerida:

```text
NimBLE Host Task

Connection/Group Manager Task

LEFT Command Worker
RIGHT Command Worker

opcional:
Console Task
```

Ajustar se houver solução mais simples.

---

# 62. Boot behavior

No boot:

```text
NVS init
↓
load LEFT/RIGHT
↓
load Desired State
↓
BLE init
↓
scan known controllers
↓
connect
↓
notifications
↓
state query
↓
determine group state
```

Por enquanto:

```text
restore_on_boot = false
```

Logo:

não alterar LEDs automaticamente apenas devido ao reboot.

---

# 63. Teste de reboot

Depois da implementação:

1. ambos conectados;
2. mapping confirmado;
3. reiniciar ESP32;
4. carregar mapping;
5. encontrar LEFT/RIGHT;
6. conectar automaticamente;
7. query state;
8. atingir READY.

Sem repetir provisioning.

Sem teste visual automático.

Sem mudar as cores no boot.

---

# 64. README

Atualizar comandos de diagnóstico.

Exemplo:

```text
status
state
metrics
rgb
resync
test-reconnect
test-sync
```

Explicar comportamento Strict Sync.

---

# 65. AGENTS.md

Adicionar princípios obrigatórios:

```text
1. Nunca enviar comando de grupo diretamente para somente um lado por conveniência.

2. Toda mudança desejada deve passar pelo Desired State / Group Controller.

3. Um controlador reconectado deve consultar estado antes de ser reconciliado.

4. Nunca considerar GATT write como confirmação visual definitiva.

5. State Query + parser são a fonte de verificação.

6. Nunca criar reconnect loops sem backoff.

7. Não executar lógica pesada diretamente em callbacks BLE.

8. LEFT/RIGHT mapping persistido não deve ser sobrescrito automaticamente.
```

---

# 66. Documentação

Criar:

```text
docs/reconnection.md
docs/state-reconciliation.md
docs/runtime-architecture.md
```

---

# 67. reconnection.md

Documentar com dados reais:

```text
backoff
retry rules
GAP reason codes encontrados
tempo médio de reconnect
tempo máximo
software disconnect results
physical disconnect results
```

---

# 68. state-reconciliation.md

Explicar:

```text
Desired State
Observed State
generation
verified generation
STRICT_SYNC_MODE
DEGRADED
SYNCED
```

Incluir exemplos reais dos testes.

---

# 69. runtime-architecture.md

Documentar fluxo:

```text
GAP CALLBACK
↓
EVENT
↓
CONNECTION MANAGER
↓
COMMAND QUEUE
↓
STATE QUERY
↓
STATE RECONCILER
↓
GROUP STATE
```

---

# 70. Build

Executar full clean build.

Obrigatório:

```text
PASS
```

Resolver warnings introduzidos.

---

# 71. Flash

Gravar fisicamente.

Verificar hashes.

Boot físico obrigatório.

---

# 72. Test sequence obrigatória

Executar nesta ordem:

```text
1. boot
2. mapping load
3. connect LEFT
4. connect RIGHT
5. state query LEFT
6. state query RIGHT
7. GROUP READY/SYNCED

8. RGB command through Group Controller
9. verify both
10. restore

11. test-reconnect LEFT
12. verify recovery

13. test-reconnect RIGHT
14. verify recovery

15. alternating reconnect stress test
16. verify all cycles

17. mid-command disconnect test
18. verify auto resync
19. restore

20. reboot test
21. reconnect both
22. no visual changes on boot

23. 15-minute stability test
```

---

# 73. Critérios de aceite

Somente concluir quando:

- [x] Connection Manager estiver implementado;
- [x] LEFT possuir máquina de estados;
- [x] RIGHT possuir máquina de estados;
- [x] reconnect possuir backoff;
- [x] scanner parar quando ambos READY;
- [x] command queues independentes existirem;
- [x] somente um write por dispositivo ocorrer por vez;
- [x] Desired State existir;
- [x] generations existirem;
- [x] stale commands forem descartados;
- [x] Strict Sync estiver funcionando;
- [x] State Reconciler estiver implementado;
- [x] state query verificar reconciliação;
- [x] métricas estiverem disponíveis;
- [x] software reconnect LEFT passar;
- [x] software reconnect RIGHT passar;
- [x] pelo menos 5 ciclos de reconnect por lado passarem;
- [x] mid-command disconnect recuperar automaticamente;
- [x] reboot reconnect funcionar;
- [x] reboot não alterar visualmente os LEDs;
- [x] stability test de 15 minutos passar;
- [x] nenhuma memory leak evidente ocorrer;
- [x] nenhum panic ocorrer;
- [x] nenhum watchdog ocorrer;
- [x] nenhum unexpected reboot ocorrer;
- [x] estado original dos faróis for restaurado ao final;
- [x] documentação estiver atualizada.

Validação concluída em hardware em 2026-08-09. Evidências e medições:
`docs/sp624e-reliability-report.md`.

---

# 74. Relatório final

Entregar:

```text
SP624E RELIABILITY REPORT
=========================

Firmware
--------
Version:
Build:
Flash:
Boot:

Mapping
-------
LEFT:
RIGHT:

Connection Manager
------------------
LEFT:
RIGHT:
Backoff:
Scanner behavior:

Command Queues
--------------
LEFT queue:
RIGHT queue:
Serialization:
Generation handling:
Stale command handling:

Desired State
-------------
Generation:
Strict Sync:
Persistence:

Reconnect Test LEFT
-------------------
Cycles:
Pass:
Fail:
Average reconnect:
Maximum reconnect:
Average resync:
Maximum resync:

Reconnect Test RIGHT
--------------------
Cycles:
Pass:
Fail:
Average reconnect:
Maximum reconnect:
Average resync:
Maximum resync:

Mid-command Failure Test
------------------------
Side disconnected:
Desired state:
Group entered DEGRADED:
Automatic reconnect:
Automatic reconcile:
Returned SYNCED:
Temporary desync duration:

Physical Power-cycle
--------------------
Result:
Time to detect:
Time to reconnect:
Time to sync:

or:

SKIPPED_PHYSICAL_POWER_CYCLE

Reboot Test
-----------
Mapping restored:
LEFT auto-connected:
RIGHT auto-connected:
Unexpected visual change:

Long Running Test
-----------------
Duration:
LEFT disconnects:
RIGHT disconnects:
Reconnects:
Health checks:
Reconciliations:
Unexpected desyncs:

Memory
------
Initial free heap:
Final free heap:
Minimum free heap:

BLE
---
LEFT RSSI:
RIGHT RSSI:
LEFT connection parameters:
RIGHT connection parameters:
Disconnect reason codes observed:

Safety
------
Original state restored:
Unexpected persistent changes:
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

# 75. Resultado esperado

O objetivo é provar experimentalmente:

```text
Ambos sincronizados
       ↓
RIGHT perde conexão
       ↓
LEFT continua estável
       ↓
ESP32 percebe
       ↓
RIGHT volta
       ↓
ESP32 reconecta sozinho
       ↓
consulta estado
       ↓
reaplica Desired State se necessário
       ↓
verifica
       ↓
ambos sincronizados novamente
```

Sem:

```text
abrir BanlanX
tocar no celular
reiniciar ESP32
pressionar botão
reconectar manualmente
```

O sistema deve se recuperar sozinho.

---

# 76. Próxima etapa

NÃO implementar agora.

Se este goal passar, a próxima etapa será:

```text
ESP32 Wi-Fi Access Point
↓
HTTP API
↓
WebSocket
↓
PWA
↓
iPhone
```

A PWA não deverá conhecer BLE.

Ela apenas alterará:

```text
Desired State
```

e consultará:

```text
Group Status
Connection Status
Metrics
```

Toda a confiabilidade continuará dentro do ESP32.

---

# REGRA FINAL

Esta é a etapa mais importante do projeto.

Não otimizar para “funcionou uma vez”.

O objetivo é:

```text
falhar
↓
detectar
↓
recuperar
↓
verificar
```

de maneira previsível, repetível e automática.

Execute testes reais no ESP32 e nos dois SP624E antes de considerar a tarefa concluída.
