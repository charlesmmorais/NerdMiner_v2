# Benchmarks de hashrate

Registre uma linha por medição. Procedimento: mesma placa, mesma pool, firmware
estável há ≥ 5 min, anotar o KH/s do display (ou a média de 3 leituras).

| Data | Placa / env | Branch / commit | Flags extras | KH/s | Obs. |
|---|---|---|---|---|---|
| | | `main` (baseline) | — | | |
| | | `perf/fase1-o3` | — | | |
| | | `perf/fase1-o3` | `-D NERD_SHA_NO_O3` | | deve igualar o baseline |

Para o ESP32 clássico, confira também no log do link que o IRAM não estourou
(`-O3` pode aumentar um pouco o tamanho das funções em `.iram1`).
