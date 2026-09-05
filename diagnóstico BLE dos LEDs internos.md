Quero que você trabalhe neste projeto ESP32 já existente e implemente uma nova capacidade de diagnóstico Bluetooth para investigarmos e, posteriormente, integrarmos o kit de iluminação ambiente interna do carro.

Antes de qualquer alteração, faça uma análise cuidadosa do que já existe no repositório. **Não assuma que o contexto abaixo corresponde exatamente à implementação atual: use-o apenas como contexto funcional e confirme tudo no código.**

# CONTEXTO DO PROJETO

Este projeto foi desenvolvido para controlar os faróis RGB do meu Honda Civic através de um ESP32.

Atualmente temos um sistema funcional onde:

- um ESP32 controla os controladores dos faróis;
- os faróis utilizam controladores SP624E;
- a comunicação é feita por Bluetooth;
- existe um frontend/PWA para controlar cores, brilho, efeitos e outros comportamentos;
- existem dois lados independentes:
  - farol esquerdo;
  - farol direito;
- o firmware já possui lógica de conexão e reconexão com os controladores;
- tivemos anteriormente problemas de perda de conexão quando o sistema elétrico do carro alternava modos dos faróis;
- esses problemas de reconexão **já foram corrigidos**;
- o comportamento atual está funcionando bem no carro;
- portanto, trate a implementação atual dos SP624E como uma funcionalidade estável que não deve sofrer regressões.

Além do controle pelo PWA, o projeto pode possuir outras integrações, estados, comandos, diagnósticos e regras específicas do carro.

**Descubra tudo isso analisando o repositório antes de tomar decisões.**

# IMPORTANTE: ANALISE O PROJETO PRIMEIRO

Antes de implementar qualquer coisa:

1. Leia os arquivos de orientação do projeto, caso existam:
   - `CLAUDE.md`
   - `AGENTS.md`
   - README
   - documentação em `docs/`
   - outros `.md` relevantes.

2. Analise a estrutura do firmware.

3. Descubra:
   - framework utilizado;
   - modelo/target do ESP32;
   - stack Bluetooth utilizada;
   - como BLE é inicializado;
   - como os SP624E são descobertos;
   - como os dispositivos são identificados;
   - como LEFT e RIGHT são representados;
   - como as conexões são armazenadas;
   - como funciona a reconexão;
   - como os comandos RGB são enviados;
   - como erros Bluetooth são tratados;
   - como tarefas/threads/event loops são organizados;
   - como os logs funcionam;
   - como o frontend/PWA conversa com o ESP32;
   - quais mecanismos de diagnóstico já existem;
   - limites atuais de conexões Bluetooth;
   - uso relevante de heap/memória.

4. Consulte histórico recente do Git quando isso ajudar a entender decisões relacionadas principalmente a:
   - BLE;
   - reconexão;
   - SP624E;
   - estabilidade;
   - diagnóstico.

Não leia indiscriminadamente o repositório inteiro para o contexto. Vá aprofundando somente nas áreas relevantes.

Antes de implementar, me apresente um resumo curto da arquitetura que encontrou e então prossiga com a implementação sem esperar minha confirmação.

---

# NOVO OBJETIVO

Eu já tenho instalado dentro do carro um kit de iluminação ambiente RGB:

AliExpress:
`https://pt.aliexpress.com/item/1005003666858928.html`

Esse kit possui controlador próprio e é controlado por aplicativo através de Bluetooth.

Meu objetivo final é fazer o **mesmo ESP32 que já controla os faróis também controlar esse kit interno**.

Exemplo:

```text
Usuário seleciona vermelho no PWA

ESP32
 ├── farol esquerdo → vermelho
 ├── farol direito  → vermelho
 └── LEDs internos  → vermelho
```

Não existe necessidade de sincronização precisa.

Se:

```text
faróis → vermelho em T+0ms
interior → vermelho em T+200ms
```

isso é perfeitamente aceitável.

O LED interno deverá futuramente funcionar como **best effort**.

Ou seja:

