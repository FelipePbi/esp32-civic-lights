# GOAL — Identificar e conectar simultaneamente aos 2 SP624E via BLE

## Contexto confirmado

O ambiente ESP32 já está completamente configurado e funcionando.

Hardware:

```text
Chip: ESP32-D0WD-V3
Revision: v3.1
Flash: 4 MB
CPU: dual-core 240 MHz
USB/Serial: WCH CH9102
Porta: COM5
VID/PID: 1A86:55D4
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
Toolchain: xtensa-esp-elf 15.2.0
```

Projeto existente:

```text
C:\Projetos\ESP32
```

O projeto já:

- compila;
- grava fisicamente no ESP32;
- inicializa corretamente;
- possui monitor serial;
- possui NimBLE;
- executa BLE scan;
- possui scripts PowerShell;
- possui README.md;
- possui AGENTS.md;
- possui docs/environment.md.

O comando normal de desenvolvimento é:

```powershell
.\scripts\flash-monitor.ps1
```

Quando necessário, devido ao comportamento de boot da placa:

```powershell
.\scripts\flash-monitor.ps1 -ManualBoot
```

Não recrie o projeto.

Trabalhe em cima da implementação atual.

---

# 1. Objetivo

Evoluir o firmware atual para:

1. capturar completamente os advertising packets BLE;
2. identificar especificamente controladores SP624E;
3. encontrar os dois SP624E presentes;
4. criar um registry de dispositivos;
5. conectar aos dois simultaneamente;
6. realizar GATT Service Discovery;
7. realizar Characteristic Discovery;
8. confirmar a existência do serviço FFE0;
9. confirmar a existência da característica FFE1;
10. manter ambas as conexões abertas;
11. monitorar desconexões;
12. coletar logs de diagnóstico.

Nesta etapa:

**NÃO enviar nenhum comando que altere os LEDs.**

Não ligar/desligar.

Não alterar cor.

Não alterar brilho.

Não alterar efeito.

Não testar comandos proprietários.

O objetivo desta etapa é somente:

```text
ESP32
  ├── conexão BLE → SP624E #1
  └── conexão BLE → SP624E #2
```

---

# 2. Informação conhecida sobre o SP624E

Existe implementação open source atual do protocolo BanlanX v3 no projeto UniLED.

Use-a apenas como referência técnica.

Referência:

```text
GitHub:
monty68/uniled

Arquivo:
custom_components/uniled/lib/ble/banlanx3.py

Modelo:
SP624E
```

A implementação atual do UniLED define o SP624E como:

```text
Model ID: 0x624E
Protocol: BanlanX v3
Channels: RGBW / 4 colors
Internal microphone: false
```

A assinatura específica definida para SP624E é:

```text
Manufacturer ID:
20563 decimal
0x5053 hexadecimal

Manufacturer data prefix:
0F 00
```

O UniLED também define:

```text
BLE Service:
0000FFE0-0000-1000-8000-00805F9B34FB

Write Characteristic:
0000FFE1-0000-1000-8000-00805F9B34FB
```

Não assuma que o nome BLE contém:

```text
SP624E
BanlanX
LED
```

A identificação principal deve ser baseada nos dados do advertising packet.

---

# 3. Primeiro problema a resolver

O scanner atual encontrou 11 dispositivos:

```text
1.  3C:0B:59:45:80:31 RSSI -75
2.  06:B9:37:B1:E0:26 RSSI -18
3.  66:2B:E1:CE:EF:5D RSSI -58
4.  50:55:9D:5E:CB:70 RSSI -58
5.  72:F7:BC:F7:A3:33 RSSI -72
6.  56:60:26:58:FF:D9 RSSI -64
7.  41:21:C6:1F:0E:59 RSSI -58
8.  E5:1E:9A:2C:EC:62 RSSI -58
9.  E2:7F:C1:29:93:FB RSSI -46
10. F0:EF:93:2B:D3:1D RSSI -78
11. BC:35:1E:F8:FB:AC RSSI -88
```

Nenhum apresentou nome reconhecível.

Isso NÃO significa que os SP624E não estavam presentes.

O scanner anterior não estava utilizando todos os campos necessários para identificação.

---

# 4. Melhorar o scanner BLE

