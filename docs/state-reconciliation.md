# Desired State e reconciliação

## Modelo

Há uma Desired State global e duas Observed States independentes. Toda mudança
do usuário incrementa `generation`. Cada lado registra
`last_applied_generation` e `last_verified_generation`.

O grupo só fica `SYNCED` quando LEFT e RIGHT verificaram exatamente a geração
desejada. Outros estados são `UNINITIALIZED`, `DEGRADED`, `RECONCILING`,
`UNSYNCED` e `ERROR`.

## Strict Sync

`STRICT_SYNC_MODE=true`. Se um novo RGB chega com um lado indisponível, a
Desired State muda, mas nenhum lado recebe a geração nova. Quando ambos voltam
a READY, a mesma geração é despachada para as duas filas.

Uma simples recuperação é diferente: sem comando novo durante a queda, o lado
saudável não é tocado. O lado recuperado faz query, recebe apenas as correções
necessárias, faz outra query e então retorna a READY.

## Plano mínimo

- Estado já correto: zero write e log `RECONCILE: already synchronized`.
- Somente brilho diferente: `12 01 LEVEL`.
- RGB diferente: `13 04 R G B LEVEL`.
- Saída de outro efeito para solid: `15 01 63`, seguido somente do restante
  necessário.
- Diferença que exigiria power, white, speed, mode ou efeito não-solid isolado:
  não é corrigida automaticamente nesta versão.

Sucesso do write não basta. Toda reconciliação termina com `1D 00`, notification
completa, parse e comparação. Alteração externa gera `EXTERNAL_STATE_CHANGE` e
cooldown de 2 s antes de reconciliar novamente.

## Persistência

NVS namespace `sp624e`: `desired`, `desired_ver` e `restore_boot`. O debounce é
2 s e a gravação ocorre somente com ambos verificados, grupo `SYNCED` e comando
não temporário. `restore_on_boot=false` é persistido por padrão.
