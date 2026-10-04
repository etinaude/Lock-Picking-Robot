# Arm board v2, rev 3c: full check report

**Board:** `lock-picking.kicad_pcb`, rev 3c (U3 and the Ø2.5 mm shaft hole moved 3.5 mm, then re-routed). 25.0 × 23.7 mm, 2 layers, 1.6 mm, 1 oz.
**Date:** 2026-10-04
**Verdict:** the board is clean to manufacture. DRC and ERC have no errors, and every production file matches the board. Before the board is powered, the firmware pin map has to be updated (I1). Accuracy at long travel depends on firmware compensation for heat and motor current (I3, I4).

| Status | Meaning |
|---|---|
| **Issue** | Act on it: it stops the board working, risks damage, or is a real accuracy limit |
| **Warning** | A known limitation, or something to confirm in CAD or with the fab |
| **OK** | Checked, not a problem |

## 1. Summary of every check

| # | Check | Result | Status |
|---|---|---|---|
| 1 | Firmware pin map (`firmware/common/config.h`) | Still the v1 pins, so the magnetometer and motor won't work on v2 | **Issue** |
| 2 | 12 V on a USB-C connector (J1) | Plugging the arm cable into the SuperMini's own USB-C port puts 12 V on its 5 V rail | **Issue** |
| 3 | Heat reaching the magnetometer (U3) | +35.4 K per W in U2 (was 38.5); the firmware treats U3's die temperature as the magnet's | **Issue** (accuracy) |
| 4 | Motor-current field at U3 | 6.1 µT/A (was 3.5), ≈ 30 µm at 1.5 A and 10.5 mm travel | **Issue** (accuracy) |
| 5 | U2 (motor driver) junction temperature | 111 K/W, so 1 A continuous reaches thermal shutdown in still air | **Issue** (unchanged) |
| 6 | Carriage fit: shaft | Board edge 4.32 mm from the shaft centre: clears the Ø6.9 hole by 0.87 mm, overlaps the R5 boss by 0.68 mm | Warning |
| 7 | Carriage fit: mounting screws | H1/H2 move 3.5 mm relative to the carriage | Warning |
| 8 | Carriage fit: magnet alignment | U3 is 0.11 mm off the magnet centre (unchanged) | Warning |
| 9 | Bottom side faces the carriage | U3, U4, L1, D1 and the TP10/TP11 wire pads all point at the carriage | Warning |
| 10 | STEP completeness | J1, J2, H1, H2 bodies missing (library path); U1 has no model | Warning |
| 11 | EasyEDA library configuration | 2 DRC + 7 ERC warnings, same cause as #10 | Warning |
| 12 | Silkscreen | 17 cosmetic DRC warnings; EasyEDA outlines 0.06 mm (below JLC's 0.153); text 0.8 mm | Warning |
| 13 | 12 V input protection / bulk | No TVS, ceramic-only bulk (unchanged) | Warning |
| 14 | Motherboard I²C over the USB-C cable | No ESD or series R, SDA/SCL coupled in the cable (unchanged) | Warning |
| 15 | J1 peg holes to pads | 0.18 mm (manufacturer pattern), may draw a DFM query | Warning |
| 16 | KiCad DRC | 0 errors, 0 unconnected, 0 schematic-parity issues | OK |
| 17 | KiCad ERC | 0 errors | OK |
| 18 | Copper clearance (independent re-check) | ≥ 0.200 mm everywhere (rule 0.2, JLC 0.127) | OK |
| 19 | Copper to board edge and shaft hole | ≥ 0.300 mm (rule 0.3) | OK |
| 20 | Hole-to-hole spacing | ≥ 0.275 mm (**fixed**: was 0.137 mm at J1) | OK |
| 21 | Track, via and annular-ring sizes | Tracks ≥ 0.2, vias 0.3 drill / ≥ 0.1 ring | OK |
| 22 | Acid traps, duplicate or dangling tracks | None (**fixed**: one duplicate segment) | OK |
| 23 | ESP32 socket footprint pin grid | All 18 pins on 2.54 mm (**fixed**: pin 13 was 40 µm off) | OK |
| 24 | MAG I²C over unbroken front GND | Same coverage as rev 3b; only the pin/via ends are uncovered | OK |
| 25 | MAG lines to motor copper | 1.21 mm (needs ≥ 1.2) | OK |
| 26 | Crosstalk | Only I²C lines next to each other or to +3V3 | OK |
| 27 | Power-path resistance and current density | +12 V 9.1 mΩ, GND 2.1 mΩ (unchanged) | OK |
| 28 | ESP 5 V supply-loop field at U3 | 1.7 µT/A (was 2.7) | OK |
| 29 | Decoupling placement around U3 | C10 1.0 mm from pin 8, C9 2.4 mm from pin 2 | OK |
| 30 | Hand-soldering clearance to ESP32/J2 pins | 0.91 mm (needs ≥ 0.9) | OK |
| 31 | Production files vs board | Gerbers, drills, CPL, BOM, netlist and DXF all match | OK |
| 32 | BOM sourcing data | All 22 placed parts have an LCSC number; single-sided (bottom) assembly | OK |

## 2. Fixed during this check

| Fix | Why |
|---|---|
| Added an `analysis` output to `production/generate_production.py`. The new `production/board_analysis.py` writes the current and heat maps plus `…_analysis.txt` | You asked for it. It runs in KiCad's own Python (pcbnew + numpy), so the script still needs nothing beyond the standard library and KiCad |
| ESP32 SuperMini pin 13 moved from y = −2.50 to −2.54 mm: on the board, in the project `footprints/` copy, and in your global `footprints` library (`~/.var/app/org.kicad.KiCad/data/kicad/10.0/footprints/`; a one-line change, the old value was −2.5) | A typical 0.64 mm square socket pin in a 1.0 mm hole has about 0.05 mm of radial play, and the 40 µm offset used most of it. All three copies are fixed so "Update footprint from library" can't undo it |
| GND stitching vias at J1's shell slots moved 0.15 mm, from (36.4, 19.0)/(36.4, 28.7) to (36.4, 18.85)/(36.4, 28.85) | Only 0.137 mm of board was left between the via drill and the slot. KiCad's DRC doesn't flag this, but JLC's minimum is 0.254 mm. It is now 0.275 mm |
| Removed one duplicated GND segment at C11 | A leftover from the re-route; harmless, but untidy |
| U2's 3D model now points at `QFN-16-1EP_3x3mm_P0.5mm_EP1.7x1.7mm.step` | KiCad 10 doesn't ship the old file name. The STEP now includes U2 |
| Zones refilled with the custom rules; board ROUTING NOTES and Design_Review_v2 Appendix C updated | So the copper, the notes and the numbers agree |

## 3. Issues

### I1. Firmware pin map is still v1
`config.h` still has the v1 pins:

```
MAG_SDA/SCL 41/40   COMMS_SDA/SCL 21/22   PIN_CS 4   IN1_EN 5   IN2_PH 6   MODE_PIN 7   CURRENT_SENSE_V_PER_A 2.5
```

On the v2 board this goes wrong in four ways:

- the magnetometer is on pins the SuperMini doesn't break out, so `magnet.begin()` never returns;
- PWM goes to nSLEEP;
- the "mode" pin is EN, so driving it low holds the bridge in brake;
- current sensing reads I2C_SCL.

The v2 map:

| Signal | v2 pin |
|---|---|
| MAG_SDA / MAG_SCL / MAG_INT | 9 / 10 / 11 |
| IN1/EN (PWM) | 7 |
| IN2/PH | 6 |
| nSLEEP | 5 (drive high) |
| nFAULT | 2 (input) |
| IPROPI | 1 |
| Current sense | 1.5 V/A |
| COMMS SDA/SCL | 3 / 4 |

**Action:** update the firmware before the first power-up. This is unchanged from Design_Review_v2 F1.

### I2. 12 V on a USB-C receptacle
J1 always carries 12 V on VBUS, right next to the SuperMini's real USB-C port. One wrong plug destroys the module, or anything else plugged into the motherboard port.

**Action:** short term, add a "12 V – NOT USB" label and use a coloured cable. Next spin, use a non-USB connector or CC-ID gating. Unchanged from Design_Review_v2 U1.

### I3. Heat reaching the magnetometer
U3 rises 35.4 K per watt in U2, an improvement from 38.5 K because U3 is now 2.8 mm further from U2. It also gains 6.4 K from the buck regulator and D1 (was 5.4 K), because U3 now sits next to them.

The firmware divides the field by a tempco based on U3's die temperature, but the magnet doesn't see the board's heat. That error is about 58 µm per +10 K at 10.5 mm travel.

**Action (firmware):**
- stop applying the magnet tempco from the die temperature;
- enable `TCMP_EN`;
- re-zero against an end stop periodically.

### I4. Motor-current field at U3

![motor current](img/rev3c_current_motor.svg)

For comparison, the same map for the committed rev 3b board is [`img/rev3b_current_motor.svg`](img/rev3b_current_motor.svg).

| B_z at U3 per amp of motor current (signs as in Design_Review_v2 §3.1) | rev 3b | rev 3c |
|---|---|---|
| +12 V copper, J1 → U2 | −6.4 | −4.9 |
| GND return, U2 → J1 | +8.5 | +10.2 |
| Motor leads U2 → J2 / cable | +2.7 / −1.3 | +1.5 / −0.7 |
| **Net** | **+3.5 µT/A** | **+6.1 µT/A** |

U3 now sits next to the front-GND path that carries the motor return around the 12 V copper and the relocated hole. The right-hand panel above shows which copper produces the field. U3 is also further from the 12 V feed, which used to cancel part of that field.

At 10.5 mm travel the magnet's field changes by only 0.305 mT per mm, so:

| Motor current | Field at U3 | Position error at 10.5 mm (rev 3c) | Position error, rev 3b |
|---|---|---|---|
| 1.0 A | 6.1 µT | ≈ 20 µm | ≈ 11 µm |
| 1.5 A | 9.2 µT | ≈ 30 µm | ≈ 17 µm |

The firmware deadband is 10 µm. Near the magnet the error is negligible: about 1 µm at 2.5 mm travel and 7 µm at 6 mm.

A routing-only fix isn't available. The hole leaves about 0.3 mm of copper beside the ESP32 pins, and the only other return path is the narrow bottom-layer strip along the top edge.

**Action:** the error is proportional to motor current, so compensate in firmware: B_corr = B_z − k·I_motor, with k calibrated on the bench with the magnet removed. Alternatively, brake the motor for the ~25 ms conversion near the target. On the next spin, run a GND strip directly under the 12 V feed (Design_Review_v2 M1).

### I5. U2 junction temperature
U2's junction runs at 111 K/W in still air, unchanged by this layout. At 1 A continuous U2 dissipates about 1.1 W, which puts the junction at ≈ 145–160 °C, right at the 160 °C thermal-shutdown minimum.

**Action:** limit continuous current or duty and read nFAULT (GPIO2). On the next spin, consider the DRV8874PWPR, which has 3.5× lower loss. Unchanged from Design_Review_v2 T2.

![thermal](img/rev3c_thermal.svg)

## 4. Warnings

| # | Warning | What to do |
|---|---|---|
| W1 | **Shaft clearance.** The carriage is rotated 15.9° to the board. The board's bottom edge is 4.32 mm from the shaft centre, which clears the Ø6.9 mm hole by 0.87 mm. It overlaps the R5 mm boss around the hole by 0.68 mm in plan view. | Confirm in Onshape that the boss doesn't reach the PCB plane and that the shaft's real diameter fits. |
| W2 | **Mounting screws.** Relative to the carriage, the whole board (and H1/H2) moved 3.5 mm. | Move the carriage's screw holes to match the new DXF. |
| W3 | **Magnet alignment.** U3 is 9.114 mm from the shaft hole; the carriage's magnet is 9.000 mm from its hole. U3 can only sit within 0.11 mm of the magnet centre. | Harmless if you calibrate in place. Nudge U3 if you want it exact. |
| W4 | **Bottom side faces the carriage.** All the assembled parts are on the bottom, as are the TP10/TP11 wire pads (TP10 is 1.5 mm from the shaft hole edge). | Check the carriage has pockets for the parts and room for any wires soldered to TP10/TP11. |
| W5 | **STEP is incomplete.** The footprints look for EasyEDA models in `${KICAD_3RD_PARTY}` = `~/KiCad/3rdparty`, but the files are in `~/KiCad/`. Only J2 has a STEP model there; J1, H1 and H2 have none. U1 (SuperMini) has no model at all. | Move or symlink `~/KiCad/EasyEDA.*` into `~/KiCad/3rdparty/` and add STEP files for J1, H1, H2 and the SuperMini. Until then the STEP can't prove clearances. |
| W6 | **EasyEDA library not enabled.** This gives 2 DRC and 7 ERC warnings. | Same fix as W5, then register the library in KiCad's library tables. |
| W7 | **Silkscreen.** There are 17 DRC warnings: J1/J2 overhang the board edge by design, so their silk is clipped; some silk sits over pads; U1's outline overlaps J2/H2. EasyEDA outlines are 0.06 mm (JLC's minimum is 0.153 mm, so they may not print). Board text is 0.8 mm (JLC recommends 1.0 mm). | Cosmetic only. |
| W8 | **No bulk or TVS on 12 V.** The 12 V rail has ceramic bulk only and no TVS. | 47–100 µF polymer + SMAJ15A on the next spin (Design_Review_v2 P1). |
| W9 | **Motherboard I²C over the USB-C cable.** It has no ESD protection or series R, and SDA/SCL are coupled in the cable. | Run at ≤ 100 kHz; add a TVS and 33–100 Ω series resistors next spin (X1). |
| W10 | **J1 peg holes.** The holes are 0.18 mm from the pad toes (manufacturer pattern, allowed by a custom rule). | JLC may raise a DFM query; accept it. |