Refatore o scanner atual para fazer parsing completo do advertising packet.

Para cada dispositivo relevante, capturar quando disponível:

```text
BLE address
address type
RSSI
advertisement type
flags
complete local name
short local name
TX power
16-bit service UUIDs
32-bit service UUIDs
128-bit service UUIDs
manufacturer specific data
service data
raw advertising payload
scan response payload
```

Imprimir dados binários também em hexadecimal.

Exemplo:

```text
BLE DEVICE
--------------------------------
Address: AA:BB:CC:DD:EE:FF
Address type: PUBLIC
RSSI: -42
Name: <none>

Manufacturer:
ID: 0x5053
Data: 0F 00 XX XX XX ...

Services:
FFE0

Raw ADV:
02 01 06 ...

--------------------------------
```

Não dependa exclusivamente de Local Name.

---

# 5. Active Scan

Verifique se o scan atual está configurado de forma adequada para receber Scan Response.

Se necessário, utilize Active Scan.

Precisamos obter o máximo possível de informação que o periférico disponibilizar.

Mantenha deduplicação lógica no registry, mas permita atualizar informações quando um novo advertising packet ou scan response contiver campos adicionais.

Exemplo:

```text
primeiro packet:
address + RSSI

segundo packet:
manufacturer data

scan response:
local name
```

O registry deve combinar essas informações.

---

# 6. Parsing correto de Manufacturer Specific Data

Faça parsing do campo BLE:

```text
AD Type 0xFF
Manufacturer Specific Data
```

Lembre que o Company/Manufacturer ID presente no advertising data utiliza representação little-endian no payload BLE.

Não procure apenas pela sequência literal `0F 00` em qualquer lugar do pacote.

A identificação deve considerar estruturalmente:

```text
Manufacturer ID == 0x5053
```

e:

```text
manufacturer payload começa com:

0F 00
```

Conceitualmente:

```c
bool is_sp624e(const adv_data_t *adv)
{
    return adv->manufacturer_id == 0x5053 &&
           adv->manufacturer_data_len >= 2 &&
           adv->manufacturer_data[0] == 0x0F &&
           adv->manufacturer_data[1] == 0x00;
}
```

Adapte para as estruturas/APIs reais do NimBLE usado pelo ESP-IDF 6.0.2.

---

# 7. Níveis de confiança

Implemente classificação de candidatos.

## CONFIRMED_SP624E

Quando:

```text
Manufacturer ID == 0x5053
AND
manufacturer data começa com 0F 00
```

## POSSIBLE_BANLANX

Quando alguma evidência relevante existir, por exemplo:

```text
Manufacturer ID == 0x5053
```

mas o payload não puder ser completamente validado.

## GENERIC_FFE0_DEVICE

Quando o advertising anunciar FFE0, mas não houver assinatura de SP624E.

Não classifique automaticamente todo dispositivo FFE0 como SP624E.

FFE0/FFE1 são UUIDs genéricos utilizados por diferentes equipamentos BLE.

---

# 8. Registry

Crie uma estrutura apropriada para registrar dispositivos encontrados.

Exemplo conceitual:

```c
typedef enum {
    DEVICE_UNKNOWN,
    DEVICE_POSSIBLE_BANLANX,
    DEVICE_CONFIRMED_SP624E
} device_type_t;

typedef struct {
    ble_addr_t address;

    int8_t rssi;

    char name[...];

    uint16_t manufacturer_id;

    uint8_t manufacturer_data[...];
    size_t manufacturer_data_len;

    bool advertises_ffe0;

    device_type_t type;

    bool connected;

    uint16_t conn_handle;

    bool service_ffe0_found;

    bool characteristic_ffe1_found;

} ble_device_entry_t;
```

A estrutura real pode ser melhorada conforme a arquitetura existente.

Não faça alocação descontrolada durante o scan.

Use limites claros.

---

# 9. Resultado esperado do scan

Ao finalizar o scan, produzir um resumo:

```text
=======================================
BLE SCAN SUMMARY
=======================================

Total unique devices: 11

Confirmed SP624E: 2

SP624E #1
Address: XX:XX:XX:XX:XX:XX
RSSI: -XX
Manufacturer ID: 0x5053
Manufacturer Data: 0F 00 ...

SP624E #2
Address: YY:YY:YY:YY:YY:YY
RSSI: -XX
Manufacturer ID: 0x5053
Manufacturer Data: 0F 00 ...

=======================================
```

