# GOAL — Setup completo ESP32 no Windows + Firmware Inicial

## 1. Objetivo

Configure completamente este computador Windows para desenvolvimento de firmware ESP32 usando **ESP-IDF**, detecte automaticamente a placa ESP32 que já está conectada ao computador via USB, configure todas as dependências necessárias, crie um projeto inicial, compile, grave o firmware na placa e valide a comunicação através da porta serial.

Este computador deve ficar pronto para continuar o desenvolvimento de uma central automotiva que futuramente terá:

- conexão BLE simultânea com 2 controladores SP624E;
- monitoramento permanente das duas conexões;
- reconexão automática;
- sincronização dos dois faróis;
- Wi-Fi para interface Web/PWA;
- API HTTP/WebSocket;
- receptor RF 433 MHz;
- persistência de configuração;
- diagnóstico e logs.

**Nesta tarefa inicial, não implemente ainda o controle real dos SP624E.**

O foco é deixar:

1. Windows configurado;
2. toolchain funcionando;
3. placa detectada;
4. firmware compilando;
5. firmware sendo gravado;
6. serial funcionando;
7. BLE funcionando através de um scanner inicial;
8. projeto organizado para as próximas etapas.

---

# 2. Autonomia

Você possui autorização para:

- executar comandos no terminal;
- usar PowerShell;
- instalar dependências;
- instalar ferramentas;
- instalar drivers;
- baixar ferramentas oficiais;
- configurar variáveis de ambiente;
- modificar PATH;
- criar arquivos;
- criar pastas;
- criar scripts;
- compilar código;
- gravar firmware no ESP32;
- abrir a porta serial;
- reiniciar processos;
- executar ferramentas com privilégios elevados quando disponíveis;
- instalar extensões ou CLIs necessárias.

Não peça confirmação para instalações comuns necessárias ao desenvolvimento.

Se alguma ação exigir uma interação manual inevitável do Windows, como uma janela UAC que não possa ser controlada pelo terminal, explique exatamente o que precisa ser feito, mas primeiro tente realizar automaticamente tudo que for possível.

Não interrompa a tarefa simplesmente porque uma dependência está ausente.

Instale-a e continue.

---

# 3. Regra importante: não assumir qual ESP32 é

Existe uma placa ESP32 fisicamente conectada ao computador via USB.

NÃO assuma antecipadamente que é:

- ESP32;
- ESP32-S3;
- ESP32-C3;
- ESP32-C6;
- ESP32-WROOM-32;
- DOIT DevKit V1;
- qualquer outro modelo.

Primeiro detecte o hardware.

Use as informações disponíveis pelo Windows e, posteriormente, pelo esptool/ESP-IDF.

Verifique pelo menos:

- dispositivos USB conectados;
- portas COM;
- VID;
- PID;
- descrição do dispositivo;
- fabricante do conversor USB/Serial;
- chip detectado pelo esptool;
- revisão do chip;
- tamanho de flash, quando possível;
- recursos relevantes do chip.

Exemplo de ferramentas que podem ser utilizadas:

```powershell
Get-PnpDevice
Get-PnpDevice -Class Ports
Get-CimInstance Win32_SerialPort
```

Use também o esptool após instalar o ambiente ESP-IDF.

Descubra automaticamente algo equivalente a:

```text
Placa USB encontrada
Porta: COM5
USB Serial: CP210x / CH340 / FTDI / USB-JTAG-Serial
Chip: ESP32 / ESP32-S3 / ESP32-C3 / ...
Flash: ...
```

A porta acima é somente exemplo.

NÃO hardcode `COM5`.

---

# 4. Drivers USB

Verifique se a placa já aparece corretamente como porta serial no Windows.

Se aparecer:

```text
COMx
```

e for possível comunicar com o chip, não instale drivers desnecessários.

Se não aparecer corretamente, determine o chip USB/Serial através de VID/PID.

Possíveis conversores incluem:

- Silicon Labs CP210x;
- WCH CH340/CH341;
- FTDI;
- USB Serial/JTAG nativo da Espressif.

Somente instale um driver caso realmente necessário.

Quando necessário, utilize preferencialmente drivers:

1. do Windows Update;
2. do fabricante oficial;
3. da Espressif, quando aplicável.

Não baixe drivers de sites aleatórios.

Depois da instalação, valide novamente a existência da porta COM.

---

# 5. Ambiente de desenvolvimento

