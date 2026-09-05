# Descoberta BLE dos SP624E

## KNOWN FROM REFERENCE

Referência técnica: implementação [BanlanX v3 do UniLED](https://github.com/monty68/uniled/blob/main/custom_components/uniled/lib/ble/banlanx3.py) e seu [matcher BLE](https://github.com/monty68/uniled/blob/main/custom_components/uniled/lib/ble/device.py). Estes itens orientam a identificação; ainda precisam ser confrontados com o hardware local.

| Campo | Valor de referência |
|---|---|
| Modelo | SP624E (`0x624E`) |
| Protocolo | BanlanX v3 |
| Manufacturer ID | `0x5053` (`20563`), lido em little-endian no AD type `0xFF` |
| Prefixo do Manufacturer Data | `0F 00` |
| Serviço esperado | `0000FFE0-0000-1000-8000-00805F9B34FB` |
| Característica esperada | `0000FFE1-0000-1000-8000-00805F9B34FB` |
| Canais | RGBW / quatro cores |
| Microfone interno | Não |

Critério local de identificação antes da conexão: Manufacturer ID `0x5053` **e** payload iniciado por `0F 00`. FFE0 anunciado sem essa assinatura é somente `GENERIC_FFE0_DEVICE`.

## CONFIRMED ON OUR HARDWARE

Status: **APROVADO** em teste físico executado em 2026-08-08. Dois dispositivos confirmados chegaram a `READY` e permaneceram conectados simultaneamente por mais de 90 segundos.

### Scan e dispositivos

| Item | Resultado real |
|---|---|
| Scan ativo / scan response | OK / OK, 15 s |
| Dispositivos BLE únicos | 14 |
| SP624E confirmados | 2 |
| SP624E_1 endereço, tipo e RSSI final do scan | `FF:FF:11:CD:A0:60`, PUBLIC, `-97 dBm` |
| SP624E_1 Manufacturer / raw | `0x5053`, `0F 00 FF FF 11 CD A0 60`; ADV `02 01 06 03 02 B0 FF 0B FF 53 50 0F 00 FF FF 11 CD A0 60`; scan response `07 09 53 50 36 32 34 45` |
| SP624E_2 endereço, tipo e RSSI final do scan | `FF:FF:11:CD:AC:FA`, PUBLIC, `-94 dBm` |
| SP624E_2 Manufacturer / raw | `0x5053`, `0F 00 FF FF 11 CD AC FA`; ADV `02 01 06 03 02 B0 FF 0B FF 53 50 0F 00 FF FF 11 CD AC FA`; scan response `07 09 53 50 36 32 34 45` |

### Perfis GATT reais

| Item | SP624E_1 | SP624E_2 |
|---|---|---|
| Connection handle | `0` | `1` |
| Serviços e handles | FFE0 `0x0001–0x0004`; `5833ff01-9b8b-5191-6142-22a4536ef123` `0x0005–0xFFFF` | Iguais ao SP624E_1 |
| FFE0 | FOUND | FOUND |
| FFE1 value handle | `0x0003` (definition `0x0002`) | `0x0003` (definition `0x0002`) |
| Propriedades FFE1 | `0x1C`: WRITE, WRITE_NO_RESPONSE, NOTIFY | `0x1C`: WRITE, WRITE_NO_RESPONSE, NOTIFY |
| Outros characteristics | `5833ff02...` value `0x0007` WRITE; `5833ff03...` value `0x0009` NOTIFY | Iguais ao SP624E_1 |
| Descriptors | CCCD `0x2902` em `0x0004` e `0x000A` | CCCD `0x2902` em `0x0004` e `0x000A` |

### Teste simultâneo e segurança

| Item | Resultado real |
|---|---|
| Duas conexões simultâneas por 60 s | PASSED; ambos `READY` em todos os checkpoints de 5 s |
| Duração observada | SP624E_1: 98 s; SP624E_2: 92 s no último checkpoint capturado |
| Desconexões após link estabelecido | Nenhuma |
| Falhas de estabelecimento | SP624E_2 teve uma falha `0x23E` (HCI `0x3E`, Connection Failed to Be Established); a única repetição permitida conectou com sucesso |
| RSSI no checkpoint de 90 s | SP624E_1 `-96 dBm`; SP624E_2 `-99 dBm` |
| Panic, watchdog ou reboot inesperado | Nenhum durante 115 s de captura |
| GATT writes de aplicação | ZERO, confirmado em código e log |

Essa evidência pertence ao firmware `0.2.0`, que não iniciava pairing nem fazia
qualquer GATT write. A fase `0.3.0` preserva a mesma identificação estrita, mas
habilita apenas writes controlados de CCCD, query, teste e restauração, sempre
depois das novas barreiras documentadas em `sp624e-protocol.md`.

### Endereços para persistência futura

- SP624E_1: `FF:FF:11:CD:A0:60`
- SP624E_2: `FF:FF:11:CD:AC:FA`

Esses rótulos ainda não significam LEFT/RIGHT. A associação física deve ser feita por provisionamento posterior, nunca por RSSI.

## Continuação no firmware 0.3.0

O perfil físico acima é a base para a query e o provisionamento. Resultados de
notifications, bytes de estado, mapping físico e restauração são registrados
separadamente em `sp624e-protocol.md` e `provisioning.md`, evitando misturar a
evidência read-only de `0.2.0` com os testes de controle de `0.3.0`.

Validação `0.3.0` concluída: os dois foram conectados simultaneamente, seus CCCDs
foram habilitados, estados foram lidos e o mapping LEFT/RIGHT sobreviveu a dois
reboots. Falhas `0x23E` ocorreram apenas durante estabelecimento com RSSI entre
aproximadamente -89 e -101 dBm; as repetições limitadas recuperaram. Não houve
desconexão durante os testes finais, panic, watchdog ou reboot inesperado.
