# Ambiente ESP32

Inventário em 2026-08-08 (America/Sao_Paulo).

| Item | Valor |
|---|---|
| Windows | Windows 11 Pro 64 bits, build 26200 (`10.0.26200`) |
| ESP-IDF | v6.0.2 stable |
| Caminho IDF | `C:\Espressif\v6.0.2\esp-idf` |
| EIM | 0.17.4, config `C:\Espressif\tools\eim_idf.json` |
| Python do IDF | 3.11.15, `C:\Espressif\tools\python\v6.0.2\venv` |
| Python do Windows | 3.13.15 |
| Git | 2.52.0.windows.1 |
| CMake | 4.0.3 |
| Ninja | 1.12.1 |
| esptool | 5.3.1 |
| Toolchain | Espressif xtensa-esp-elf 15.2.0_20251204 |
| Porta | COM5, detectada dinamicamente |
| USB/serial | WCH USB-Enhanced-SERIAL CH9102 |
| VID/PID | `1A86:55D4` |
| Chip/target | ESP32-D0WD-V3 / `esp32` |
| Revisão | v3.1 |
| Recursos | Wi-Fi, Bluetooth, 2 cores, 240 MHz máximo, cristal 40 MHz |
| Módulo observado | WROOM-32 |
| Flash | 4 MB, DIO 40 MHz no firmware anterior |
| Driver | WCH `wch.cn` 2.1.2025.7 (`CH343SER_A64`, 2025-07-18); já funcional, nenhuma instalação necessária |
| Host BLE | NimBLE, Central + Observer, máximo 3 conexões |
| Controller BT | BLE-only, máximo 3 conexões; Classic Bluetooth desligado |

## Ativação

Scripts leem instalação selecionada em `C:\Espressif\tools\eim_idf.json` e executam `activationScript`. Manualmente:

```powershell
. 'C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1'
```

Depois use `idf.py build`, ou prefira scripts do projeto.

## Validação física

Executada em 2026-08-08:

- clean build: OK; firmware `0x63060` bytes, 61% da partição livre;
- flash a 460800 baud: OK; hashes verificados;
- boot ESP-IDF v6.0.2: OK;
- aplicação 0.1.0 e informações reais do chip: OK;
- NimBLE: OK;
- scan ativo de 10 segundos: OK, 11 dispositivos únicos;
- panic, watchdog ou reboot inesperado: nenhum;
- possível SP624E: nenhum dispositivo pôde ser confirmado pelos anúncios recebidos.

Essa validação corresponde ao firmware 0.1.0 anterior. A validação física do firmware 0.2.0, com parser estrutural, conexão dupla e descoberta GATT, é registrada em `ble-discovery.md`.

Em 2026-08-08, o firmware 0.2.0 também teve clean build, flash com hashes verificados e boot físico validados. Dois SP624E chegaram a `READY`; o teste de duas conexões simultâneas por 60 segundos foi aprovado e a captura observou ambos conectados por mais de 90 segundos. Detalhes em `ble-discovery.md`.