Se exatamente dois forem encontrados, continuar automaticamente para a fase de conexão.

Se apenas um for encontrado:

```text
não inventar o segundo
não conectar em dispositivo aleatório
```

Se mais de dois forem encontrados:

```text
listar todos
não escolher baseado somente no RSSI
```

---

# 10. Configuração NimBLE para múltiplas conexões

O dispositivo real é:

```text
ESP32-D0WD-V3
```

Precisamos manter pelo menos duas conexões BLE Central simultaneamente.

Verifique as configurações atuais do ESP-IDF/NimBLE.

Garanta:

```text
NimBLE Central role habilitado
Maximum Connections >= 2
```

Preferencialmente configure:

```text
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3
```

para manter margem.

Como o target é ESP32 clássico, verifique também a configuração correspondente do controller para máximo de conexões BLE no ESP-IDF 6.0.2.

Não adivinhe o nome da configuração.

Consulte os Kconfig existentes na versão instalada.

Utilize:

```text
sdkconfig.defaults
```

quando adequado, para tornar a configuração reproduzível.

Não habilite Classic Bluetooth.

Precisamos apenas de BLE.

---

# 11. Connection Manager

Crie um módulo apropriado:

```text
main/ble/
├── ble_scanner.c
├── ble_scanner.h
├── ble_registry.c
├── ble_registry.h
├── ble_connection.c
└── ble_connection.h
```

Pode adaptar nomes à estrutura existente.

Responsabilidades:

```text
BLE Scanner
    ↓
Device Registry
    ↓
SP624E Detection
    ↓
Connection Manager
    ↓
GATT Discovery
```

---

# 12. Conexão

Quando dois SP624E forem confirmados:

```text
SP624E A
SP624E B
```

conectar aos dois.

Não desconectar o primeiro para conectar o segundo.

Fluxo esperado:

```text
scan
↓
encontrou os dois
↓
stop scan
↓
connect SP624E A
↓
A conectado
↓
GATT discovery A
↓
connect SP624E B
↓
B conectado
↓
GATT discovery B
↓
manter A + B conectados
```

Se tecnicamente fizer mais sentido conectar antes de finalizar toda a discovery do primeiro, isso pode ser ajustado.

Priorize estabilidade e clareza.

---

# 13. Eventos GAP

Trate corretamente eventos como:

```text
connect
disconnect
connection update
MTU
encryption change, se ocorrer
repeat pairing, se ocorrer
```

Não exigir bonding se o SP624E não exigir.

Não iniciar pairing sem necessidade.

Registrar sempre:

```text
address
conn_handle
status
reason
```

Exemplo:

```text
[BLE] CONNECTED
Device: 12:34:56:78:9A:BC
Handle: 1

[BLE] DISCONNECTED
Device: 12:34:56:78:9A:BC
Reason: 0x...
```

---

# 14. GATT Service Discovery

Depois da conexão, descubra todos os serviços.

Não assuma que FFE0 está em determinado handle.

Para cada serviço:

```text
UUID
start handle
end handle
```

Exemplo:

```text
[GATT] Service
UUID: FFE0
Start: 0x000C
End: 0x0010
```

Confirmar especificamente:

```text
0000FFE0-0000-1000-8000-00805F9B34FB
```

Marcar:

```text
service_ffe0_found = true
```

---

# 15. Characteristic Discovery

Dentro do serviço FFE0, descobrir todas as características.

Registrar:

```text
UUID
definition handle
value handle
properties
```

Decodificar properties de maneira legível:

```text
READ
WRITE
WRITE_NO_RESPONSE
NOTIFY
INDICATE
```

Confirmar especificamente:

```text
0000FFE1-0000-1000-8000-00805F9B34FB
```

Salvar:

```text
FFE1 value handle
FFE1 properties
```

Não escrever nada na característica.

---

# 16. Descobrir descriptors

Quando razoável, também descubra os descriptors relacionados às características.

Registrar:

```text
UUID
handle
```

