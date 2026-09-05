# Protocolo SP624E / BanlanX v3

## Reference-derived

Fonte técnica: implementação atual
[BanlanX v3 do UniLED](https://github.com/monty68/uniled/blob/main/custom_components/uniled/lib/ble/banlanx3.py)
e [transporte BLE](https://github.com/monty68/uniled/blob/main/custom_components/uniled/lib/ble/device.py).
Estes dados orientam a implementação; não são evidência do hardware local.

| Operação | Payload |
|---|---|
| Query | `1D 00` |
| Power on/off | `0F 01 01` / `0F 01 00` |
| Brightness | `12 01 LL` |
| Solid / white | `15 01 63` / `15 01 CC` |
| RGB + nível | `13 04 RR GG BB LL` |
| White | `21 02 LL FF` |
| Speed 1–10 | `14 01 SS` |
| Mode 0–2 | `16 01 MM` |

FFE1 é a característica de transporte. Notifications são habilitadas escrevendo
`01 00` no CCCD. A primeira notification tem `packet,total,payload_len,payload`;
as seguintes, `packet,payload_len,payload`. Pacotes duplicados, fora de ordem,
truncados ou maiores que 64 bytes são descartados.

O payload remontado usa offsets 0–9 para power, brightness, speed, chip order,
effect, mode, RGB e gain. O terceiro byte a partir do fim é input; o penúltimo é
white; o último permanece reservado. O payload bruto é preservado.

## Hardware-confirmed

Teste físico concluído em 2026-08-08 com firmware `0.3.0`.

| Evidência | Estado | Resultado real |
|---|---|---|
| CCCD `01 00` com resposta | CONFIRMED | Status GATT 0 nos dois, handle `0x0004` |
| Query `1D 00` | CONFIRMED | Status GATT 0 e resposta nos dois |
| Notification/reassembly | CONFIRMED | Resposta real em dois fragments, remontada sem perda |
| Decode de estado | CONFIRMED | Campos e raw coerentes nos dois |
| Solid `15 01 63` | CONFIRMED | Estado `effect=0x63` e mudança física |
| RGB `13 04 ...` | CONFIRMED | Vermelho, azul e verde confirmados fisicamente |
| Power `0F 01 ...` | NOT_TESTED | ON foi aceito, mas não houve transição de power |
| Brightness `12 01 ...` | CONFIRMED | `64 → 255` confirmado por query durante recuperação |
| White `21 02 ...` | NOT_TESTED | Nível já era 255; sem transição isolada conclusiva |
| Speed `14 01 ...` | NOT_TESTED | Não foi necessário |
| Mode `16 01 ...` | NOT_TESTED | Mode já era 0; write redundante foi removido |
| Restauração + query | CONFIRMED | Payloads finais iguais aos snapshots |

`NOT_TESTED` não significa falha: comandos de restauração dependem do estado
original e não são enviados desnecessariamente.

### Notifications reais fragmentadas

Exemplo do RIGHT (`A0:60`) recebido pelo Windows BLE e reproduzido pelo parser C:

```text
01 19 11 01 FF 0A 00 CC 00 FF 00 00 10 02 03 FF 00 00 00 FF
02 08 00 00 00 FF 00 01 FF 00
```

Payload remontado (`25` bytes):

```text
01 FF 0A 00 CC 00 FF 00 00 10 02 03 FF 00 00 00 FF 00 00 00 FF 00 01 FF 00
```

O primeiro fragmento declara total `0x19` e carrega `0x11` bytes; o segundo é
packet 2 e carrega `0x08` bytes.

### Estados iniciais e finais

LEFT `FF:FF:11:CD:AC:FA` (`28` bytes):

```text
01 FF 09 00 CC 00 FF 00 00 10 01 04 FF 00 00 00 FF 00 00 00 FF A2 00 00 00 01 FF 00
```

RIGHT `FF:FF:11:CD:A0:60` (`25` bytes):

```text
01 FF 0A 00 CC 00 FF 00 00 10 02 03 FF 00 00 00 FF 00 00 00 FF 00 01 FF 00
```

Ambos: power ON, brightness 255, effect `0xCC`, mode 0, RGB `255,0,0`,
white 255. Speed: LEFT 9, RIGHT 10. Os mesmos payloads foram recebidos depois
das restaurações finais.

### Ordem de rollback confirmada

O primeiro ensaio revelou que enviar `MODE 0` depois de `EFFECT 0xCC` selecionava
novamente `effect=0x02`. A proteção de mismatch abortou o provisionamento, e o
estado foi recuperado diretamente e confirmado byte a byte. A implementação
final restaura apenas os campos alterados: RGB + nível enquanto ainda sólido,
depois o efeito original por último; power somente se originalmente OFF. Essa
ordem passou nos testes vermelho, azul e verde para os dois dispositivos.

## Barreiras de segurança implementadas

- Exatamente dois devices confirmados antes da conexão.
- CCCD e query antes do snapshot; ambos os snapshots antes do primeiro visual.
- `READY_FOR_CONTROL` obrigatório para comandos que alteram estado.
- WRITE WITH RESPONSE, timeout de 2 s e serialização por dispositivo.
- Testes visuais de 2 s, nível `0x40`, sempre seguidos de restore/query/compare.
- Um retry máximo; mismatch persistente aborta os testes seguintes.
- Desconexão ativa `RESTORE_PENDING` e uma única recuperação controlada.