```text
Farol esquerdo: conectado
Farol direito: conectado
LED interno: desconectado
```

não pode impedir os dois faróis de continuarem funcionando normalmente.

A iluminação interna é secundária.

---

# O QUE NÃO SABEMOS AINDA

Ainda não sabemos o protocolo utilizado pelo controlador da iluminação interna.

Precisamos descobrir:

- se realmente utiliza BLE ou Bluetooth Classic;
- nome anunciado;
- endereço;
- tipo de endereço;
- advertisement;
- services;
- characteristics;
- UUIDs;
- propriedades GATT;
- characteristic utilizada para comandos;
- formato dos pacotes;
- comandos de:
  - cor;
  - brilho;
  - efeitos;
  - liga/desliga.

**Não invente essas informações.**

Também não use protocolos encontrados na internet como se fossem necessariamente deste controlador.

Precisamos primeiro observar o hardware real instalado no carro.

---

# OBJETIVO DESTA IMPLEMENTAÇÃO

Nesta etapa, NÃO implemente ainda:

```text
setColor() dos faróis
        ↓
setColor() do interior
```

Primeiro quero adicionar ao firmware uma ferramenta de **BLE Diagnostic / Reverse Engineering Mode**.

Ela permitirá usar o próprio ESP32 para investigar o controlador interno.

O modo deve ser suficientemente completo para descobrirmos posteriormente o protocolo real.

---

# PRINCÍPIO FUNDAMENTAL

O diagnóstico não pode comprometer a funcionalidade existente.

Prioridade:

```text
1. Farol LEFT
2. Farol RIGHT
3. funcionamento normal do ESP32
4. diagnóstico do LED interno
```

Se for necessário abortar uma operação de diagnóstico para preservar os faróis:

**aborte o diagnóstico.**

Nunca o contrário.

---

# MODO DE DIAGNÓSTICO

Implemente um módulo isolado de diagnóstico Bluetooth seguindo a arquitetura existente.

Não quero lógica experimental espalhada pelos módulos dos SP624E.

Algo conceitualmente semelhante a:

```text
BleDiagnosticManager
```

é desejável, mas escolha nomes e organização compatíveis com o projeto.

O modo deve estar desligado por padrão ou ser acessível somente explicitamente.

Analise qual mecanismo faz mais sentido:

- configuração;
- flag de build;
- modo runtime;
- console serial;
- endpoint interno;
- página de diagnóstico já existente;
- outro mecanismo que o projeto já utilize.

Para a primeira versão, **console serial é perfeitamente aceitável e provavelmente preferível**, caso seja simples e não invasivo.

---

# COMANDOS DE DIAGNÓSTICO

Quero conseguir executar operações equivalentes a:

```text
diag help

diag status

diag scan start
diag scan stop
diag scan list
diag scan clear

diag target <address>

diag connect
diag disconnect

diag gatt

diag read <service_uuid> <characteristic_uuid>

diag subscribe <service_uuid> <characteristic_uuid>
diag unsubscribe <service_uuid> <characteristic_uuid>

diag write <service_uuid> <characteristic_uuid> <hex>

diag write_nr <service_uuid> <characteristic_uuid> <hex>
```

Não precisa seguir essa sintaxe literalmente se já existir padrão melhor no projeto.

O importante é oferecer essas capacidades.

---

# 1. BLE SCAN

Implemente scan detalhado.

Para cada dispositivo, quando disponível, quero descobrir:

```text
Name
Address
Address type
RSSI
Service UUIDs
Manufacturer Data
Service Data
TX Power
Appearance
Advertisement payload
Scan Response payload
```

Dados brutos importantes devem ser exibidos também em HEX.

Exemplo:

```text
[BLE-DIAG][SCAN]

Name: CAR-LED
Address: AA:BB:CC:DD:EE:FF
Address type: random
RSSI: -43 dBm

Services:
FFE0

Manufacturer:
01 02 03 04 ...

Advertisement RAW:
...
```

Evite flood de log.

Durante uma sessão, deduplique dispositivos.