Isso será útil posteriormente caso existam notificações ou CCCD.

Mas:

```text
NÃO escrever no CCCD nesta tarefa.
```

---

# 17. GATT dump

Para cada SP624E produzir algo assim:

```text
========================================
SP624E GATT PROFILE
========================================

Address:
RSSI:

SERVICE
UUID: 0000FFE0-0000-1000-8000-00805F9B34FB
Start:
End:

  CHARACTERISTIC
  UUID: 0000FFE1-0000-1000-8000-00805F9B34FB
  Value Handle:
  Properties:
    WRITE: yes/no
    WRITE NO RESPONSE: yes/no
    READ: yes/no
    NOTIFY: yes/no
    INDICATE: yes/no

  DESCRIPTORS
  ...

========================================
```

Também liste outros serviços/características existentes.

Não descarte informação só porque aparentemente não é necessária.

---

# 18. Identificação dos dois lados

Nesta tarefa não é obrigatório definir:

```text
LEFT
RIGHT
```

Os dois podem inicialmente ser:

```text
SP624E_1
SP624E_2
```

ou identificados por endereço.

Não adivinhe esquerda/direita usando RSSI.

Posteriormente será criado um processo de provisionamento para associar fisicamente cada endereço ao lado correspondente.

---

# 19. Teste de conexão simultânea

Quando os dois estiverem conectados e GATT discovery tiver terminado:

mantenha ambos conectados durante pelo menos:

```text
60 segundos
```

Durante esse período:

- não enviar comandos;
- não fazer polling GATT agressivo;
- observar eventos de disconnect;
- registrar uptime da conexão;
- registrar connection handle;
- opcionalmente consultar RSSI usando API apropriada quando disponível sem gerar tráfego excessivo.

Log periódico:

```text
[BLE STATUS]

Uptime: 15s

SP624E_1:
CONNECTED
handle=0
RSSI=-XX

SP624E_2:
CONNECTED
handle=1
RSSI=-XX
```

Intervalo de status:

```text
5 segundos
```

Esse status é apenas diagnóstico.

---

# 20. Não implementar reconexão agressiva ainda

Nesta etapa, se um dispositivo desconectar:

```text
registrar
motivo
horário
tempo que permaneceu conectado
```

Pode realizar uma tentativa simples de recuperação, mas NÃO implementar ainda o algoritmo final de reconexão infinita.

Primeiro queremos entender:

```text
se os dois ficam conectados
por quanto tempo
por qual motivo desconectam
```

O Reconnection Manager definitivo será a próxima etapa depois dessa validação.

---

# 21. Segurança absoluta: nenhum WRITE

Crie uma proteção explícita nesta etapa.

Não deve existir caminho normal de código capaz de chamar GATT write nos SP624E.

Se já existir algum helper genérico de write, não o utilize.

Opcionalmente adicione:

```c
#define SP624E_ALLOW_WRITES 0
```

e compile qualquer futura função de escrita apenas quando explicitamente habilitada.

Por padrão:

```text
SP624E_ALLOW_WRITES = 0
```

Logs:

```text
SP624E write commands: DISABLED
```

Queremos garantir que esta etapa não altere o comportamento dos faróis.

---

# 22. Não copiar cegamente código de terceiros

UniLED deve ser utilizado como referência para:

```text
assinatura
manufacturer ID
manufacturer data
UUIDs
protocolo
```

Não copie grandes trechos de implementação Python para C.

Implemente corretamente usando:

```text
ESP-IDF 6.0.2
NimBLE
C/C++
```

Siga as APIs disponíveis na versão instalada.

---

# 23. Logs

Utilize tags claras:

```text
BLE_SCAN
BLE_REGISTRY
BLE_CONNECT
BLE_GATT
SP624E
```

Exemplos:

```text
I (1234) SP624E: Confirmed SP624E: XX:XX:XX:XX:XX:XX

I (1500) BLE_CONNECT: Connecting to XX:XX...

I (2000) BLE_CONNECT: Connected handle=0

I (2200) BLE_GATT: FFE0 service found

I (2250) BLE_GATT: FFE1 characteristic found

I (3000) SP624E: Device READY
```

Evitar spam descontrolado no advertising callback.

---