Utilize como framework principal:

```text
ESP-IDF
```

Utilize uma versão **stable atual** do ESP-IDF.

Não utilize:

```text
master
nightly
preview
beta
release candidate
```

a menos que exista uma incompatibilidade comprovada com o hardware.

Para instalações modernas do ESP-IDF no Windows, priorize o método oficial atual da Espressif, incluindo o **Espressif Installation Manager (EIM)** quando aplicável.

---

# 6. Dependências

Inspecione primeiro o que já está instalado.

Instale ou configure tudo que estiver faltando para conseguir trabalhar normalmente com ESP-IDF.

Isso pode incluir:

- Git;
- Python suportado pelo ESP-IDF;
- ESP-IDF;
- Espressif Installation Manager;
- toolchain específico do chip;
- esptool;
- CMake;
- Ninja;
- compilador cross-platform Espressif;
- OpenOCD quando aplicável;
- drivers USB;
- ferramentas auxiliares do ESP-IDF.

Evite instalar múltiplas versões conflitantes sem necessidade.

Depois da instalação, valide individualmente ferramentas relevantes.

Exemplos:

```text
git --version
python --version
idf.py --version
esptool version
cmake --version
ninja --version
```

Os comandos exatos podem mudar entre versões.

Utilize os comandos corretos para a versão instalada.

---

# 7. Ambiente ESP-IDF

Garanta que seja possível abrir um terminal com o ambiente ESP-IDF corretamente ativado.

O projeto deverá ser utilizável futuramente sem depender de configurações obscuras feitas apenas nesta sessão.

Caso seja necessário ativar o ESP-IDF antes de usar `idf.py`, documente e automatize isso quando razoável.

Crie scripts PowerShell para facilitar o desenvolvimento.

Estrutura desejada:

```text
scripts/
├── doctor.ps1
├── detect-board.ps1
├── build.ps1
├── flash.ps1
├── monitor.ps1
└── flash-monitor.ps1
```

---

# 8. doctor.ps1

Crie:

```text
scripts/doctor.ps1
```

Ele deve validar rapidamente:

- ESP-IDF disponível;
- versão;
- Python;
- Git;
- CMake;
- Ninja;
- esptool;
- porta serial;
- placa conectada;
- chip detectado.

Exemplo conceitual de saída:

```text
ESP32 Project Doctor

[OK] Git
[OK] Python
[OK] ESP-IDF
[OK] CMake
[OK] Ninja
[OK] Serial port: COM7
[OK] Chip: ESP32
[OK] Device communication

Environment ready.
```

Se algo estiver errado, apresentar mensagem clara.

---

# 9. detect-board.ps1

Crie:

```text
scripts/detect-board.ps1
```

O script deverá tentar encontrar automaticamente a placa conectada.

Não dependa apenas do nome do dispositivo.

Combine quando necessário:

- lista de portas seriais;
- hardware ID;
- VID/PID;
- comunicação com esptool.

Caso exista apenas uma placa Espressif conectada, selecioná-la automaticamente.

Caso existam várias portas COM, testar de maneira segura para determinar qual responde como ESP.

Não grave firmware durante a simples detecção.

Ao final, apresentar:

```text
PORT=COMx
CHIP=esp32...
```

De preferência permita que os outros scripts reutilizem essa descoberta.

---

# 10. Criação do projeto

Crie nesta pasta um projeto ESP-IDF chamado conceitualmente:

```text
sp624e-controller
```

Caso esta pasta já seja a raiz do repositório, não crie níveis de diretório redundantes.

Estrutura inicial esperada:

```text
.
├── CMakeLists.txt
├── sdkconfig
├── sdkconfig.defaults
├── README.md
├── AGENTS.md
├── main/
│   ├── CMakeLists.txt
│   ├── main.c ou main.cpp
│   ├── app_config.h
│   ├── system/
│   └── ble/
├── scripts/
│   ├── doctor.ps1
│   ├── detect-board.ps1
│   ├── build.ps1
│   ├── flash.ps1
│   ├── monitor.ps1
│   └── flash-monitor.ps1
└── docs/
    └── environment.md
```

Pode ajustar a estrutura quando houver um motivo técnico melhor.

Evite overengineering.

---

# 11. Configuração do target

Depois de detectar o chip real, configure corretamente o target do ESP-IDF.

Conceitualmente:

```bash
idf.py set-target <chip-detectado>
```

