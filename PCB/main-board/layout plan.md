# Main board rev 6: layout plan

Placement plan for the rev 6 arm board ("main board"), written before any layout work. Routing is out of scope except where it decides where parts can go. Coordinates are KiCad board millimetres (y grows downwards). "Distance" means plan distance from U3's centre (23.2, 37.75), which sits over the carriage magnet.

Status (2026-10-07):

- The schematic is rev 6 plus the GPIO remap in [section 2](#2-gpio-assignment). The PCB has **not** been updated from it yet.
- The board has no tracks or vias. The GND zones on both layers, the front-pour notch between J2's pins and the heat-sink pad over U2 are still there.
- Decided with Etienne: the remap below; the [distance targets](#4-magnetometer-distance-targets) are fine; D1's position stays open until the schematic is settled; Claude does the placement once the schematic is fixed.

## 1. What's fixed on the board

These are where they need to be. Lock them before placing anything.

| Part | Position | Side | Notes |
|---|---|---|---|
| U1 SuperMini | (27.2, 33.5), **0°** | top | Rotated from 180° in rev 5. USB-C end now at the top edge, overhanging it by 1.3 mm, above J2 |
| U2 DRV8876 | (30.05, 25.95), 90° | bottom | VM/VCP/CPH/CPL pins face the top edge; nSLEEP, nFAULT, VREF, IPROPI face down (y 27.41); EN, PH face right (x 31.51) |
| U3 MLX90393 | (23.2, 37.75), 0° | bottom | INT, CS, SCL on the top edge (x 22.45 / 22.95 / 23.45, y 36.25); SDA on the right edge (24.7, 37.0); VDD_IO pin 8 at (24.7, 38.5) |
| J2 motor | (25.35, 25.9) | top | Pins at x 24.35 / 26.35, y 25.9 |
| CN2 XT30 | (39.625, 33.715), 90° | top | +12 V (39.6, 29.3), GND (39.6, 34.3), BUS_P (38.6, 38.1), BUS_N (40.6, 38.1) |
| H1 / H2 nuts | (39.6, 22.65) / (39.6, 44.65) | top | 4.2 mm GND ring on the bottom |
| TP row | y 43.75: TP1 22.25, TP2 24.69, TP7 27.13, TP5 29.56, TP6 32.0 | top | TP3/TP4 (TX/RX) removed. A new TP3 (IO7) needs a slot, see step 8 |
| Heat-sink pad | F.Cu (28.6–32.4, 23.45–27.85) | top | GND, over U2 |

U1 pin positions with U1 at 0°:

| Left header, x = 19.58 | y | Right header, x = 34.82 | y |
|---|---|---|---|
| TX (not used) | 23.34 | 5V | 23.34 |
| RX (not used) | 25.88 | GND | 25.88 |
| IO1 | 28.42 | 3V3 | 28.42 |
| IO2 | 30.96 | IO13 | 30.96 |
| IO3 | 33.50 | IO12 | 33.50 |
| IO4 | 36.04 | IO11 | 36.04 |
| IO5 | 38.58 | IO10 | 38.58 |
| IO6 | 41.12 | IO9 | 41.12 |
| IO7 | 43.66 | IO8 | 43.66 |

## 2. GPIO assignment

The 180° turn of U1 put the old motor-driver pins on the far side from U2 and the old magnetometer pins on the far side from U3. The remap puts each group on the header next to its chip.

| GPIO | Header | Net (new) | Net (before) | Goes to |
|---|---|---|---|---|
| IO1 | left 28.42 | DRV_IPROPI | DRV_IPROPI | U2 pin 4 (needs ADC1, kept) |
| IO2 | left 30.96 | **MAG_SDA** | DRV_nFAULT | U3 pin 5 |
| IO3 | left 33.50 | **MAG_SCL** | SDA_CANTX | U3 pin 3 |
| IO4 | left 36.04 | **MAG_INT** | SCL_CANRX | U3 pin 1 |
| IO5 | left 38.58 | **EXP_IO5** (TP1) | DRV_nSLEEP | spare pad |
| IO6 | left 41.12 | **EXP_IO6** (TP2) | DRV_IN2_PH | spare pad |
| IO7 | left 43.66 | **EXP_IO7** (TP3, new) | DRV_IN1_EN | spare pad |
| IO8 | right 43.66 | **SCL_CANRX** | free | JP3 / JP2 (CAN RX or I²C SCL) |
| IO9 | right 41.12 | **SDA_CANTX** | MAG_SDA | U5 TXD, R7, JP1 (CAN TX or I²C SDA) |
| IO10 | right 38.58 | **DRV_nFAULT** | MAG_SCL | U2 pin 2 |
| IO11 | right 36.04 | **DRV_nSLEEP** | MAG_INT | U2 pin 1 |
| IO12 | right 33.50 | **DRV_IN2_PH** | EXP_IO12 | U2 pin 16 |
| IO13 | right 30.96 | **DRV_IN1_EN** | EXP_IO13 | U2 pin 15 |

Why these pins:

- **Magnetometer on IO2/3/4.** All three U3 pins are reached from above, as rev 5 did. Lines from the left header nest without crossing if the highest pin goes furthest right: SDA (IO2) runs to x ≈ 25.1 and drops onto the right-edge pad; SCL (IO3) drops at x 23.45; INT (IO4, the lowest) drops at x 22.45. IO5 and below can't reach the top-edge pads without crossing IO4's line, and SDA from below would have to pass VDD_IO (pin 8) and C10.
- **IPROPI stays on IO1.** It needs ADC1 (GPIO1–10) and runs straight left from U2's bottom edge at y ≈ 28.3, above the magnetometer lines and away from the buck.
- **The other driver lines go on the right header, in U2's pin order.** Going around U2 from its right edge to its bottom edge, the order is EN (y 26.2), PH (26.7), nSLEEP (x 30.8), nFAULT (x 30.3). That matches IO13, IO12, IO11, IO10 down the header, so the four lines don't cross each other.
- **CAN on IO9/IO8.** IO3/IO4 went to the magnetometer. IO9/IO8 sit right next to CN2's bus pins (y 38.1), so U5, JP1–JP4 and the bus pair all live in the lower right, away from U3. In I²C mode, JP1/JP2's I²C pads link to IO9/IO8 a few mm away.
- **Spares IO5/6/7** are the bottom-left pins, next to the TP row.
- **TX/RX not used** (ROM boot log on TX).
- ESP32-S3 notes:
  - GPIO3 is a strapping pin only if the JTAG-select eFuse is burnt; R4 pulls it high anyway.
  - nSLEEP floats while the ESP boots, but the DRV8876's internal pull-down keeps the bridge asleep.
  - TWAI and I²C can use any pin through the GPIO matrix.

Not changed here: `firmware/common/config.h` still has the v1 pins and must be updated to this table before the first power-up.

## 3. Which passives belong together

| Chip | Must be tight | Can be a few mm away |
|---|---|---|
| U2 DRV8876 | C5 (100 nF) on VM pin 9 to PGND; C7 VCP (pin 10) to VM; C8 across CPH/CPL (pins 11/12); C6 (22 µF) on VM | R1 (0805, hand-swappable) and C12 on IPROPI; R2 nFAULT pull-up |
| U4 TPS54202 | C1 across VIN (3) and GND (1), the tightest loop; L1 on SW (2); C2 across BOOT (6) and SW | R8/R9/C14 at FB (4), away from SW and L1, sensing at C3/C4; C3/C4 at L1's +5 V end |
| D1 | Between +5V and U1's 5V pin | — |
| U3 MLX90393 | C10 ≤ 1 mm from pin 8; C9 at pins 15/13 | C11 (bulk); R3/R4 pull-ups on MAG_SDA/MAG_SCL |
| U5 SN65HVD230 | C13 under VCC (pin 3), one via | R6 on Rs (static); R7 anywhere on SDA_CANTX; R5 under JP4 |

## 4. Magnetometer distance targets

The whole buck can't sit 12 mm from U3; there isn't room. Agreed targets:

| Part | Rev 5 | Rev 6 target |
|---|---|---|
| L1 | 4.6 mm | ≈ 10–11 mm |
| SW node | 4.1 mm | ≈ 10 mm |
| U4 | 8.7 mm | ≈ 8.5–9.5 mm |
| C3/C4, R8/R9/C14 (no DC current) | — | ≈ 6.5–9 mm |
| D1 | 4.4 mm | open (see step 6) |
| CAN bus-current parts (U5 CAN side, JP1, JP2, JP4, R5) | U5 3.4 mm | ≥ 8 mm |

Keep every DC current path in the top-right corner: 12 V into U4, L1 → D1 → the 5V pin, and the GND return. C3/C4 and the divider carry no DC, so they can be the closest buck parts.

## 5. Floorplan

```
BOTTOM (seen from top)  x: 19.6 ....... 25 ........ 30 ....... 33.5 |hdr| 36.5 ..... 42.8
y 22   [C6]  J2 pins  [C5][C7][C8]                           5V  |  (H1 ring)
y 24                   ┌── U2 ──┐                            GND |  D1? (open)
y 28   IO1 ── IPROPI ──┘ fan-out └── via field: nSLEEP/nFAULT/EN/PH → top   3V3
y 29   R1 C12          (VREF via)    [  via field  ]         13  |  (+12V pin)
y 31   IO2 ─SDA──────┐               [L1]  BUCK: U4 C1 C2    12  |
y 33   IO3 ─SCL────┐ │               R8 R9 C14 C3 C4         11  |  (GND pin)
y 35   IO4 ─INT──┐ │ │                                       10  |
y 37        [U3] C10 R3/R4                                    9  |  (bus pins)
y 40        C9 C11      CAN passives: C13 R6 R7 R5            8  |
y 43.7 IO5-7 → spare TPs (bottom routes to vias)                  |  (H2 ring)

TOP (under the module)
y 22.6 ═ 12 V from CN2: gap between 5V/GND pins (y 24.6) → along top edge → VM vias ═
y 28.4 ═ 3V3 from the 3V3 pin, leftwards below the heat-sink pad ═
y 29–39  right inside column x ≈ 32.7–33.4: EN/PH/nSLEEP/nFAULT down to IO13–IO10
y 29–37  upper left: front GND kept whole over the magnetometer lines
y 34–42  CAN block: U5, JP1, JP2, JP3, JP4; bus pair out through the gap
         between IO10/IO9 (y ≈ 39.85), under the XT30 body to the bus pins
y 43.75  TP row (fixed)
```

### The top-right corner

This is where the crossings are. With U1 at 0°, the right header runs 5V, GND, 3V3, EN, PH, nSLEEP, nFAULT from the top. But around U2 and L1 the order is EN, PH, nSLEEP, nFAULT, 3V3 (VREF), then 5V (L1, below U2). 5V and 3V3 have to cross the four driver lines once, so each net gets a layer:

- **EN, PH:** leave U2's right edge on the bottom and go down the strip between U2 and the header (x ≈ 32.3–33.4) to vias at y ≈ 29–30. They continue on the top down the inside of the header to IO13/IO12.
- **nSLEEP, nFAULT:** leave U2's bottom edge downwards on the bottom, go right at y ≈ 29.2 and join the same via field. They continue on the top to IO11/IO10.
- **3V3:** leaves the 3V3 pin on the bottom at y ≈ 28.4 and goes up through a via at about (31.5, 28.4). It runs on the top below the heat-sink pad to a VREF via and on towards U3, U5 and the pull-ups.
- **5V:** leaves L1's +5V end on the bottom and goes right through the gap between the 3V3 and IO13 pins (y ≈ 29.7). It passes under the driver lines, which are on the top there, towards D1.
- **12 V to U2:** on the top. It goes from CN2's +12 V pin, between H1's top pad and the +12 V pad, into the gap between the 5V and GND pins (y ≈ 24.6). Then along the top edge (y ≈ 22.85, clear of the heat-sink pad) to the VM vias by C5 (as rev 5).
- **12 V to U4:** on the bottom, through the gap between the IO12 and IO11 pins (y ≈ 34.8), under the driver lines.

Placement consequence: keep a via field at about x 31.5–33.4, y 28.6–30.3 free, so L1 sits just below it, around (31.6, 31.9), about 10.2 mm from U3.

## 6. Step by step

**Step 0. Prep**

- Etienne closes KiCad.
- Back up to `lock-picking-backups/main-board.kicad_pcb.before-rev6-placement`.
- Lock U1, U2, U3, J2, CN2, H1, H2, TP1, TP2, TP5, TP6, TP7, the heat-sink pad, the outline and both zones.

**Step 1. Update PCB from Schematic (F8)**

- Tick "update footprints" (R1 → 0805 hand-solder pads).
- Expect these changes:
  - U4 → TPS54202DDCR and L1 → 10 µH.
  - R8, R9, C14 and TP3 arrive off-board.
  - U1's pad nets change to the [new map](#2-gpio-assignment).
  - TP1/TP2 move to EXP_IO5/EXP_IO6.
  - The renamed nets come across.
- Run DRC with schematic parity: it should show 0 parity issues and only unconnected items.
- Confirm the locked parts haven't moved.

**Step 2. Conventions**

- Grid: 0.05 mm for 0402s, 0.25 mm otherwise.
- One rotation per row, with GND pads on the same side so they share vias.
- Courtyards touching or ≤ 0.1 mm apart within a group.
- Keep ≥ 0.9 mm from the U1, J2 and CN2 pins (hand soldering).
- References stay on F.Fab/B.Fab at 1 mm, as in rev 5.

**Step 3. U2 group (bottom)**

- Reuse rev 5 exactly (U2 and J2 haven't moved):

  | Part | Position | Rotation |
  |---|---|---|
  | C6 | (24.00, 23.15) | 180° |
  | C5 | (27.40, 23.10) | 180° |
  | C7 | (29.35, 23.10) | 180° |
  | C8 | (31.30, 23.10) | 0° |

- R1, horizontal, at about (25.0, 29.3), hanging below the IPROPI line (y ≈ 28.3). It's hand-swappable, so keep 0.5 mm clear around its pads. C12 beside it on the same node.
- R2 (nFAULT pull-up) on the nFAULT line, near the via field or at the IO10 end; its other side is 3V3.
- Keep the strip under U2 (y 27.8–29.0) for the fan-out and the VREF via.

**Step 4. U3 group (bottom)**

- C10 at (25.70, 38.25), 90° (rev 5).
- C11 directly below C10, at about (25.70, 40.1), so they form one column at x = 25.70.
- C9 moves from its rev 5 spot (21.55, 34.8), which is now where the IO3/IO4 lines run. Put it below U3's left corner, at about (22.4, 40.4), fed from pins 15/13.
- R3/R4 in a second column at x ≈ 26.8 beside C10/C11, sharing the 3V3 node. Alternatively at the IO2/IO3 end.
- Leave the nest (x 20.3–25.4, y 28.6–36) free for the three magnetometer lines and the CS/3V3 via. Spread INT to x ≈ 22.0 and SCL to x ≈ 23.9 above y ≈ 35, so a via for CS (3V3) fits at about (22.95, 34.9).

**Step 5. Buck (bottom, below U2 and left of the right header)**

1. **L1** at about (31.6, 31.9), below the via field. Its pads must stay ≥ 0.9 mm from the right-header pins (x ≤ 33.15).
2. **U4** next to L1, SW pin facing L1.
   - Gotcha: on the bottom the footprint is mirrored. With SW facing up, VIN ends up on the header side.
   - Try both orientations and keep the one with the smallest C1 loop.
   - 12 V arrives through the gap at y ≈ 34.8.
3. **C1** across VIN and GND.
4. **C2** across BOOT and SW.
5. **R8/R9/C14** on the far side from L1.
6. **C3/C4** at L1's +5V end, towards the lower left of the zone (≈ 6.5–9 mm from U3).
7. Keep the zone above the magnetometer lines and clear of the CS/3V3 via.

**Step 6. D1 (open)**

- The candidate is bottom, right of the header, at about (37.9, 26.1), cathode towards U1's 5V pin. It clears the header pin by 0.98 mm, H1's ring by 1.1 mm and the +12 V pad by 1.8 mm. +5V would come in through the gap at y ≈ 29.7 and up the outside at x ≈ 36.3.
- The alternative is inside, next to C3/C4, with VBUS crossing to the 5V pin on another layer.
- Decide once the schematic is final.

**Step 7. CAN block (top, lower right) and its bottom passives**

- **U5** in the lower right under the module, roughly x 25–33, y 34–42.
  - TXD (pin 1) and RXD (pin 4, through JP3) reach IO9/IO8 on the right header.
  - The CANH/CANL side points at JP1/JP2.
  - Turn it 90° if that shortens both sides.
- **JP1/JP2** next to U5: CAN pads facing U5, bus pads leading to the gap between IO10/IO9 (y ≈ 39.85), I²C pads linking to IO9/IO8.
- **JP3** between U5's RXD and IO8.
- **JP4** by CANH, with R5 under it on the bottom.
- Keep the CANH/CANL, JP1/JP2/JP4 loop at the right end, ≥ 8 mm from U3.
- **C13, R6, R7** on the bottom under U5.
- Move the "RX CAN", "CAN", "I2C" and "TERM" silk labels with their jumpers.

**Step 8. Test pads**

- TP3 (EXP_IO7) is new: give it a slot in the TP row. Re-space the row to six pads, or use a free spot at y ≈ 41.
- Update the TP silk labels: "IO12" → "IO5", "IO13" → "IO6", add "IO7".
- The spare pins IO5–IO7 are the bottom-left pins, so their routes stay short. Lines go on the bottom with a via at each top-side pad.

**Step 9. Check the routing channels are open (no routing yet)**

- On the bottom:
  - U2's fan-out and the via field;
  - the magnetometer nest;
  - the 12 V gap (y 34.8) and the 5 V gap (y 29.7);
  - the bus-pair gap (y 39.85).
- On the top:
  - the 12 V path along the top edge;
  - the 3V3 run at y ≈ 28.4;
  - the driver column at x ≈ 32.7–33.4;
  - unbroken GND over the magnetometer nest.

**Step 10. Tidy and check**

- Align rows and keep pin-1 marks consistent (JLC: check U2, U3, U4, D1).
- DRC: courtyards, parity, edge clearance.
- Hand-solder check: ≥ 0.9 mm to through-hole pins.
- Distance-from-U3 table against [section 4](#4-magnetometer-distance-targets).
- 3D view:
  - the USB-C plug against J2 and the motor cable;
  - the heat-sink pad height;
  - U5 under the module.
- **Before trusting its 5 V-loop result, fix `scripts/board_analysis.py`.** Its `SUPPLY` lists still use the AP63205 pins for U4 (`'4'` for GND, `'5'` for SW); the TPS54202 uses `'1'` and `'2'`.
- Update `PCB/README.md` (layout section, revision history) once placement and routing are done.
