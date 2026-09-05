# Provisionamento LEFT / RIGHT

## Resultado físico

Provisionamento concluído em 2026-08-08 com firmware `0.3.0`.

| Lado | Endereço | Tipo | Validação física | Restauração |
|---|---|---|---|---|
| LEFT | `FF:FF:11:CD:AC:FA` | PUBLIC | Azul confirmado pelo usuário | Payload idêntico ao snapshot |
| RIGHT | `FF:FF:11:CD:A0:60` | PUBLIC | Vermelho confirmado pelo usuário | Payload idêntico ao snapshot |

O firmware também executou vermelho no primeiro device da inicialização e azul
no segundo, sempre restaurando antes da entrada serial. A confirmação física foi
repetida com janelas longas porque o veículo estava na garagem: vermelho por
120 s, azul por 60/120 s e verde nos dois por 90 s.

## Persistência NVS

- Namespace: `sp624e`.
- Chave `mapping`: blob binário de 22 bytes.
- Conteúdo: magic `SPM1`, versão little-endian, tipo + endereço binário LEFT,
  tipo + endereço binário RIGHT.
- Mapping version: `1`.
- Chave `sync_done`: `u8`; evita repetir automaticamente o teste visual.
- `sp624e_mapping_clear()` apaga apenas o namespace interno do SP624E.

Evidência após reboot:

```text
SP624E MAPPING version=1 source=NVS
LEFT=FF:FF:11:CD:AC:FA RIGHT=FF:FF:11:CD:A0:60
Mapping=PROVISIONED Control=READY
Synchronized test already completed; no automatic visual writes on boot
```

## Teste sincronizado

Teste automático do firmware:

```text
LEFT RGB write=23076487 us
RIGHT RGB write=23098209 us
delta=21722 us
LEFT_RESTORE=OK
RIGHT_RESTORE=OK
SYNCHRONIZED TEST PASSED NVS=SAVED
```

Repetição visual longa, confirmada pelo usuário:

```text
Cor: verde, nível 0x40
Duração: 90 s
Delta RGB: 229 us
LEFT durante:  effect=0x63 RGB=0,255,0 brightness=64
RIGHT durante: effect=0x63 RGB=0,255,0 brightness=64
LEFT restore exact: true
RIGHT restore exact: true
```

Nenhum estado vermelho, azul ou verde temporário foi persistido. No boot final,
os dois payloads continuaram iguais aos snapshots originais.