Por exemplo:

```text
esp32
esp32s3
esp32c3
```

O valor real deverá vir da detecção.

Não configure um target incompatível apenas para conseguir compilar.

---

# 12. Firmware inicial

Crie um firmware mínimo, mas útil.

Ao iniciar, ele deve escrever pela serial algo semelhante a:

```text
====================================
 SP624E Controller
====================================

Firmware: 0.1.0
ESP-IDF: <versão>
Chip: <chip>
Revision: <revision>
CPU cores: <quantidade>
Flash: <tamanho>
Free heap: <valor>

System initialized successfully.
```

Use APIs reais do ESP-IDF para obter as informações.

Não hardcode informações da placa.

---

# 13. Logging

Utilize o sistema oficial de logs do ESP-IDF.

Por exemplo:

```text
ESP_LOGI
ESP_LOGW
ESP_LOGE
ESP_LOGD
```

Crie tags claras como:

```text
SYSTEM
BLE
SP624E
WIFI
RF433
```

Mesmo que alguns módulos ainda não tenham implementação.

Não use `printf` indiscriminadamente quando o logging do ESP-IDF for mais adequado.

---

# 14. Bluetooth Low Energy

O futuro projeto precisa conectar como **BLE Central / GATT Client** a dois SP624E simultaneamente.

Portanto, prepare o projeto desde já usando preferencialmente:

```text
ESP-NimBLE / NimBLE
```

quando suportado pelo chip detectado.

Não implemente ainda comandos proprietários do SP624E.

Nesta etapa implemente apenas:

```text
BLE Scan
```

---

# 15. Scanner BLE inicial

Após o sistema iniciar, disponibilize um scanner BLE de diagnóstico.

Pode iniciar automaticamente ou por uma opção simples de configuração.

Faça scan por aproximadamente:

```text
10 segundos
```

e depois finalize.

Mostre pela serial:

```text
BLE device discovered

Name: ...
Address: ...
RSSI: ...
Service UUIDs: ...
Manufacturer data: ...
```

Evite imprimir o mesmo dispositivo centenas de vezes.

Faça deduplicação quando razoável.

---

# 16. Identificação de possíveis SP624E

Durante o scan, tente destacar dispositivos que possam ser SP624E.

Podem existir nomes relacionados a:

```text
SP624E
BanlanX
LED
```

ou outros nomes.

Também considere que o dispositivo pode:

- não anunciar o nome esperado;
- usar outro nome;
- não anunciar todos os serviços no advertising packet.

Portanto:

**não filtre exclusivamente por nome.**

Nesta fase apenas apresente os dispositivos encontrados.

Se identificar algo que parece ser SP624E, destaque no log:

```text
[POSSIBLE SP624E]
Name: ...
Address: ...
RSSI: ...
```

Não conecte automaticamente ainda.

Não envie comandos aos controladores nesta tarefa.

---

# 17. Serviço BLE futuro

Prepare a arquitetura para que posteriormente seja possível implementar:

```text
BLE Manager
    ├── Scanner
    ├── Device Registry
    ├── Left SP624E
    ├── Right SP624E
    ├── Connection Manager
    ├── Reconnection Manager
    └── Command Queue
```

Porém não implemente funcionalidades fictícias apenas para preencher arquivos.

Crie somente abstrações que realmente sejam úteis nesta etapa.

---

# 18. Não assumir GPIO do LED onboard

Não assuma que existe LED onboard em determinado GPIO.

Placas ESP32 diferentes utilizam GPIOs diferentes ou sequer possuem LED de usuário.

Se conseguir identificar com segurança o modelo da placa e o GPIO do LED onboard, pode implementar um blink de teste.

Caso contrário:

**não tente piscar GPIO aleatório.**

O teste principal deve ser:

```text
flash bem-sucedido + boot + serial + BLE scan
```

---

# 19. Build

Faça um clean build após configurar o projeto.

O build deve terminar sem erros.

Idealmente também sem warnings relevantes criados pelo nosso código.

Execute o equivalente correto a:

```bash
idf.py build
```

Corrija todos os problemas encontrados.

Não considere a tarefa concluída apenas porque o código parece correto.

Ele precisa compilar de verdade.

---

# 20. Flash

Depois do build bem-sucedido:

1. detectar automaticamente a porta COM;
2. confirmar comunicação com o chip;
3. gravar o firmware.