Se o mesmo dispositivo aparecer 100 vezes, não quero 100 blocos completos.

O comando `scan list` deve mostrar os dispositivos encontrados com informações suficientes para eu comparar o resultado com:

```text
kit desligado
vs
kit ligado
```

Isso provavelmente será nosso primeiro método para identificá-lo.

---

# 2. SELEÇÃO DO TARGET

Permita escolher explicitamente o dispositivo que será investigado.

Idealmente pelo endereço Bluetooth.

Exemplo:

```text
diag target AA:BB:CC:DD:EE:FF
```

O target não deve interferir nas regras que identificam os SP624E.

---

# 3. CONEXÃO

Permita conectar manualmente ao target.

Registre:

```text
Address
connection start
connection success
connection failure
error/status
connection handle
MTU
connection parameters
disconnect reason
```

Não derrube os SP624E para abrir essa conexão.

Primeiro investigue:

- limite atual de conexões;
- configuração do Bluetooth;
- memória disponível;
- impacto de uma terceira conexão.

Se o sistema atualmente não suportar uma terceira conexão com segurança, documente e proponha a alteração mínima necessária.

---

# 4. GATT DISCOVERY

Após conectado:

```text
diag gatt
```

deve fazer discovery completo e mostrar algo semelhante:

```text
Service
  UUID
  handle/range

Characteristic
  UUID
  handle
  value handle

Properties
  READ
  WRITE
  WRITE_NO_RESPONSE
  NOTIFY
  INDICATE

Descriptors
  UUID
  handle
```

Destaque principalmente characteristics com:

```text
WRITE
WRITE WITHOUT RESPONSE
NOTIFY
INDICATE
```

pois provavelmente são as mais relevantes para engenharia reversa.

Não conclua que uma characteristic é de RGB apenas por suas propriedades.

---

# 5. READ

Permita leitura manual de characteristics compatíveis.

Exemplo conceitual:

```text
diag read <service> <characteristic>
```

Mostrar:

```text
Length
HEX
ASCII
```

Não interprete bytes arbitrariamente.

---

# 6. NOTIFY / INDICATE

Permita subscribe.

Quando receber dados:

```text
[BLE-DIAG][NOTIFY]

timestamp
service
characteristic
length

HEX:
...

ASCII:
...
```

Permita unsubscribe também.

Isso será útil caso o controlador envie estado ou respostas.

---

# 7. WRITE

Essa funcionalidade é essencial para posteriormente reproduzirmos os comandos do aplicativo.

Permita enviar manualmente um pacote HEX.

Exemplo:

```text
diag write FFE0 FFE1 7E000503FF0000EF
```

e:

```text
diag write_nr FFE0 FFE1 7E000503FF0000EF
```

Suporte:

- Write With Response
- Write Without Response

Faça validações de:

- HEX;
- tamanho;
- characteristic;
- propriedades;
- estado da conexão.

Sempre registre exatamente os bytes enviados.

---

# SEGURANÇA DO REVERSE ENGINEERING

NÃO implemente:

- fuzzing;
- brute force;
- geração aleatória de pacotes;
- teste automático de comandos;
- varredura automática de valores;
- escrita automática em todas characteristics.

Writes devem acontecer **somente quando eu explicitamente fornecer os bytes**.

Nesta etapa quero observar, não adivinhar.

---

# LOGGING

Padronize logs:

```text
[BLE-DIAG][SCAN]
[BLE-DIAG][CONNECT]
[BLE-DIAG][DISCONNECT]
[BLE-DIAG][GATT]
[BLE-DIAG][READ]
[BLE-DIAG][WRITE]
[BLE-DIAG][NOTIFY]
[BLE-DIAG][ERROR]
```

Quando possível:

```text
[BLE-DIAG][12.432s][WRITE]
```

Não gere esses logs em volume relevante quando o diagnóstico estiver desativado.

---

# STATUS

Quero conseguir obter algo equivalente a:

```text
BLE DIAGNOSTIC

Diagnostic mode: enabled

LEFT:
connected

RIGHT:
connected

Target:
AA:BB:CC:DD:EE:FF

Target connection:
connected

MTU:
...

Active connections:
...

Free heap:
...

Minimum free heap:
...

Scan:
inactive

Last diagnostic error:
none
```

Use os conceitos/nomenclaturas reais encontrados no projeto.

---

# MEMÓRIA E RECURSOS

Investigue especificamente:

- máximo de conexões Bluetooth configurado;
- número de conexões já utilizadas;
- heap antes do diagnóstico;
- heap durante scan;
- heap após terceira conexão;
- stack/task adicional, caso exista;
- possíveis riscos de watchdog;
- impacto do scan enquanto SP624E estão conectados.

Se o scan contínuo puder prejudicar as conexões existentes, não insista nele.

Pode usar scan manual ou em períodos curtos.

Exemplo:

```text
scan durante alguns segundos
↓
stop
```

O diagnóstico não precisa ficar procurando o controlador interno permanentemente nesta fase.

---

# NÃO ALTERAR DESNECESSARIAMENTE A RECONEXÃO DOS FARÓIS

A reconexão atual passou por várias correções e agora está funcionando bem no carro.

Portanto:

**não refatore essa parte apenas porque encontrou uma maneira arquiteturalmente mais bonita.**

Só altere código relacionado à reconexão atual se for absolutamente necessário para suportar o diagnóstico.

Se precisar modificar algo nessa área:

1. explique o motivo;
2. faça a menor alteração possível;
3. garanta compatibilidade;
4. valide regressões.

---

# BLUETOOTH CLASSIC

Comece investigando BLE porque é a hipótese inicial.

Se o controlador interno não aparecer:

não conclua que não é controlável.

Pode ser:

```text
Bluetooth Classic
BLE com advertising diferente
outro mecanismo
```

Nesse caso:

- documente a evidência;
- identifique se o ESP32/hardware atual suporta o próximo método necessário;
- não implemente uma grande stack de Bluetooth Classic sem termos evidência de que é necessária.

---

# PREPARAÇÃO PARA A INTEGRAÇÃO FUTURA

A arquitetura deve facilitar que futuramente criemos algo como:

```text
InteriorLightController
```

e possamos chegar a:

```cpp
setInteriorColor(r, g, b);
```

Então posteriormente o fluxo será:

```text
PWA seleciona RGB
       ↓
LightingManager
       ↓
 ┌───────────────┬────────────────┐
 ↓               ↓                ↓
LEFT           RIGHT           INTERIOR
```

Mas o `INTERIOR` será:

```text
best effort
```

Exemplo:

```cpp
headlights.setColor(r, g, b);

if (interior.isAvailable()) {
    interior.setColor(r, g, b);
}
```

Isso é apenas conceito.

**Não implemente ainda um `InteriorLightController` que invente UUIDs ou protocolos.**

---

# DOCUMENTAÇÃO DA PESQUISA

Crie um documento apropriado dentro da estrutura de docs do projeto para registrarmos a engenharia reversa.

Algo como:

```text
docs/interior-led-ble-research.md
```

Adapte ao padrão existente.

Deve conter:

## Hardware

```text
Kit:
AliExpress 1005003666858928

Controller:
unknown

Bluetooth type:
unknown
```

## Discovery

```text
Name:
Address:
Address type:
Advertisement:
Manufacturer:
Services advertised:
```

## GATT

Tabela ou estrutura contendo:

```text
Service
Characteristic
Properties
Descriptors
Observations
```

## Protocol investigation

Monte uma tabela:

| Ação | Service | Characteristic | Write type | Payload HEX | Resposta | Confirmado |
|---|---|---|---|---|---|---|
| vermelho | ? | ? | ? | ? | ? | não |
| verde | ? | ? | ? | ? | ? | não |
| azul | ? | ? | ? | ? | ? | não |
| branco | ? | ? | ? | ? | ? | não |
| brilho | ? | ? | ? | ? | ? | não |
| ligar | ? | ? | ? | ? | ? | não |
| desligar | ? | ? | ? | ? | ? | não |

