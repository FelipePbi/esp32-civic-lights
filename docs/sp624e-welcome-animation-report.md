# SP624E WELCOME ANIMATION REPORT

> HISTÓRICO — funcionalidade removida na versão `0.7.6` por decisão do usuário.
> O teste físico mostrou início vários segundos após ligar os faróis; firmware,
> API, PWA e runners específicos foram retirados. O conteúdo abaixo registra a
> implementação anterior e não descreve o sistema atual.

## Firmware

- Version: `0.7.5`
- Build full clean: PASS — ESP-IDF v6.0.2
- Binary: `build/sp624e_controller.bin`, `0x107b30` bytes
- App partition free: `0xf84d0` bytes (48%)
- Flash `0.7.4`: PASS — COM5 CH9102, ESP32-D0WD-V3 rev. 3.1, hashes verified

## Configuration

- Enabled default: YES
- Selected default: `red_welcome`
- Duration: 1000–6000 ms, step 250 ms; Preview does not persist
- NVS: namespace `welcome`, fixed 8-byte v2 encoding; v1 6-byte config migrates
  in memory and preserves existing selection
- Persisted after reboot: PASS — iPhone mostrou `disabled + premium_pulse` após
  reboot/reload; depois `enabled + red_welcome` foi salvo e permaneceu após reload
- Configurable duration persistence: PASS — `5000 ms` salvo pelo iPhone e mantido
  após recarregar a página
- Existing valid config preserved: YES

## Available Animations

- Disabled: YES, persistent toggle
- Red Welcome: YES, 1800 ms, recommended
- OEM White: YES, 1700 ms, gated by confirmed WHITE
- Red → White: YES, 1900 ms, gated by confirmed WHITE
- Premium Pulse: YES, 1700 ms
- Show Welcome: YES, 2800 ms
- Final segment: interpolates into captured Desired State

## Red Welcome

- Duration: 1800 ms
- Generated frames: observed in real run; final post-fix metrics pending
- Sent frames: PENDING hardware run
- Dropped/coalesced frames: PENDING hardware run
- Max queue depth: PENDING hardware run
- LEFT/RIGHT sync: PENDING visual validation
- Visual result: PENDING
- Restore: pre-fix intermittent mismatch reproduced; forced Desired State write fix
  implemented, built and flashed; post-fix visual proof pending
- Final verification: State Query required on both sides before `completed`

## OEM White

- Supported: runtime capability dependent
- Result: PENDING hardware/WHITE confirmation
- Duration: 1700 ms
- Restore: implemented; physical proof PENDING

## Red → White

- Supported: runtime capability dependent
- Result: PENDING hardware/WHITE confirmation
- Duration: 1900 ms
- Restore: implemented; physical proof PENDING

## Premium Pulse

- Result: PENDING visual validation
- Duration: 1700 ms
- Restore: implemented; physical proof PENDING

## Show Welcome

- Result: PENDING visual validation
- Duration: 2800 ms
- Restore: implemented; physical proof PENDING

## Startup Test

- State Query dos dois lados antes do start: obrigatório por gate
- Executed once: uma vez por ciclo de alimentação confirmado dos dois SP624E
- `0.7.4`: execução única por boot observada; iniciou após branco padrão e foi rejeitada
- `0.7.5`: reconciliação padrão fica pausada entre State Query e Welcome
- Execution delay após segunda State Query: PENDING measurement
- Animation: loaded from NVS
- Final Desired State: captured before animation and restored
- GROUP final: requires verified `SYNCED`

## Persistence Test

- Selected before reboot: `premium_pulse`, disabled
- Loaded after reboot: PASS (`disabled + premium_pulse`)
- Executed correct animation: PENDING janela visual final

## Disabled Test

- Saved disabled: PASS through physical API e reload da UI
- Reboot: PASS with faróis off
- Animation executed: NO event observed
- Expected: NO

## Reconnect e Power Cycle

- Reconnect isolado: não rearma; detector limpa o candidato ao recuperar o lado
- Power cycle: queda dos dois lados dentro de 3000 ms rearma exatamente uma vez
- Dois ciclos consecutivos em hardware: PENDING `0.7.5`

## User Override Test

- Animation running: PENDING hardware run
- User command: cancels session before new Desired State reconciliation
- Animation cancelled: lifecycle unit test PASS; hardware PENDING
- New Desired State applied: implementation complete; hardware PENDING
- Final GROUP: must be `SYNCED`

## Disconnect During Animation

- Disconnected side: PENDING hardware run
- Animation cancelled: implemented from disconnect callback
- Reconnect: normal Connection Manager recovery
- Desired State restored: enforced after recovery
- Final GROUP: PENDING hardware proof

## Web UI

- Selector: PASS build
- Toggle: PASS build
- Preview: API/UI implemented
- Stop: API/UI implemented
- Save: API/UI implemented
- Realtime status: WebSocket events and progress implemented
- iPhone: PENDING device validation

## BLE

- Disconnects: PENDING hardware run
- Unexpected desync: PENDING hardware run
- Command failures: PENDING hardware run
- Atomic dispatch: both queues populated before either worker is awakened
- Frame policy: 20 FPS monotonic, replaceable commands coalesced, late frame dropped

## Memory

- Initial heap: 199600 bytes before Wi-Fi
- Final heap: 49516 bytes with group READY
- Minimum heap: 44092 bytes with group READY

## Safety

- Panic: none observed
- Watchdog: none observed
- Unexpected reboot: none observed in captured cycles
- Incorrect persistent state: serialization tests PASS; hardware PENDING

## Software Validation

- C host tests: PASS
- REST parsing tests: PASS
- React/Vitest: PASS, 10 tests
- TypeScript/Vite production build: PASS
- ESP-IDF full-clean build: PASS
- Hardware runner: `tests/run-welcome-hardware-tests.ps1`

## Conclusion

Firmware `0.7.5` aguarda flash. O lifecycle rearma por ciclo de
alimentação dos dois faróis, preserva reconnect isolado e inicia após State Query,
antes da reconciliação padrão. Falta o passe visual de dois ciclos consecutivos.

## Open Issues

- Confirmar dois power cycles curtos, cada um com um único Welcome.
- Medir intervalo entre segunda State Query e primeiro frame.
- Confirmar restore, visual smoothness e iPhone realtime state.
- Run 20 skip boots only with faróis explicitly confirmed off.

## Recommended Next Step

Executar janela visual curta em `0.7.5` e desligar os faróis imediatamente após o
resultado. `enabled + premium_pulse + 3250 ms` está persistido.