Utilize o mecanismo oficial do ESP-IDF.

Conceitualmente:

```bash
idf.py -p COMx flash
```

Mas utilize a porta realmente detectada.

Se a gravação falhar:

- identificar se é driver;
- porta ocupada;
- modo bootloader;
- velocidade;
- cabo USB;
- reset;
- target errado;
- chip errado;
- processo segurando a COM.

Resolva automaticamente tudo que puder.

Se for necessário pressionar fisicamente `BOOT` ou `EN`, somente então informe de maneira objetiva.

---

# 21. Monitor serial

Após gravar:

- abrir serial monitor;
- aguardar boot;
- capturar os logs;
- confirmar execução do firmware.

Validar pelo menos:

```text
boot realizado
aplicação iniciada
informações do chip exibidas
BLE inicializado
BLE scan iniciado
BLE scan finalizado
nenhum panic
nenhum watchdog
nenhum reboot inesperado
```

O ESP-IDF permite fluxo de build/flash/monitor; utilize a abordagem apropriada para a versão instalada.

---

# 22. Scripts PowerShell

## build.ps1

Deve:

```text
ativar ambiente ESP-IDF se necessário
configurar target quando necessário
compilar
retornar código de erro adequado
```

Uso esperado:

```powershell
.\scripts\build.ps1
```

---

## flash.ps1

Deve:

```text
detectar placa
descobrir COM
gravar firmware
```

Uso:

```powershell
.\scripts\flash.ps1
```

---

## monitor.ps1

Deve:

```text
detectar porta
abrir monitor serial
```

Uso:

```powershell
.\scripts\monitor.ps1
```

---

## flash-monitor.ps1

Deve realizar:

```text
build
↓
flash
↓
monitor
```

Uso:

```powershell
.\scripts\flash-monitor.ps1
```

Este deverá ser o comando principal durante o desenvolvimento.

---

# 23. Arquitetura futura

O objetivo final deste hardware será:

```text
                   iPhone
                     │
                  Wi-Fi
                     │
                     ▼
               ┌──────────┐
               │  ESP32   │
               └──────────┘
                  │      │
                BLE      BLE
                  │      │
                  ▼      ▼
              SP624E   SP624E
              LEFT      RIGHT
                  │      │
                  ▼      ▼
               Farol    Farol
```

E futuramente:

```text
Controle RF 433 MHz
        │
        ▼
Receptor 433 MHz
        │ GPIO
        ▼
      ESP32
```

O firmware deverá eventualmente ser a autoridade sobre o estado desejado dos dois faróis.

---

# 24. Não implementar nesta tarefa

NÃO implementar agora:

- PWA;
- React;
- servidor HTTP completo;
- WebSocket;
- protocolo SP624E;
- alteração de cores;
- efeitos;
- controle de brilho;
- pareamento definitivo;
- reconexão BLE agressiva;
- receptor 433 MHz;
- persistência das configurações;
- atualização OTA.

Essas serão etapas posteriores.

Evite adicionar complexidade antes de provar que:

```text
PC → USB → ESP32 → firmware
```

e:

```text
ESP32 → BLE scan
```

estão funcionando.

---

# 25. Preparação para Wi-Fi

Embora Wi-Fi não deva ser implementado nesta tarefa, mantenha a arquitetura compatível com o uso futuro simultâneo de:

```text
BLE + Wi-Fi
```

Não faça escolhas arquiteturais que impeçam coexistência posterior.

O ESP32 futuramente deverá manter duas conexões BLE enquanto disponibiliza uma interface de controle pelo Wi-Fi.

---

# 26. Segurança

Não envie dados ou firmware para serviços externos sem necessidade.

Não baixe binários de sites desconhecidos.

Priorize:

1. Espressif;
2. Microsoft/Windows;
3. Git oficial;
4. fabricante oficial do componente.

Não execute scripts aleatórios obtidos de fóruns.

---

# 27. Git

Inicialize Git caso ainda não esteja inicializado.

Crie `.gitignore` apropriado para ESP-IDF.

Não versionar:

- diretórios de build;
- caches;
- ambientes Python;
- arquivos temporários;
- credenciais;
- configurações locais desnecessárias.

Versionar:

- código;
- CMake;
- scripts;
- `sdkconfig.defaults`;
- documentação;
- configurações reproduzíveis.

Avalie cuidadosamente se `sdkconfig` deve ser versionado ou se `sdkconfig.defaults` é suficiente para este projeto.

