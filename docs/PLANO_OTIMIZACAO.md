# NerdMiner v2 — Plano de Otimização e Correções

Este documento registra a análise do código-fonte (branch `fix/correcoes-e-plano-otimizacao`)
e organiza as mudanças em fases, da mais segura para a mais ambiciosa. Cada fase deve ser
medida no hardware real antes de avançar para a próxima.

## Diagnóstico resumido

O núcleo SHA256d por software (`src/ShaTests/nerdSHA256plus.cpp`) já está muito otimizado:

- midstate do primeiro bloco de 64 bytes calculado uma vez por job (`nerd_mids`);
- rounds 0–3 do segundo bloco pré-computados (`nerd_sha256_bake`, W0–W2 e W16–W17);
- segundo hash com `W[8..15]` constantes;
- *early-exit* no round 60 (`a7 & 0xFFFF != 0x32E7`) — os rounds 61–63 só rodam
  para ~1/65536 dos nonces.

Portanto o ganho de hashrate restante está **fora** da matemática: compilador, uso do
acelerador SHA de hardware e código de suporte (stratum, alocação, I/O serial).

## Fase 0 — Correções (feitas neste branch)

| Arquivo | Problema | Correção |
|---|---|---|
| `utils.cpp` `checkValid()` | `memcpy(diff_target, &target, 32)` copiava o **endereço** do ponteiro; laço `uint8_t i>=0` nunca termina normalmente (i vai a 255 → leitura fora do array); byte menor não encerrava a comparação | copia os bytes do target; laço `int`; comparação lexicográfica correta (menor → válido, maior → inválido, igual → continua) |
| `mining.cpp` `checkPoolConnection()` | `Serial.printf("%s", serverIP.toString())` passa objeto `String` em varargs (UB, pode travar na reconexão) | `.c_str()` |
| `stratum.cpp/.h` `checkError()` | `StaticJsonDocument<4096>` recebido **por valor** → 4 KB copiados na stack da task Stratum (12–15 KB) a cada chamada | `const&` |
| `stratum.cpp/.h` `tx_mining_submit()` | `mining_subscribe` e `mining_job` por valor (várias `String` copiadas a cada share) | `const&` |
| `mining.cpp` `restoreStat()` | `nvs_flash_init` repetido sem `nvs_flash_erase`; `nv_shares/nv_valids/nv_crc` não inicializados; `nvs_open` sem checagem de erro | erase antes do re-init; variáveis zeradas; handle validado |
| `mining.cpp` `saveStat()` | nunca chamava `nvs_commit()` | commit após gravar |
| `mining.cpp` envio de share | dump hexadecimal do hash a 115200 baud bloqueava a task Stratum alguns ms por share | só com `-D DEBUG_MINING` |

Nenhuma dessas mudanças altera o algoritmo de mineração; o hashrate esperado é igual ou
ligeiramente melhor (menos trabalho na task Stratum).

## Fase 1 — Compilador (baixo risco, ganho potencial 10–25 %)

O core Arduino‑ESP32 compila tudo com `-Os`. Código de rounds desenrolados ganha com
`-O2/-O3` (alocação de registradores, funnel‑shift `SRC`/`SSR` do Xtensa para `ROTR`).

1. Em `nerdSHA256plus.cpp`, anotar as funções quentes:
   ```cpp
   __attribute__((optimize("O3"))) IRAM_ATTR bool nerd_sha256d_baked(...)
   ```
   (ou `#pragma GCC optimize("O3")` no topo do arquivo, que o autor deixou comentado).
2. Alternativa por ambiente no `platformio.ini`: `build_flags = -O2` (afeta `src/` e libs
   compiladas pelo PlatformIO; os `.a` do IDF continuam `-Os`).
3. Testar também `-fno-stack-protector`, `-fomit-frame-pointer` (geralmente já ativos).

**Como medir:** hashrate no display após 5 min estável, mesma pool, mesma placa, antes/depois.
Registrar em `docs/BENCHMARKS.md` (placa, env, flags, KH/s).

## Fase 2 — Camada stratum / sistema (baixo risco)

