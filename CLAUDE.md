# Lock Picking Robot v2

Open source lock picking robot ("Unlocked"). v1 fed wire through a hollow key to brute-force pin combinations (see `README.md`, which still describes v1). v2, on branch `v2`, moves to ESP32-S3 boards: one arm board per arm (DC motor + magnetometer for carriage position) talking to a motherboard over CAN or I²C, plus a web UI for bench calibration.

## Rules

- **Never touch `firmware/common/config.h`.** Pins and constants are still being tested on breadboards.
- **Only edit the lock-picking KiCad files** in `PCB/main-board/` (and their backups). Don't modify other KiCad projects or global libraries.
- **Ask Etienne to close KiCad before writing any board file** (`.kicad_pcb`, `.kicad_sch`, `.kicad_pro`, `.kicad_sym`, library parts). KiCad overwrites external edits when it saves.
- **Never commit or push unless asked.**
- No trivial getters/setters in firmware classes: use public members (`motor.setpoint`). Methods that do real work are fine.

## Layout

| Path | What |
|---|---|
| `firmware/arm/` | Arm board firmware: FreeRTOS tasks for magnet sensing and PID motor control, `comms.h` link to the motherboard |
| `firmware/motherboard/` | Motherboard firmware (stub so far) |
| `firmware/calibration/` | Bench calibration routines driven from the serial monitor, reusing the arm's classes |
| `firmware/common/` | Shared headers: `motor.h`, `magnet.h`, `structure.h`, `config.h` (off limits) |
| `platformio.ini` | PlatformIO config at the repo root (`src_dir = firmware`) |
| `UI/` | SvelteKit 5 (runes) + TypeScript calibration UI over Web Serial, managed with bun |
| `PCB/main-board/` | KiCad 10 project for the per-arm board (Etienne calls it the "main board"; schematic title "Arm Board"). Not the motherboard |
| `PCB/README.md` | Full board notes: components, layout decisions, checks, revision history |
| `3D files/` | v1 mechanical parts (STL, 3MF, STEP zip, F3D) |
| `unlocked/` | Leftover v1 editor config only |

The splitter board was removed (commit `fedb49c`); `PCB/README.md` still mentions it.

## Firmware

PlatformIO, ESP32-S3 (`esp32-s3-devkitm-1`, Arduino). Each env builds its folder plus `common/`.

```bash
pio run -e arm                 # build
pio run -e arm -t upload       # flash
pio device monitor             # serial
```

Envs: `arm`, `motherboard`, `calibration`.

## UI

```bash
cd UI
bun install
bun run dev      # http://localhost:5199
bun run check    # svelte-check
bun run format   # prettier
```

Imports use `#lib` / `#lib/*` (see `package.json`). Calibration pages live in `src/lib/calibration/` and `src/routes/calibration/`; serial code in `src/lib/serial/`.

## PCB workflow

- KiCad 10 is only installed as the flatpak `org.kicad.KiCad`; there is no `kicad-cli` or `pcbnew` on the host. Run tools inside it, granting the directory (build argv as a bash array, paths may contain spaces):
  ```bash
  flatpak run --filesystem="$PWD" --command=kicad-cli org.kicad.KiCad pcb drc ...
  flatpak run --filesystem="$PWD" --command=python3 org.kicad.KiCad script.py
  ```
- Order of work: schematic, then update PCB from schematic, then DRC, then regenerate production and commit `production/` together with the board.
- `scripts/generate_production.py` (run from `PCB/main-board`): DRC pre-flight with schematic parity, then gerbers/BOM/CPL/netlist to `production/JLC/`, STEP/renders/DXF to `production/CAD/`, analysis + DRC report to `production/analysis/`. Use `--all`, or `--only jlc`, `--only dxf,analysis`, `--skip-drc`, `--clean`.
- `scripts/board_analysis.py`: current-density and thermal analysis (runs in KiCad's Python, about a minute). Called by the `analysis` output.
- Zone refill: use `kicad-cli pcb drc --refill-zones --save-board`, never pcbnew Python's `ZONE_FILLER` (it ignores the custom `.kicad_dru` rules). The kicad-cli route rewrites `.kicad_pro`/`.kicad_prl`, so `git checkout` those afterwards.
- All symbols, footprints and 3D models are bundled in `libraries/` via `${KIPRJMOD}`.
- **Backups:** before editing board files, copy them into `PCB/main-board/lock-picking-backups/` as `<file>.before-<change>` (e.g. `main-board.kicad_pcb.before-routing`) or a dated `wip-YYYYMMDD/` folder. That folder is gitignored and large; don't read it unless restoring.
- Update `PCB/README.md` (revision history and relevant sections) with any board change.
