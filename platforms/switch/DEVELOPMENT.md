# Guia de desenvolvimento — Minish Cap Switch port

Guia interno de **como o projeto funciona e como modificá-lo**. Para instalar/jogar
e para o build básico, veja [`README.md`](README.md) / [`README.pt-BR.md`](README.pt-BR.md).
Para o roadmap e tarefas, veja [`TODO.md`](TODO.md) (espelhado nas issues do fork).

---

## 1. Visão geral da arquitetura

Não é um emulador de GBA. É o **decomp do jogo rodando nativo** no CPU do Switch:

- **PPU em software** (`libs/ViruaPPU`) compõe o framebuffer por scanline no CPU.
- **APU/som em software** (`libs/VirtuaAPU` + agbplay) mixa no callback de áudio do SDL2.
- O framebuffer é apresentado via **switch-sdl2** — como tudo é CPU→framebuffer,
  **não há tradução de GPU** (por isso roda em hardware real, mas não em Eden/yuzu).
- A camada de portabilidade vive em `port/` com adaptações `#ifdef __SWITCH__`.

---

## 2. Repositórios e branches

Remotes (ver `git remote -v`):

| Remote | Repo | Papel |
|---|---|---|
| `origin` | `999sian/tmc` | PC port (nossa base) |
| `upstream` | `MatheoVignaud/tmc` | fork divergente (NÃO mergear em massa) |
| `hayato` | `HayatoG/tmc` | **nosso fork** — onde vivem branches/PRs/issues/releases |

Branches no fork:

| Branch | Papel |
|---|---|
| **`switch-port`** | **default / integração** — recebe os PRs de feature |
| `widescreen` | feature do widescreen 288px ("Half A") — PR #2 → `switch-port` |
| `virtuappu-fps` | multithreading do PPU (já contido em `switch-port`) |
| `master` | decomp puro, sem o port |

---

## 3. Build

Pré-requisitos e passos completos: seção **"Build from source"** do `README.md`.
Resumo dentro do shell **MSYS2 do devkitPro**:

```sh
git submodule update --init
bash platforms/switch/build.sh           # aplica patches, gera sources, faz make
# -> platforms/switch/tmc_switch.nro
```

O `build.sh` é o caminho recomendado porque:
- **aplica os patches do submódulo de forma idempotente** (ver §5),
- roda `gen_sources.py` (sincroniza `sources.mk` com `xmake.lua`),
- chama `make -j$(nproc)`.

`make GAME_VER=EU` builda a variante EU. Sem cache pré-assado em `romfs/`, o `.nro`
cai na extração on-device (lenta) — veja "Pre-baking" no README.

---

## 4. Onde mexer (mapa de modificação)

### Largura widescreen
- `platforms/switch/Makefile` → `-DMODE1_GBA_WIDTH=N` (240 = nativo, 288 = atual).
- ⚠️ **Mudar esse define exige rebuild limpo** — o `make` não detecta mudança de
  DEFINES. Rode `make -C platforms/switch clean` antes.
- O "Phase 2" (widescreen de verdade) é mudança de engine — ver `WIDESCREEN_PLAN.md`
  e issue #8.

### Overlay de settings (botão Minus) e FPS counter
- Abertura/binding do Minus: `port/port_bios.c` (~l.82, `Port_DebugMenu_OpenSettings`).
- Conteúdo/itens do menu: `port/port_debug_menu.cpp` (`BuildDisplaySettingsPage`).
- Render do box: `Port_DebugMenu_Render` no mesmo arquivo (geometria vem de `charW`).
- Fonte de texto: `platforms/switch/compat/sdl3_debug_text.c` (8x8, 1:1, sem escala).
- FPS counter: `port/port_ppu.cpp` (~l.485, desenhado em `(8,8)`).
- Soft-slots: `port/port_softslots.c` (~l.682).
- Tarefas relacionadas: issues #3 (aumentar overlay), #5 (cantos), #6 (tamanho).

### Input / bindings
- Poll e mapeamento: `port/port_runtime_config.cpp` e `port/port_bios.c`.
- Rebind pelo usuário: `sdmc:/switch/tmc/config.json`.
- Stick analógico → Link: issue #4.