# 24. Estado de cada controlador

Crie estados claros:

```c
typedef enum {
    SP624E_STATE_UNKNOWN,
    SP624E_STATE_DISCOVERED,
    SP624E_STATE_CONNECTING,
    SP624E_STATE_CONNECTED,
    SP624E_STATE_DISCOVERING,
    SP624E_STATE_READY,
    SP624E_STATE_DISCONNECTED,
    SP624E_STATE_ERROR
} sp624e_state_t;
```

O conceito importante é:

```text
READY
```

somente quando:

```text
conectado
+
FFE0 encontrado
+
FFE1 encontrado
```

---

# 25. Critério de identificação definitivo

Um dispositivo somente pode chegar a:

```text
SP624E_STATE_READY
```

quando:

### Advertising

For confirmado como SP624E através da assinatura conhecida:

```text
Manufacturer ID 0x5053
Manufacturer payload 0F 00...
```

E:

### GATT

Após conectar possuir:

```text
Service FFE0
Characteristic FFE1
```

Isso reduz o risco de controlar posteriormente um dispositivo BLE errado.

---

# 26. Caso a assinatura 0F 00 não apareça

Se nenhum dos dispositivos encontrados apresentar:

```text
Manufacturer 0x5053
Data prefix 0F 00
```

NÃO desista imediatamente.

Faça diagnóstico.

Verifique:

- se Active Scan está habilitado;
- se Scan Response foi capturado;
- se Manufacturer Data está sendo parseado corretamente;
- se Company ID está sendo interpretado little-endian;
- se o payload bruto contém informações ignoradas;
- se os SP624E estão ligados;
- se continuam anunciando enquanto desconectados;
- se outro central BLE pode estar conectado.

Imprima o advertising payload bruto dos dispositivos encontrados.

Não conecte aleatoriamente nos 11 dispositivos.

Se necessário, apenas candidatos com evidência relevante poderão ser inspecionados de maneira controlada.

---

# 27. Caso apenas FFE0 apareça após conexão

É possível que o serviço FFE0 não seja anunciado no advertising packet e apareça apenas através de GATT Service Discovery.

Portanto:

```text
manufacturer signature
```

é a principal identificação antes da conexão.

Depois:

```text
FFE0 + FFE1
```

serve como confirmação adicional.

---

# 28. Atualizar scripts

O comando existente:

```powershell
.\scripts\flash-monitor.ps1
```

deve continuar funcionando.

Não quebrar:

```text
doctor.ps1
detect-board.ps1
build.ps1
flash.ps1
monitor.ps1
flash-monitor.ps1
```

Se fizer alterações necessárias, manter compatibilidade.

---

# 29. Atualizar documentação

Atualize:

```text
README.md
AGENTS.md
docs/environment.md
```

e crie:

```text
docs/ble-discovery.md
```

O novo documento deve registrar:

- assinatura SP624E;
- Manufacturer ID;
- Manufacturer Data;
- serviço FFE0;
- característica FFE1;
- endereços encontrados;
- GATT profiles reais;
- propriedades reais da FFE1;
- resultados do teste simultâneo;
- motivos de disconnect encontrados.

Não documentar suposições como fatos.

Separar:

```text
KNOWN FROM REFERENCE
```

de:

```text
CONFIRMED ON OUR HARDWARE
```

---

# 30. Build obrigatório

Depois da implementação:

```text
clean build
```

Resolver:

- erros;
- warnings introduzidos;
- problemas de tipos;
- problemas de concorrência;
- callbacks usando memória inválida.

Não considerar concluído apenas porque o código parece correto.

---

# 31. Flash obrigatório

Gravar fisicamente no:

```text
ESP32-D0WD-V3
COM5
```

A porta pode ser redetectada pelo script, mas COM5 é o valor atualmente conhecido.

Validar hash e boot.

---

# 32. Monitor obrigatório

Executar firmware real.

Capturar logs.

Confirmar:

```text
NimBLE OK
Active Scan OK
Manufacturer parsing OK
SP624E detection OK
Connection #1 OK
Connection #2 OK
GATT discovery #1 OK
GATT discovery #2 OK
FFE0 #1 OK
FFE1 #1 OK
FFE0 #2 OK
FFE1 #2 OK
60-second simultaneous connection test
```