---

# 28. README

Crie um `README.md` útil contendo:

## Requisitos

Ambiente utilizado e versões.

## Hardware detectado

Exemplo:

```text
Chip:
Board:
USB Serial:
COM:
Flash:
```

Use os valores reais encontrados.

## Como compilar

```powershell
.\scripts\build.ps1
```

## Como gravar

```powershell
.\scripts\flash.ps1
```

## Como monitorar

```powershell
.\scripts\monitor.ps1
```

## Desenvolvimento normal

```powershell
.\scripts\flash-monitor.ps1
```

## Troubleshooting

Inclua problemas comuns realmente relevantes encontrados durante a configuração.

---

# 29. AGENTS.md

Crie um `AGENTS.md` para futuras execuções do Codex contendo contexto sobre:

- objetivo do projeto;
- arquitetura;
- hardware;
- ESP-IDF;
- target;
- comandos de build;
- comandos de flash;
- comandos de monitor;
- convenções;
- estrutura do projeto;
- necessidade de validar fisicamente firmware antes de considerar alterações concluídas.

Inclua uma regra:

> Qualquer alteração no firmware deve, quando houver hardware disponível, ser validada através de build real e, quando seguro, flash e monitor serial.

---

# 30. docs/environment.md

Documente:

- Windows detectado;
- versão do ESP-IDF;
- versão Python;
- versão Git;
- toolchain;
- porta COM;
- VID/PID;
- chip;
- revisão;
- flash;
- driver USB utilizado;
- caminho do ESP-IDF;
- forma de ativar o ambiente.

Isso servirá para recuperar o ambiente futuramente.

---

# 31. Teste final obrigatório

Ao concluir, execute um teste end-to-end:

```text
1. detectar a placa
2. validar ambiente
3. clean build
4. flash
5. reset
6. monitor serial
7. confirmar informações do chip
8. executar BLE scan
9. listar dispositivos BLE encontrados
10. confirmar ausência de crashes
```

Se os SP624E estiverem ligados e ao alcance, registre no relatório qualquer dispositivo que pareça corresponder a eles.

Não é obrigatório encontrá-los para considerar o setup do ESP32 funcional.

---

# 32. Critérios de aceite

A tarefa só estará concluída quando:

- [ ] ESP-IDF estiver instalado;
- [ ] dependências estiverem configuradas;
- [ ] placa estiver corretamente detectada;
- [ ] porta COM estiver identificada;
- [ ] chip ESP estiver identificado;
- [ ] target correto estiver configurado;
- [ ] projeto estiver criado;
- [ ] projeto compilar;
- [ ] firmware for gravado fisicamente no ESP32;
- [ ] ESP32 inicializar sem crash;
- [ ] monitor serial funcionar;
- [ ] informações reais do chip aparecerem no log;
- [ ] NimBLE/BLE inicializar;
- [ ] scan BLE funcionar;
- [ ] scripts PowerShell funcionarem;
- [ ] README estiver criado;
- [ ] AGENTS.md estiver criado;
- [ ] environment.md estiver criado.

---

# 33. Relatório final

Ao terminar, apresente um relatório objetivo neste formato:

```text
SETUP ESP32 CONCLUÍDO

Hardware
--------
Chip:
Revisão:
Flash:
USB/Serial:
Porta:
VID/PID:

Ambiente
--------
Windows:
ESP-IDF:
Python:
Git:
CMake:
Ninja:
esptool:

Firmware
--------
Target:
Build: OK/FAIL
Flash: OK/FAIL
Boot: OK/FAIL
Serial: OK/FAIL
BLE: OK/FAIL
BLE Scan: OK/FAIL

Dispositivos BLE encontrados
----------------------------
1.
2.
3.

Possíveis SP624E
----------------
1.
2.

Arquivos principais criados
----------------------------
...

Comando para continuar desenvolvendo
------------------------------------
...

Problemas encontrados
---------------------
...

Próxima etapa recomendada
-------------------------
...
```

Inclua logs importantes quando necessários.

---

# 34. Princípio geral

Não apenas escreva código.

**Execute e valide.**

O resultado final esperado desta tarefa é um ESP32 fisicamente conectado ao computador, com um firmware real criado neste repositório, compilado e gravado com sucesso, emitindo logs pela serial e conseguindo realizar um BLE scan.

Somente depois disso considere a tarefa concluída.