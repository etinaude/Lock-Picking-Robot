# PCBs

KiCad 10 project for the **arm board** in [`main-board/`](main-board/). Each arm of the robot has one: an ESP32-S3 SuperMini drives the arm's brushed DC motor and reads the carriage position from a magnet, and talks to the motherboard over CAN. Etienne calls it the "main board"; the schematic title is "Lock Picking Robot - Arm Board" and the firmware env is `arm`. It is not the motherboard.

## Status

- **Schematic:** v2 rev 7: CAN only (the I²C option and JP1–JP3 are gone) and CAN TX/RX moved to GPIO8/GPIO9. Rev 6 re-assigned the GPIOs for U1 at 0° and added TP3. ERC 0 errors (38 pin-type warnings from the EasyEDA symbols). C14 changed to 100 pF on 2026-10-09 (schematic, PCB and JLC BOM all updated; the BOM lists C1546).
- **PCB: rev 7 routed** (2026-10-07). Zones filled; DRC 0 violations, 0 unconnected, parity clean. U1, U2, U3, J2, CN2, H1/H2, the TPs, the outline and both zones are locked. The heat-sink pad over U2 is still missing and must come back smaller than before (see [Layout to-do](#layout-to-do)).
- **Production files** in [`main-board/production/`](main-board/production/) are from rev 5 and don't match the board. Regenerate before ordering.
- **Firmware isn't ready for this board** ([Known issues](#known-issues)).

## At a glance

| | |
|---|---|
| MCU | ESP32-S3 SuperMini (U1) on 2 × 9-pin 2.54 mm female headers, top side |
| Motor driver | TI DRV8876 (U2), PH/EN mode, 1.0 A current limit |
| Position sensor | Melexis MLX90393 (U3) 3-axis magnetometer, centred over the carriage magnet |
| Bus | TI SN65HVD230 CAN transceiver (U5), 120 Ω termination on solder jumper JP4 |
| Power | 12 V in on CN2 → TPS54202 buck (U4) → 5 V → SuperMini (its LDO makes 3.3 V) |
| Board | 24.7 × 23.75 mm with 1 mm corners, plus R 3.2 mm bulges round the two M2 nuts (24.7 × 28.4 mm overall). 2 layers, 1.6 mm FR4, 1 oz |
| Assembly | Bottom side by JLCPCB (Economic PCBA); top side hand-soldered |

## Connectors and pinout

### CN2: 12 V + bus (AMASS XT30APB(2+2)-M, C53065086)

Vertical male, plug enters from the top. Polarised: make sure the cable and the motherboard use the same pin order.

| Pin | Net |
|---|---|
| 4 | +12V |
| 3 | GND (solid to both pours, carries the motor return) |
| 2 | CANH |
| 1 | CANL |

### J2: motor (JST S2B-PH-K-S, side entry, C173752)

Pin 1 = OUT1, pin 2 = OUT2. The body overhangs the board edge by design.

### U1: SuperMini pin map

| Pin | Net | Use |
|---|---|---|
| GPIO1 | DRV_IPROPI | Motor current, ADC1_CH0, 3.3 V/A |
| GPIO2 / 3 / 4 | MAG_SDA / MAG_SCL / MAG_INT | Magnetometer, I²C address 0x0C |
| GPIO5 / 6 / 7 | EXP_IO5 / EXP_IO6 / EXP_IO7 | Spare, on TP1 / TP2 / TP3 |
| GPIO8 | CAN_TX | CAN TX to U5 (R7 pulls it high) |
| GPIO9 | CAN_RX | CAN RX from U5 |
| GPIO10 | DRV_nFAULT | Driver fault, active low (10 k pull-up) |
| GPIO11 | DRV_nSLEEP | Drive **high** to run |
| GPIO12 | DRV_IN2_PH | Direction |
| GPIO13 | DRV_IN1_EN | PWM |
| TX / RX | | Not used. TX prints the ROM boot log at reset |
| 5V / 3V3 | VBUS / +3V3 | VBUS ≈ 4.7 V after D1 |

The pins follow the layout: U1 sits at 0°, so the left header (GPIO1–7) is next to U3 and the right header (GPIO8–13) is next to U2 and CN2. The reasoning is in [`main-board/layout plan.md`](main-board/layout%20plan.md).

GPIOs are 3.3 V only. Test pads TP1, TP2, TP3, TP5 and TP7 (top, 1.5 mm, labelled) carry IO5, IO6, IO7, 3V3 and GND. TP6 (VBUS, labelled 5V) is on the bottom next to the 5V header pin.

## CAN: U5, JP4

- **U5** is hand-soldered on the top under the SuperMini, so fit and test it **before** soldering the headers.
- **Termination:** JP4 ("TERM") is on the bottom under the XT30, between the bus pins, with R5 (120 Ω) beside it. It is open as made and can be bridged after assembly. The whole bus needs exactly two 120 Ω terminations, at its two ends: bridge JP4 on the two end arms only. On every arm it would be about 17 Ω and the bus won't work. Keep the bit rate modest (250 kbit/s).
- **U5 details:** R7 pulls TXD high so a resetting or flashing arm can't hold the bus dominant (GPIO8 floats during boot). R6 (10 kΩ on Rs) selects slope control. C13 decouples VCC.

## Power

- **U4, TPS54202DDCR** (C191884): 4.5–28 V in, 500 kHz synchronous buck.
  - R8/R9 = 75 kΩ / 10 kΩ set 5.07 V. C14 = 100 pF (C1546) feed-forward across R8: TI's Eq. 16 puts its zero at the loop crossover, 1/(2π × 75 kΩ × 100 pF) ≈ 21 kHz, the same corner as the datasheet's 100 kΩ / 75 pF. Rev 7 had 47 pF (45 kHz, above crossover, little phase boost).
  - L1 = 10 µH FTC252012S100MBCA (C5832376). Stock was low (389 on 2026-10-07); fallback SWPA3015S100MT (C45403, needs a 3 × 3 mm footprint).
  - C1 10 µF 25 V in, C2 100 nF BOOT–SW, C3/C4 2 × 47 µF 6.3 V out. Add a third 47 µF (C16780) if the output rings on a load step.
  - **EN floats** on its internal pull-up. It is rated 7 V max, so never tie it to VIN.
- **D1, 1N5819WS:** feeds the SuperMini's 5 V pin and stops its USB back-feeding the buck while programming. It sits outside the right header next to the 5V pin, so the ESP's supply current stays in the top-right corner, away from U3.
- **+3V3** comes from the SuperMini's LDO and feeds U3, the pull-ups, U5 and the DRV8876 VREF.

## Motor driver: U2, DRV8876RGTR

- PMODE and IMODE tied to GND: PH/EN mode, fixed off-time chopping, auto-retry on overcurrent.
- **Current limit:** R1 = 3.3 kΩ gives I_TRIP = 1.0 A, just above the 12 V N30 gearmotor's stall (just under 1 A measured), and IPROPI reads 3.3 V/A. The ESP32 ADC tops out near 3.1 V, so readings cover 0–0.94 A; a reading pinned at full scale means stalled.
- R1 is an 0805 on hand-solder pads so it's easy to swap: 4.7 kΩ gives 0.70 A, 2.2 kΩ gives 1.5 A.
- C12 (10 nF) filters IPROPI. C5/C6 (100 nF + 22 µF) at VM, C7 on VCP, C8 on CPH–CPL. R2 pulls nFAULT up.

## Magnetometer: U3, MLX90393SLW-ABA-011-RE

I²C mode, address 0x0C (A0 = A1 = GND), INT on MAG_INT. The Hall plates are at the package centre, so U3 is centred over the carriage magnet. R3/R4 are 2.2 kΩ pull-ups; C9/C10 100 nF and C11 10 µF decouple it.

## Layout

### Layout to-do

The full placement plan is in [`main-board/layout plan.md`](main-board/layout%20plan.md).

1. ~~Update PCB from Schematic and place.~~ Done. Placement as built:
   - **Bottom, U2:** C5–C8 along the top edge. R2 right under U2, across nFAULT and VREF (3V3). The strip right of U2 is kept clear for EN/PH and the motor return. R1 (0805) stands on the IPROPI line at (24.3, 29.3), IPROPI pad up, so IPROPI runs straight to C12 and IO1 and MAG_SDA passes underneath.
   - **Bottom, buck:** stacked L1 → C1 → U4. C1 straddles the SW trace, which runs up between C1's pads to L1, so the VIN–GND loop is about 2 mm. C3/C4 sit left of L1, C2 left of U4, and R8/C14/R9 in a row under U4's FB pin.
   - **Bottom, other:** D1 outside the right header, VBUS end towards the 5V pin. TP6 right of C8 next to the 5V pin. U3's caps and pull-ups around U3, with C10 beside pin 8 and R4 on SCL by the CS via. R6 by U5's Rs pin and C13 at (28.9, 39.9) beside U5's VCC via. JP4 and R5 under the XT30 between the bus pins, ≥ 0.98 mm from them.
   - **Top, under the SuperMini:** U5 at 90°: TXD/RXD face IO8/IO9 above the TP row, CANH/CANL leave through the IO10/IO9 gap to the CN2 pins.
2. ~~Route.~~ Done, see [Routing](#routing).
3. **Restore the heat-sink pad, smaller.** It was deleted in the `layout` commit (4722493). The old 28.6–32.4 × 23.45–27.85 rectangle now hits the IPROPI via at (28.45, 27.5) and comes within 0.2 mm of the 3V3 trunk at y 28.15. Use **x 28.95–32.4, y 23.45–27.8** (GND, F.Cu + F.Mask), then refill zones and re-run DRC.
4. Run `scripts/generate_production.py --all` and check the analysis output.

### Routing

| Net group | Layer and path | Width |
|---|---|---|
| 12 V motor feed | Top: CN2 +12V up between H1 and the pad, through the 5V/GND header gap, along the top edge to two vias by C5/C7. VM bus C6–C5–C7 on the bottom, a 0.3 mm link between C5 and C7 into pin 9 | 0.6 mm |
| Motor outputs | Bottom, necked to 0.3 mm at U2. OUT2 runs over the top of J2 pin 1 | 0.5 mm |
| Motor return | GND pours. U2's PGND pins and exposed pad sit in the main bottom pour, which runs straight to CN2's GND pin | — |
| Buck | Bottom. C1 onto VIN/GND, SW up between C1's pads to L1 and under U4 to C2, +5V bar L1–C3–C4 | 0.4–0.6 mm |
| 12 V to the buck | Top through the IO13/IO12 gap, via at (33.55, 32.23), bottom into C1 | 0.4 mm |
| +5V / VBUS | Bottom: L1 out through the 3V3/IO13 gap to D1, D1 to the 5V pin and TP6 | 0.5 mm |
| FB sense | Bottom: branch off +5V outside the header, down the outside, in through the IO9/IO8 gap to R8/C14 | 0.25 mm |
| Driver lines | Bottom fan-out from U2 to four vias, then a top column down the inside of the right header to IO13…IO10, no crossings | 0.2 mm |
| 3V3 | Top trunk from the 3V3 pin at y 28.15, drops at R2/VREF, at R4/CS and at (26.4, 40.0). U3's pins, caps and pull-ups on the bottom; U5/C13/R7/TP5 joined along the bottom edge | 0.25 mm |
| IPROPI | Pin 4 via, top run at y 27.4, via onto R1, bottom to C12 and IO1 | 0.25 mm |
| MAG I²C / INT | Bottom, nested from IO2/3/4, no top copper over them except the 3V3 branch at y 34.8 | 0.2 mm |
| CAN | Top: CANH/CANL through U5's body area, out of the IO10/IO9 gap, under the XT30 to CN2. Termination JP4/R5 on the bottom between the bus pins | 0.2 mm |

- **GND:** SMD GND pads reach the pours by short tracks and vias (the zones are `thru_hole_only`). The buck's bottom GND patch is fenced by its own nets; it reaches the main ground through the via at (28.2, 42.62) and through the top patch above C3/C4 (vias at (28.0, 33.6), (29.6, 33.75) and (27.2, 29.55)). U5's GND pin drops through a via at (28.66, 39.2).
- **Analysis** (`board_analysis.py`, routed board): motor loop −1.6 µT/A at U3; 12 V path 15.9 mΩ, GND return 2.7 mΩ; peak sheet current 4.1 A/mm per amp at U2's VM link. ESP 5 V loop about +11 µT/A (model-sensitive), so roughly 1 µT at 100 mA. Moving IPROPI's long run to the top halved this by letting the 5 V return flow under U2 instead of round U3.

### Guidelines

- **Sides:** every JLC-assembled part on the bottom; the hand-fitted parts (U1 headers, U5, CN2, J2, H1/H2) on the top. Keep bottom parts ≥ 0.9 mm from the U1, J2 and CN2 through-hole pins so they can be hand-soldered.
- **Screw-head circles:** keep bottom copper (other than GND) out of the hatched 3 mm circles round H1/H2 on the back silk. The screw heads and the carriage press there, and the screws are on GND.
- **Motor loop:** run the 12 V feed to U2 directly over the GND return to CN2's GND pin, so the loop is small and its field at U3 cancels. Keep motor copper ≥ 2 mm from the MAG I²C lines.
- **MAG I²C:** keep it away from switching CAN tracks (≥ 0.5 mm) and under unbroken front GND where possible.
- **Heat-sink pad:** a 3.8 × 4.4 mm bare-copper GND pad on the top, right over U2, stitched to U2's thermal vias. It takes a small stick-on sink under the SuperMini. Use **aluminium or copper only**: steel or ferrite would bend the field at U3. Keep the sink below the header height and use insulating tape if it overhangs a track.
- **Zones and rules:** GND pours on both layers; SMD pads join them by tracks only (`thru_hole_only`, prevents 0402 tombstoning). Default 0.2 mm track and clearance, 0.6/0.3 mm vias, 0.3 mm copper-to-edge. [`main-board.kicad_dru`](main-board/main-board.kicad_dru) lets parts sit under U1 (it's on sockets) and lets TP courtyards overlap.
- **Outline:** one closed Edge.Cuts loop. H1/H2 (M2 SMD nuts) are 22 mm apart at (39.6, 22.65) and (39.6, 44.65); the right edge is flush with the XT30 body at x = 42.8. A Ø2.5 mm circle on the back silk marks the old shaft-hole position.

## Known issues

| Issue | What to do |
|---|---|
| **Firmware pin map is still v1.** `firmware/common/config.h` doesn't match the [pin map](#u1-supermini-pin-map) (PWM would go to nSLEEP, current sense uses 2.5 V/A), and there is no CAN code yet | Before first power-up, switch to this board's pins, `CURRENT_SENSE_V_PER_A 3.3`, and add a TWAI driver on TX = GPIO8, RX = GPIO9 |
| **U2 overheats at 1 A continuous** (≈ 1.1 W, about 120 K/W bare in still air) | Stop the motor on stall rather than holding it, read nFAULT, or fit a sink on the heat-sink pad (≈ 77 K/W) |
| **Board heat reaches U3** (≈ 28 K per W in U2), which shifts readings if the firmware applies the magnet tempco from U3's die temperature | Don't apply the magnet tempco from the die temperature, enable `TCMP_EN`, re-zero against an end stop |
| **Motor current shifts the field at U3** by about 1.6 µT/A, and the ESP's supply current by about 11 µT/A (≈ 1 µT at 100 mA, changes with CPU load) | Negligible at the target where motor current is near zero; otherwise compensate as B − k·I. Avoid WiFi during readings |
| **No TVS on 12 V or ESD protection on the bus** | Hot-plugging can overshoot the 25 V caps. Next spin: bulk polymer cap + SMAJ15A, and a CAN TVS (e.g. PESD2CAN) at CN2 |
| **U5 sits under the SuperMini** | Fit and test U5 before soldering the headers. JP4 is on the bottom and can be set at any time |
| **Carriage fit not re-checked** since the nuts and outline moved in rev 5 | Redraw the carriage's screw holes from the component-layout DXF and check clearance to the shaft boss |
| **No SuperMini 3D model**, so the STEP export lacks U1 | Add a STEP to `libraries/3dmodels/` if needed |

## Production

[`scripts/generate_production.py`](main-board/scripts/generate_production.py) builds everything in `production/`. Run it from `PCB/main-board`:

```bash
python3 scripts/generate_production.py --all                      # everything
python3 scripts/generate_production.py --only jlc                 # gerbers, bom, cpl, netlist
python3 scripts/generate_production.py --only dxf,analysis --skip-drc
```

- It first runs DRC with schematic parity and stops on any error. `--skip-drc` skips it, `--ignore-drc` builds anyway, `--clean` deletes earlier outputs.
- Outputs: `JLC/` (Gerber zip, BOM, CPL, IPC-D-356 netlist), `CAD/` (STEP, top/bottom/iso renders, component-layout DXF for Onshape), `analysis/` (DRC report, current and heat maps). A manifest at the top lists what was built and any warnings.
- `analysis` runs [`scripts/board_analysis.py`](main-board/scripts/board_analysis.py) in KiCad's Python (about a minute): current density on the motor and 5 V paths, the motor-current field at U3, and a steady-state thermal map.
- Parts marked "exclude from BOM/position files" (U1, J2, H1, H2, CN2, TPs) are left out of the BOM and CPL.

### Ordering at JLCPCB

1. Upload the Gerber zip: 2 layers, 1.6 mm, HASL.
2. **PCB Assembly → Economic → Bottom side.** Upload the BOM and CPL, and check pin 1 of U2, U3, U4 and D1 in the placement preview.
3. Hand-solder on the top, in this order: U5 (SN65HVD230DR, C12084), then the SuperMini headers, CN2, J2, and H1/H2 (M2 nuts SMTSOM225BTR, C5301773; use hot air, they sit on solid GND).

## Working in KiCad

- Open `main-board/main-board.kicad_pro`. Every symbol, footprint and 3D model is bundled in `libraries/` through `${KIPRJMOD}`. The USB-C footprint and model there are unused leftovers from rev 4.
- KiCad 10 is installed as the flatpak `org.kicad.KiCad`; run `kicad-cli` or KiCad's Python inside it with `--filesystem="$PWD"`.
- Refill zones with **B** in the editor or `kicad-cli pcb drc --refill-zones --save-board` (then `git checkout` the `.kicad_pro`/`.kicad_prl` it rewrites). Don't use pcbnew Python's `ZONE_FILLER`: it ignores the custom rules.
- Order of work: schematic, update PCB from schematic, DRC, regenerate production, and commit `production/` together with the board.
- Back up board files to `main-board/lock-picking-backups/` (gitignored) before editing them outside KiCad.