1. **Parse único por mensagem.** Hoje cada linha da pool passa por `deserializeJson`
   duas vezes (`parse_mining_method` + `parse_mining_notify`/`set_difficulty`/`extract_id`).
   Refatorar para parsear uma vez e despachar pelo `doc` já carregado.
2. **Pool estático de jobs.** `std::make_shared<JobRequest>` (~250 B) ~10×/s por worker e
   `JobResult` por job geram churn de heap sem MMU. Substituir por ring buffer fixo de
   8 slots por fila (`JobRequest` reutilizado; `JobResult` embutido no slot).
3. **Contadores atômicos.** `templates`, `shares`, `valids`, `hashes` são escritos/lidos por
   tasks diferentes; usar `std::atomic<uint32_t>` (custo zero no Xtensa para 32 bits).
4. **Wi‑Fi modem sleep configurável.** Não há `WiFi.setSleep(false)`: o rádio dorme entre
   DTIMs e adiciona até ~100 ms de latência em `mining.notify` e `mining.submit`. Desligar
   custa ~50–60 mA; expor como opção no WiFiManager (default: ligado, para manter consumo).
5. **Nonce direto, sem passar pela memória.** O worker escreve o nonce em
   `sha_buffer+76` e `nerd_sha256d_baked` lê de volta com `GET_UINT32_BE` (4 shifts).
   Passar `uint32_t nonce_be` como parâmetro elimina store+load+swap por hash (~1–2 %).

## Fase 3 — Pré‑computação adicional do message schedule (risco baixo, ~1 %)

No segundo bloco do primeiro hash, apenas `W[3]` (nonce) varia:

- `W[18] = S1(W16) + W11 + S0(W3) + W2` → `S1(W16)+W11+W2` é constante → `bake[15]`
- `W[19] = S1(W17) + W12 + S0(W4) + W3` → `S1(W17)+W12+S0(W4)` constante → `bake[16]`
- `W[20] = S1(W18) + W13 + S0(W5) + W4` → `W13 = S0(W5) = 0`, `W4` constante

Aumentar `bake[]` de 16 para 18 palavras e usar em `nerd_sha256d_baked`. Validar com
`SHA256_VALIDATE` (compara contra mbedTLS) antes de habilitar.

## Fase 4 — Interleaving HW + SW no worker de hardware (maior ganho, maior risco)

`minerWorkerHw` (S2/S3/C3 e ESP32 clássico) faz *busy‑wait* em
`while (REG_READ(SHA_BUSY_REG))` 2–3 vezes por nonce. Durante essa espera o core não produz
nada. Proposta:

```
loop por nonce:
  escrever digest/midstate + bloco no acelerador; START
  ── executar ~32 rounds do hash SOFTWARE de outro nonce ──
  wait_idle; alimentar segundo bloco; START
  ── executar os rounds restantes do hash software ──
  wait_idle; ler digest HW (filtro 16 bits)
```

Pontos de atenção:
- o estado do hash por software precisa ser dividido em duas funções (`part1`/`part2`)
  com o `W[]` e `A[]` mantidos em registradores/stack entre as metades;
- manter `esp_sha_acquire_hardware()` fora do loop (já é assim);
- medir separadamente em S3 (dual core, HW rápido) e C3 (single core RISC‑V), pois no C3
  o worker HW é o único e o ganho tende a ser maior.

Expectativa: dezenas de % sobre o worker HW; validar com `VALIDATION` ligado por alguns
minutos antes de desligar.

## O que **não** mexer

- O loop SHA em si (rounds desenrolados, early‑exit). Reescrever em assembly traz pouco
  frente ao risco.
- `NONCE_PER_JOB_*` e as filas de 4 jobs: já equilibram latência de troca de job com
  overhead de lock.

## Ordem recomendada

Fase 0 → Fase 1 (medir) → Fase 2 → Fase 3 (medir) → Fase 4 (medir por chip).

## Validação

- `-D SHA256_VALIDATE` / `VALIDATION`: compara o hash otimizado contra mbedTLS/HW.
- Compilar os envs principais: `ESP32-S3-devKitv1`, `NerdminerV2`, `ESP32-C3-super-mini`,
  `ESP32-devKitv1`.
- Rodar 1 h em pool pública e conferir que shares aceitos/rejeitados se mantêm.