Não preencha hipóteses como fatos.

---

# TESTES E VALIDAÇÃO

Primeiro garanta que com o diagnóstico desativado o comportamento seja equivalente ao atual.

Valide, dentro das possibilidades locais:

```text
build
lint/static analysis se existir
testes
firmware initialization
Bluetooth initialization
```

Depois valide o fluxo do diagnóstico.

Se não for possível validar Bluetooth real sem o carro/hardware:

não finja que validou.

Separe claramente:

```text
VALIDADO LOCALMENTE
```

de:

```text
PRECISA SER VALIDADO NO CARRO
```

---

# PRIMEIRO TESTE QUE FAREI NO CARRO

Prepare a implementação para este fluxo:

### Teste A

Kit interno desligado:

```text
diag scan clear
diag scan start
...
diag scan stop
diag scan list
```

Salvar resultado.

### Teste B

Ligar iluminação interna/controlador.

Repetir:

```text
diag scan clear
diag scan start
...
diag scan stop
diag scan list
```

Comparar os novos dispositivos.

Quando identificarmos um candidato:

```text
diag target <address>
diag connect
diag gatt
```

A partir desses dados faremos a próxima etapa da engenharia reversa.

---

# QUALIDADE DA IMPLEMENTAÇÃO

Quero código de produção mesmo sendo uma ferramenta de diagnóstico.

Evite:

- hacks temporários;
- código duplicado;
- delays bloqueantes;
- loops infinitos;
- memória sem liberar;
- callbacks inseguros;
- concorrência sem controle;
- alteração global desnecessária;
- hardcode do dispositivo interno;
- logs soltos sem padrão.

Use os padrões que o próprio projeto já estabelece.

---

# AUTONOMIA

Você tem autonomia para ajustar detalhes desta especificação se a arquitetura existente mostrar uma solução melhor.

Porém, mantenha os objetivos:

1. preservar completamente os faróis;
2. investigar o terceiro dispositivo;
3. permitir conexão/GATT/read/write/notify manual;
4. não inventar protocolo;
5. preparar o terreno para integração posterior.

Não faça uma refatoração ampla do projeto fora desse escopo.

---

# CRITÉRIOS PARA CONSIDERAR CONCLUÍDO

Só marque como concluído quando:

- tiver analisado a arquitetura real;
- firmware continuar compilando;
- comportamento existente dos faróis tiver sido preservado;
- diagnóstico estiver isolado;
- scan funcionar;
- resultados puderem ser visualizados;
- target puder ser definido;
- conexão manual estiver implementada;
- GATT discovery estiver implementado;
- read estiver implementado;
- subscribe/unsubscribe estiver implementado;
- write estiver implementado;
- write without response estiver implementado;
- erros forem tratados;
- memória/limites Bluetooth tiverem sido analisados;
- documentação tiver sido criada;
- testes possíveis localmente tiverem sido executados;
- estiver claro o que ainda depende de teste físico no carro.

---

# ENTREGA FINAL

Ao terminar, me apresente:

## 1. Estado anterior encontrado

Resumo da arquitetura real.

## 2. Implementação

O que foi criado e como funciona.

## 3. Arquivos

Arquivos criados e modificados.

## 4. Bluetooth

Informe:

```text
stack utilizada
max connections antes
max connections depois
conexões utilizadas normalmente
heap disponível
impacto esperado da terceira conexão
```

Somente informe valores que tenham sido realmente encontrados ou medidos.

## 5. Proteção dos SP624E

Explique por que o diagnóstico não interfere na lógica LEFT/RIGHT.

## 6. Comandos

Liste todos os comandos disponíveis e exemplos.

## 7. Validação

Separe:

```text
Validado localmente
```

de:

```text
Ainda depende de teste físico
```

## 8. Teste no carro

Me dê uma sequência exata de comandos para executar.

## 9. Próxima etapa

Explique quais informações preciso trazer do teste para que possamos identificar o protocolo do controlador interno.

Não considere como resolvido o protocolo do LED interno até termos evidências obtidas do controlador real.