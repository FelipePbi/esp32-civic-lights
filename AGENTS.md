# Contexto do projeto

Central ESP32 para dois SP624E via BLE. Escopo atual: firmware `0.7.6`,
Connection Manager, reconexão automática, filas independentes, Desired State,
reconciliação verificada, RX480E 433 MHz, LED indicador, SoftAP, REST, WebSocket
e frontend React local. Não implementar ainda OTA ou sincronização musical.

## Ambiente

- ESP-IDF v6.0.2 em `C:\Espressif`; target `esp32`.
- ESP32-D0WD-V3 rev. 3.1, flash 4 MB, WCH CH9102.
- LEFT `FF:FF:11:CD:AC:FA`; RIGHT `FF:FF:11:CD:A0:60`.
- NimBLE central/observer, BLE-only, máximo de três conexões.

## Princípios obrigatórios

1. Nunca enviar comando de grupo diretamente para somente um lado por conveniência.
2. Toda mudança desejada deve passar por Desired State e Group Controller.
3. Um controlador reconectado deve consultar estado antes de ser reconciliado.
4. Nunca considerar sucesso de GATT write como confirmação visual definitiva.
5. State Query + parser são a fonte de verificação.
6. Nunca criar reconnect loops sem backoff.
7. Não executar lógica pesada diretamente em callbacks BLE.
8. O mapping LEFT/RIGHT persistido não pode ser sobrescrito automaticamente.
9. Manter um único write GATT ativo por dispositivo.
10. Descartar comandos de gerações antigas após mudança ou reconnect.
11. Testes temporários nunca podem ser persistidos como Desired State normal.
12. Restaurar o estado original antes de iniciar outro teste ou encerrar.
13. Web API nunca escreve diretamente no BLE; toda mudança visual passa pelo
    Desired State e Group Controller.
14. WebSocket não é fonte de verdade; o firmware e sua verificação são.
15. Frontend deve funcionar sem internet e sem qualquer asset externo.
16. Color picker deve permanecer throttled e enviar o valor final imediatamente.
17. Não quebrar Strict Sync nem aplicar alteração em somente um lado por
    conveniência.
18. Animações temporárias nunca podem persistir frames como Desired State.
19. Nunca executar animação em somente um SP624E.
20. Comando do usuário sempre cancela animação ativa.
21. Disconnect cancela animação; conclusão exige restore verificado.
22. Headlight visuals devem refletir Observed State do respectivo lado.
23. Nunca usar Desired State como prova de que um farol mudou.
24. Visuais LEFT e RIGHT são independentes; nunca derivar ambos do estado global.
25. Home deve permanecer sem scroll no iPhone 16 Pro em `402 × 874`.
26. Não colocar controles avançados na Home.
27. Não adicionar dependência externa em runtime.
28. Não reintroduzir botão de configurações sem existir função de configurações.
29. RF remote nunca escreve BLE diretamente.
30. Button 1 é sempre WHITE; Button 2 é sempre RED; Button 3 é sempre Police.
31. Button 4 é configurável pelo usuário e persistido em NVS.
32. Mapping físico RF começa não configurado e nunca pode ser inferido nem
    sobrescrito automaticamente.
33. Police usa Animation Manager e nunca persiste frames ou estado ativo.
34. Police nunca continua em somente um SP624E; disconnect cancela e restaura.
35. Police possui timeout de segurança de 30 segundos.
36. LED físico indica iluminação não padrão/animação; WHITE confirmado é OFF.
37. Estado desconhecido/offline sem animação mantém LED OFF.
38. Não executar testes longos com faróis físicos; testes do carro sempre por último.
39. O diagnóstico BLE nunca tem prioridade sobre LEFT/RIGHT: o Connection
    Manager aborta scan e connect de diagnóstico antes de pedir o papel master.
40. O diagnóstico não toca registry, Desired State, filas, mapping nem
    identificação SP624E; ele mantém tabelas próprias.
41. Diagnóstico começa desabilitado a cada boot e só envia bytes fornecidos
    explicitamente pelo operador: nada de fuzzing, brute force, varredura de
    valores ou write automático.
42. Não registrar protocolo do LED interno como confirmado sem observação
    visual do hardware real.
43. Interior é best effort absoluto: nunca participa de Strict Sync, generation
    ou reconciliação, e nunca retém LEFT/RIGHT nem a resposta do PWA.
44. Interior não possui Observed State; `WRITE_NO_RESPONSE` nunca é tratado como
    confirmação de efeito físico.
45. Branco padrão do interior usa `SP624E_LIGHT_MODE_WHITE`, não comparação de
    RGB; apagado é `RGB(0,0,0)` (visual off), nunca um power-off inventado.
46. Interior só pede o papel master com LEFT e RIGHT em READY, e cede pelo guard
    do Connection Manager.

## Segurança e organização

- `SP624E_ALLOW_WRITES=1` somente atrás de assinatura, FFE0/FFE1, CCCD, query e
  estado apropriado da máquina de conexão.
- Writes automáticos isolados ficam limitados a query, solid, RGB e brightness,
  já confirmados fisicamente.
- RSSI baixo é diagnóstico, não gatilho de desconexão.
- Proteger registry, Desired State, filas, buffers de notification e métricas;
  não segurar mutex durante operações BLE longas.
- Interior em `main/interior/`, dirigido pela task `group_runtime`; nunca criar
  task dedicada para ele.
- Código BLE em `main/ble/`; protocolo e filas em `main/sp624e/`; grupo e
  reconciler em `main/sync/`; RF em `main/remote/`; indicador em
  `main/indicator/`; métricas e diagnóstico BLE em `main/diagnostics/`; console
  serial compartilhado em `main/console/`.
- O console UART0 sobe no boot em `runtime_console`; `sp624e_controller`
  registra o handler fallback e continua processando seus comandos em
  `group_runtime`.
- Testes: `.\tests\run-host-tests.ps1`; build final:
  `.\scripts\build.ps1 -FullClean`.