Nenhum:

```text
panic
watchdog
Guru Meditation
memory corruption
unexpected reboot
```

---

# 33. Não alterar fisicamente os LEDs

Durante todo o teste:

```text
ZERO GATT WRITES para FFE1.
```

Caso uma API de discovery internamente realize procedimentos normais do protocolo BLE, isso é aceitável.

O que está proibido são writes de aplicação destinados ao controlador.

---

# 34. Resultado de sucesso

O resultado ideal é:

```text
ESP32
│
├── SP624E_1
│   ├── signature: CONFIRMED
│   ├── BLE: CONNECTED
│   ├── FFE0: FOUND
│   ├── FFE1: FOUND
│   └── state: READY
│
└── SP624E_2
    ├── signature: CONFIRMED
    ├── BLE: CONNECTED
    ├── FFE0: FOUND
    ├── FFE1: FOUND
    └── state: READY
```

e os dois permanecendo conectados simultaneamente.

---

# 35. Critérios de aceite

Somente marque a tarefa como concluída quando:

- [ ] advertising parser completo estiver funcionando;
- [ ] Manufacturer Specific Data estiver sendo capturado;
- [ ] Manufacturer ID estiver sendo interpretado corretamente;
- [ ] assinatura `0x5053 + 0F 00` estiver implementada;
- [ ] registry estiver funcionando;
- [ ] dois SP624E tiverem sido identificados, caso estejam fisicamente disponíveis;
- [ ] conexão GATT estiver implementada;
- [ ] dois dispositivos puderem permanecer conectados simultaneamente;
- [ ] Service Discovery estiver funcionando;
- [ ] Characteristic Discovery estiver funcionando;
- [ ] FFE0 tiver sido procurado;
- [ ] FFE1 tiver sido procurado;
- [ ] properties da FFE1 tiverem sido registradas;
- [ ] nenhuma escrita de aplicação tiver sido realizada;
- [ ] teste físico tiver sido executado;
- [ ] build estiver OK;
- [ ] flash estiver OK;
- [ ] serial estiver OK;
- [ ] documentação estiver atualizada.

Se algum item depender dos SP624E estarem ligados ou próximos e eles não estiverem disponíveis, marque explicitamente como:

```text
BLOCKED_BY_HARDWARE_AVAILABILITY
```

Não simule sucesso.

---

# 36. Relatório final

Ao terminar, responder:

```text
SP624E BLE DISCOVERY REPORT
===========================

Firmware
--------
Build:
Flash:
Boot:
NimBLE:

Scan
----
Active scan:
Unique BLE devices:
Manufacturer data parsing:

SP624E Detection
----------------
Confirmed devices:

Device 1
Address:
Address type:
RSSI:
Manufacturer ID:
Manufacturer Data:
Raw ADV:

Device 2
Address:
Address type:
RSSI:
Manufacturer ID:
Manufacturer Data:
Raw ADV:

GATT DEVICE 1
-------------
Connection:
Connection handle:
FFE0:
FFE1:
FFE1 properties:
Other services:
Disconnects:

GATT DEVICE 2
-------------
Connection:
Connection handle:
FFE0:
FFE1:
FFE1 properties:
Other services:
Disconnects:

Simultaneous Connection Test
----------------------------
Both connected:
Duration:
Unexpected disconnects:
Reason codes:
Panic:
Watchdog:
Unexpected reboot:

Safety
------
GATT writes performed:
Expected value: ZERO

Conclusion
----------
...

Addresses to persist
--------------------
SP624E_1:
SP624E_2:

Open questions
--------------
...

Recommended next step
---------------------
...
```

Inclua os dados reais.

---

# 37. Princípio da tarefa

Não queremos controlar os faróis ainda.

Queremos primeiro provar experimentalmente:

```text
ESP32
↓
identifica SP624E corretamente
↓
conecta no SP624E #1
↓
conecta no SP624E #2
↓
descobre GATT
↓
mantém duas conexões simultâneas
```

Somente depois dessa etapa será implementado:

```text
Command Protocol
State Reading
Color Control
Synchronization
Reconnection Manager
Wi-Fi
PWA
433 MHz
```

Não pule etapas.