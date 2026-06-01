# The Minish Cap — port para Nintendo Switch

**Idioma:** [English](README.md) · Português

Um build nativo para Nintendo Switch (homebrew libnx) do
[port de PC](https://github.com/999sian/tmc) do Minish Cap (que por sua vez é
feito em cima da decompilação [`zeldaret/tmc`](https://github.com/zeldaret/tmc)).

O hardware do GBA é reimplementado **em software** (`libs/ViruaPPU` para o vídeo,
`libs/VirtuaAPU` + agbplay para o som), então isto **não é um emulador de GBA** —
é o jogo decompilado rodando nativamente na CPU do Switch, com o framebuffer
resultante exibido via switch-sdl2. Como o render é CPU pura → framebuffer, não
há nenhuma tradução de GPU envolvida.

> ⚠️ Você precisa fornecer sua própria ROM do jogo. Nenhuma é incluída.

---

## ▶️ Jogar (início rápido)

Você precisa de um Switch com CFW **Atmosphère** e o Homebrew Menu.

### 1. Copie dois arquivos para o SD

Crie a pasta `sdmc:/switch/tmc/` e coloque **os dois** nela:

```
sdmc:/switch/tmc/
  ├── tmc_switch.nro      ← o homebrew (o cache de assets vem embutido nele)
  └── <sua ROM USA>.gba   ← sua própria cópia do jogo
```

- A ROM precisa ser a versão **USA** do The Minish Cap
  (sha1 `b4bd50e4131b027c334547b4524e2dbbd4227130`).
- **O nome do arquivo não importa** — basta soltar seu `.gba` com qualquer nome.
  O port identifica a ROM correta pelo conteúdo, então `baserom.gba`, `tmc.gba`
  ou `Zelda Minish Cap.gba` funcionam igual. (Builds antigos exigiam o nome
  exato `baserom.gba`; isso não é mais necessário.)

### 2. Inicie com **memória total**

O port precisa de **memória em modo Aplicativo** — ele **não** roda como library
applet. Então abra o Homebrew Menu pelo caminho de "memória total":

- **Segure `R` ao abrir qualquer jogo instalado** para entrar no Homebrew Menu, **ou**
- Use um **forwarder NSP** (veja [Forwarder na tela inicial](#-forwarder-na-tela-inicial-opcional) abaixo).

> Abrir o homebrew pelo **Álbum** o executa como applet, com memória limitada — o
> jogo não vai iniciar assim. Use o método de segurar `R` ou um forwarder.

Depois, abra **The Minish Cap** pelo menu.

**A primeira execução** leva alguns segundos a mais (só uma vez): ela extrai as
páginas da ROM e copia o cache de assets embutido para o SD. Depois disso, toda
abertura é rápida. Saves, config e caches ficam todos em `sdmc:/switch/tmc/`.

---

## 🎮 Controles

Joy-Con / Pro Controller são mapeados para os botões do GBA automaticamente:

| Switch | GBA |
|---|---|
| A / B | A / B |
| L / R | L / R |
| + / − | Start / Select |
| D-Pad / Analógico esq. | D-Pad |

Dá para remapear em `sdmc:/switch/tmc/config.json`.

---

## 🔨 Compilar do código-fonte

**Requisitos:** [devkitPro](https://devkitpro.org/) com devkitA64 + libnx, mais
as portlibs `switch-sdl2`, `switch-zlib`, `switch-libpng`:

```sh
dkp-pacman -S switch-dev switch-sdl2 switch-zlib switch-libpng
```

Dependências header-only (`fmt`, `nlohmann/json`) e um shim read-only de
`sys/mman.h` já estão vendorizados em `platforms/switch/compat/`.

No **shell MSYS2 do devkitPro** (para `/opt/devkitpro` estar montado), na raiz do repo:

```sh
git submodule update --init                  # ViruaPPU + VirtuaAPU
python platforms/switch/gen_sources.py        # regenera sources.mk a partir do xmake.lua
make -C platforms/switch -j8                  # -> platforms/switch/tmc_switch.nro
```

- O `gen_sources.py` precisa de um Python no PATH; o MSYS2 do devkitPro pode não
  ter — rode ele de um shell normal primeiro, depois `make` no shell do dkP.
- `make GAME_VER=EU` compila a variante EU.
- Sem um cache pré-extraído em `romfs/`, o `.nro` fica menor e cai no caminho de
  extração no console (lento — veja abaixo).

---

## 📦 Pré-extrair o cache de assets (recomendado)

A extração de assets no console é **single-thread** (a `std::thread` do devkitA64
é instável, então o caminho paralelo fica desligado), o que faz a primeira
execução "fria" demorar vários minutos e parecer travada. Para evitar isso, o
cache é pré-extraído no PC e **embutido no romfs do `.nro`**, então o Switch pula
a extração inteira.

```sh
bash platforms/switch/build_extractor.sh   # compila o extrator num container
                                           # Docker gcc, roda na baserom.gba e
                                           # prepara o cache em romfs/ + dist/
make -C platforms/switch -j8               # rebuilda pro elf2nro embutir romfs/
```

O script coloca o `rom_mtime` gravado em `.asset_build_state.json` como `0`, para
o Switch aceitar o cache independente do mtime da ROM no SD (ele ainda confere o
tamanho da ROM + o formato do pack).

Na primeira inicialização, o `switch_romfs.c` copia `romfs:/assets` →
`sdmc:/switch/tmc/assets` se não existir, então o usuário só precisa do `.nro` +
a `baserom.gba`.

---

## 🖼️ Ícone customizado

O ícone da home/hbmenu é um JPEG 256×256 embutido no `.nro` (`elf2nro --icon`).
O `platforms/switch/icon.jpg` é usado se existir (senão o padrão do devkitPro).
Para regerar a partir de uma imagem, coloque-a em `platforms/switch/icon_src.png`
e redimensione para 256×256, ou use `gen_icon.py` para um ícone temático.

---

## 🏠 Forwarder na tela inicial (opcional)

Quer um ícone do Minish Cap na **tela inicial** do Switch (em vez de passar pelo
Homebrew Menu)? Isso precisa de um forwarder NSP instalável. Veja
[`forwarder/README.md`](forwarder/README.md). Em resumo:

- **No console (sem keys no PC):** o app `switch-nsp-forwarder` — aponte para
  `sdmc:/switch/tmc/tmc_switch.nro`; ele reusa o ícone embutido.
- **No PC:** [NTON](https://github.com/rlaphoenix/nton) ou hacBrewPack, que
  precisam das `prod.keys` do seu console no PC.

Um forwarder também abre o `.nro` como *Application* (memória total), então o
truque do "segurar R" não é necessário.

---

## 🩺 Resolução de problemas

- **Log de boot:** `sdmc:/switch/tmc/tmc.log` (sem buffer — sobrevive a um
  congelamento). Via USB/MTP, o Windows esconde a extensão `.log`, então ele
  aparece como `tmc` do tipo *txtfile* (há dois `tmc` — o txtfile é o log, o SAV
  é o seu save).
- **Primeira inicialização lenta / "travada":** é a extração única. Deixe terminar.
- **Região errada:** o log mostra a região detectada; use o build (`GAME_VER`) e
  a ROM correspondentes.
- **Emuladores:** hardware Tegra real roda os shaders GLES do switch-mesa
  (nouveau) que o switch-sdl2 gera. **Eden/yuzu não conseguem** (o recompilador
  de shaders Maxwell deles falha nos shaders do Mesa e crasha) — o app dá boot e
  roda, mas não exibe imagem. Use hardware real, ou o Ryujinx (recompilador de
  shaders diferente).

---

## 🧩 Como funciona (as partes específicas do Switch)

- `compat/SDL3/SDL.h` + `compat/sdl3_to_sdl2.h` — redirecionam todo
  `#include <SDL3/SDL.h>` para o SDL2 real e fazem o shim da API SDL3 que o port
  usa (o devkitPro só tem SDL2).
- `compat/sys/mman.h` — mmap read-only mínimo (malloc + read) para o pak loader.
- `compat/{fmt,nlohmann}/` — bibliotecas header-only vendorizadas.
- `switch_stubs.c` — stub de link para o único símbolo de patch do ViruaPPU que
  pulamos.
- `switch_romfs.c` — monta o romfs embutido e semeia o cache de assets no SD.
- `gen_sources.py` / `sources.mk` — espelham a lista de fontes do alvo `tmc_pc`
  do xmake.
- Adaptações `#ifdef __SWITCH__` espalhadas pelo `port/`: áudio por callback do
  SDL2, renderer por software, `chdir` para `/switch/tmc`, log em arquivo,
  gamepad por índice + campos de evento do SDL2, atalhos de teclado compilados
  para fora, update-check stubado, sem `execinfo`, `ParallelFor` serial, caminhos
  da ROM no SD, e o spam de debug por frame silenciado antes do loop do jogo.

---

## Créditos & links

- Decompilação: [zeldaret/tmc](https://github.com/zeldaret/tmc)
- Port de PC: [999sian/tmc](https://github.com/999sian/tmc)
- Hardware GBA em software: [ViruaPPU/VirtuaAPU](https://github.com/MatheoVignaud),
  [agbplay](https://github.com/ipatix/agbplay)
- Toolchain: [devkitPro](https://devkitpro.org/) ·
  [libnx](https://github.com/switchbrew/libnx) ·
  [switch-sdl2](https://github.com/devkitPro/SDL)