## 5. Not a problem

| Check | Evidence |
|---|---|
| KiCad DRC (custom rules, all track errors) | 0 errors, 0 unconnected, 0 parity. All 17 warnings are W6/W7 |
| KiCad ERC | 0 errors. The 46 "Unspecified pin type" warnings come from the EasyEDA symbols |
| Copper clearance | Re-measured independently of KiCad: F.Cu ≥ 0.220 mm, B.Cu ≥ 0.200 mm. The 0.200 mm is U3's own pin 2 to its exposed pad, inside the footprint |
| Copper to edge / shaft hole | 0.300 mm on both layers; 0.376 mm on B.Cu at the hole |
| Holes | Vias 0.3 mm drill with ≥ 0.1 mm ring; closest holes 0.275 mm apart; U2's four thermal vias (in its footprint) have a 0.1 mm ring, as JLC allows for vias |
| Track geometry | Widths 0.2–0.8 mm; no acute junctions, no duplicates, no dangling ends |
| ESP32 pin grid | All 18 holes on 2.54 mm |
| MAG I²C routing | On B.Cu over the front GND pour. Uncovered length is unchanged from rev 3b (1.1 / 1.1 / 0.6 mm at the pin and via ends only) |
| Separation from motor copper | MAG lines are 1.21 mm from J2 and the motor traces. From U3's centre: MOT_OUT 10.2 mm, J2 pins 10.9 mm, U2 13.6 mm, +12 V copper 9.1 mm (all further than before) |
| Crosstalk | Same layer, parallel, ≤ 1 mm gap: MAG_SCL–+3V3 5.7 mm at 0.3 mm, MAG_SCL–MAG_INT 4.1 mm at 0.8 mm, MAG_SCL–MAG_SDA 3.7 mm at 0.6 mm, I2C_SCL–SDA 0.5 mm. All low-speed lines on the same bus; no motor, SW or charge-pump coupling |
| Power path | +12 V 9.1 mΩ, GND return 2.1 mΩ (same as rev 3b). Peak density 4.8 A/mm per A is on the short U2 VM pin stub (Design_Review_v2 L16) |
| ESP 5 V loop | 1.7 µT/A at U3 (rev 3b: 2.7). The buck is now 4.1–4.6 mm from U3, but its loop fields largely cancel. This model is a near-cancellation of large terms, so treat it as "not worse" rather than as an exact figure |
| Decoupling | C10 (100 nF) is 1.0 mm from U3 pin 8; C9 (100 nF) is 2.4 mm from pin 2 and 3.2 mm from pin 15; C11 (10 µF bulk) is on the +3V3 feed |
| Hand soldering | Closest back-side PCBA pad to a U1/J2 pin on another net: 0.91 mm (needs ≥ 0.9) |
| Production files | Edge.Cuts has the Ø2.5 hole at (25.864, 25.6). The drill file has 66 PTH + 2 NPTH holes, including the moved vias and pin 13 at 26.39. CPL positions and rotations match the board for every moved part (C9, C10, C11, R3, R4, U3). The DXF and netlist are regenerated |
| BOM | 22 placed parts on 17 lines, each with an LCSC number. All parts are on the bottom (single-sided assembly) |