### Pipeline de assets / romfs
- Pré-assar cache no PC: `platforms/switch/build_extractor.sh` (Docker gcc) → `romfs/`.
- Seeding no boot: `platforms/switch/switch_romfs.c` (copia `romfs:/assets` → SD).

### Ícone / forwarder
- Ícone: `platforms/switch/icon.jpg` (+ `gen_icon.py`). Forwarder NSP: `forwarder/`.

---

## 5. Sistema de patches do ViruaPPU (importante)

`libs/ViruaPPU` é um **submódulo** com `ignore = dirty`. O build do **PC (xmake)**
aplica `port/patches/viruappu-*.patch` automaticamente; o build do **Switch NÃO** —
ele compila o working tree do submódulo. O `build.sh` aplica o
`port/patches/switch-parallel-render.patch` de forma **idempotente** (testa
`--reverse --check` antes), então um checkout limpo reproduz o PPU multithread sem
deixar mudança rastreada no parent.

> Consequência: PC e Switch podem divergir silenciosamente. Ao mexer no ViruaPPU,
> capture a mudança como patch em `port/patches/` e documente se ela é aplicada no
> Switch, no PC, ou nos dois.

---

## 6. Fluxo de contribuição

1. **Branch** a partir de `switch-port` (ex.: `feature/fps-corners`, referencie a issue).
2. Commits pequenos e descritivos. Referencie a issue (`#5`) na mensagem.
3. **PR → `switch-port`** (base default). Teste o merge: limpo se `mergeable: MERGEABLE`.
4. Issues do roadmap: **#3–#8** (features), **#10** (bug de áudio), **#9** (tracking).
5. **CI:** o GitHub Actions "CI Build" está **`disabled_manually`** por enquanto.
   Reativar: `gh workflow enable "CI Build" -R HayatoG/tmc`. O `ci.yaml` dispara em
   push pra `master`/`CI-Test` e em `pull_request` — se reativar, considere trocar o
   gatilho de `master` → `switch-port`.

---

## 7. Como cortar uma release

Convenção: tag **`switch-vX.Y.Z`** (note o prefixo `switch-`; tags `v*` disparariam
o `release.yaml`, que é do PC — por isso usamos `switch-*`). Asset: o `.nro`.

```sh
# 1. garanta que o commit do .nro está pushado pro fork
git push hayato <branch>

# 2. crie a release apontando pro commit do build, anexando o .nro
gh release create switch-vX.Y.Z -R HayatoG/tmc \
  --target <sha-do-commit> \
  --title "The Minish Cap — Nintendo Switch (...)" \
  --notes-file platforms/switch/_release_notes_vX.Y.Z.md \
  --prerelease \                       # builds experimentais (ex.: widescreen Half A)
  platforms/switch/tmc_switch.nro
```

- **`--prerelease`**: builds experimentais NÃO recebem o selo "Latest" na home do repo
  (aparecem só na aba *Releases*). Para promover a Latest:
  `gh release edit switch-vX.Y.Z --prerelease=false --latest`.
- Notas **bilíngues (EN + PT-BR)**, com o passo de instalação em `sdmc:/switch/tmc/`
  e o sha1 da ROM USA — siga o formato das releases anteriores.

---

## 8. Armadilhas de dev

- **Eden/yuzu não exibem** (o recompilador de shader deles quebra com os shaders
  Mesa/nouveau do switch-sdl2). Teste em **hardware real** ou Ryujinx.
- **`std::thread` é instável no devkitA64** → a extração de assets roda single-thread
  e o `ParallelFor` é serial; o PPU usa um **pool persistente** próprio
  (`switch_render_pool.c`), não spawn-por-frame.
- **Primeiro boot lento** = extração one-time; deixe terminar. Log em
  `sdmc:/switch/tmc/tmc.log` (unbuffered, sobrevive a freeze).
- **Memória**: o port precisa rodar como *Application* (segure **R** no hbmenu, ou
  use forwarder NSP) — não roda como library applet.
- **FPS ~43**: o gargalo medido é a **lógica do jogo**, não o PPU (threading do render
  não ajudou). Ver `PERF_DOSSIER.md` e a discussão nas issues #7/#10.
