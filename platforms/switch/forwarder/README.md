# Home-screen forwarder / Forwarder na tela inicial

Goal: an installable **NSP** that puts a Minish Cap icon on the Switch **home
menu** which launches `sdmc:/switch/tmc/tmc_switch.nro` directly — as an
*Application* (full memory, no "hold R" trick needed). Optional; the homebrew
works fine from the Homebrew Menu without it.

> Objetivo: um **NSP** instalável que coloca um ícone do Minish Cap na **tela
> inicial** do Switch e abre o `sdmc:/switch/tmc/tmc_switch.nro` direto — como
> *Application* (memória total, sem o truque do "segurar R"). É opcional; o
> homebrew funciona normalmente pelo Homebrew Menu sem ele.

## Icon / Ícone

The `.nro` already carries the 256×256 icon (baked via `elf2nro --icon`), and
forwarder tools reuse it automatically — no separate icon needed.

> O `.nro` já carrega o ícone 256×256 (embutido via `elf2nro --icon`), e as
> ferramentas de forwarder reusam ele automaticamente — não precisa de ícone à parte.

## Methods / Métodos

Both ultimately use **hacBrewPack**, which needs the console's **`prod.keys`**
(dump with Lockpick_RWX). There is no keyless installable NSP.

> Ambos usam o **hacBrewPack** no fim, que precisa das **`prod.keys`** do console
> (dump com Lockpick_RWX). Não existe NSP instalável sem keys.

### A. On-device / No console (no PC keys / sem keys no PC) — simplest / mais simples
Run **[switch-nsp-forwarder](https://github.com/TooTallNate/switch-nsp-forwarder)**
on the Switch: point it at `sdmc:/switch/tmc/tmc_switch.nro`, set name/author,
pick the embedded icon — it generates and installs the forwarder right there
using the console's own keys.

> Rode o **switch-nsp-forwarder** no Switch: aponte para
> `sdmc:/switch/tmc/tmc_switch.nro`, defina nome/autor, escolha o ícone embutido
> — ele gera e instala o forwarder ali mesmo usando as keys do próprio console.

### B. On PC / No PC (ready `.nsp` / `.nsp` pronto) — needs keys on PC / precisa das keys no PC
Place `prod.keys` in `~/.switch/prod.keys` (or `%USERPROFILE%\.switch\`), then
use **[NTON](https://github.com/rlaphoenix/nton)** (it embeds the NRO + icon into
a self-contained forwarder NSP):

```sh
nton build path/to/tmc_switch.nro --name "The Minish Cap" --publisher "..." --icon icon.jpg
```

Produces `The Minish Cap [titleid][v0].nsp` → install with any title installer.

> Coloque `prod.keys` em `%USERPROFILE%\.switch\`, e use o **NTON** (ele embute o
> NRO + ícone num NSP forwarder autossuficiente). Gera um `.nsp` → instale com
> qualquer title installer.

## Status / what's needed — O que falta
1. Icon ✅ (baked into the `.nro`). / Ícone ✅ (já no `.nro`).
2. Method B only: dump `prod.keys` to the PC. / Só método B: dumpar `prod.keys` pro PC.
3. Run the tool to emit the `.nsp`. / Rodar a ferramenta para gerar o `.nsp`.

Sources: <https://github.com/rlaphoenix/nton> ·
<https://github.com/TooTallNate/switch-nsp-forwarder> ·
<https://nsp-forwarder.n8.io/>