![5 V supply loop](img/rev3c_current_5V.svg)

## 6. How it was checked, and how to re-run it

- **KiCad 10.0.6:** `pcb drc --schematic-parity --all-track-errors --severity-all` with the project's custom rules, and `sch erc`. Zones were refilled with kicad-cli (`--refill-zones`), which applies the custom 0.3 mm edge rule; the pcbnew Python refill doesn't.
- **Independent geometry check:** run on the copper exported from pcbnew. It measures net-to-net clearance, distance to the edge and the hole, exact hole-to-hole spacing (slot shapes included), annular rings, junction angles, duplicates, and GND coverage under the MAG lines.
- **Physics (`production/board_analysis.py`):**
  - the copper of both layers is rasterised at 0.08 mm;
  - DC solves with the source pads at 1 V and the sinks at 0 V, scaled to 1 A;
  - a Biot–Savart sum at U3's Hall plate, 0.5 mm below the bottom copper;
  - a steady-state thermal network: copper + FR4 + via barrels, h = 18/24 W/m²K top/bottom, still air, no frame contact (an upper bound).

  The constants are the ones in Design_Review_v2 Appendix A. Run on rev 3b, the model reproduces that review's numbers (U2 112 vs 117 K/W, U3 38.5 vs 39 K/W, motor loop 3.5 vs 3.4 µT/A).
- **Cross-checks:** Gerbers, Excellon drills, CPL and BOM were compared with the board. The carriage fit was taken from `carriage - Base.dxf`, anchored on the shaft hole and the direction to U3.
- **Re-run:** `python3 production/generate_production.py --only analysis` (about 1 minute). `--all` also runs it.
