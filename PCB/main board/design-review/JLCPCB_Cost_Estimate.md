# JLCPCB cost estimate: arm board v2, PCBA on the bottom side

**Date:** 2026-10-03. Part prices, stock and Basic/Extended status come live from JLCPCB's parts library. Assembly fees come from JLC's [PCB assembly price page](https://jlcpcb.com/help/article/pcb-assembly-price).
**Excludes:** shipping, import tax/VAT and payment fees.
**Order spec:**

- 2-layer FR4, 1.6 mm, 25.0 × 23.7 mm, green, HASL.
- **Economic PCBA, one side (bottom)**. JLC allows either side for Economic, 2–50 pcs, boards from 10 × 10 mm, and no edge rails needed ([assembly capabilities](https://jlcpcb.com/capabilities/pcb-assembly-capabilities)).

## Estimate

| Assembled / PCBs | PCB | Setup | Stencil | Extended-part feeders | SMT joints | Parts | **Total** | **Per assembled board** |
|---|---|---|---|---|---|---|---|---|
| 2 / 5 | $2.00 | $8.18 | $1.53 | 5 × $3.07 = $15.35 | $0.29 | $13.84 | **≈ $41** | $20.60 |
| 5 / 5 | $2.00 | $8.18 | $1.53 | $15.35 | $0.74 | $28.17 | **≈ $56** | $11.19 |
| 10 / 10 | ≈$5 | $8.18 | $1.53 | $15.35 | $1.47 | $45.35 | **≈ $77** | $7.69 |
| 20 / 20 | ≈$9 | $8.18 | $1.53 | $15.35 | $2.94 | $87.77 | **≈ $125** | $6.24 |
| 50 / 50 | ≈$17 | $8.18 | $1.53 | $15.35 | $7.36 | $191.15 | **≈ $241** | $4.81 |

How the columns are built:

- **SMT joints:** 92 per board at $0.0016 each.
- **Parts:** JLC's price ladders, plus their attrition and minimum quantities for each reel. Those minimums are why small orders pay for 20 × 0402s, but it's pennies.
- **PCB:** $2 for 5 pcs is JLC's standard ≤100 × 100 mm 2-layer price. The 10/20/50-pc board prices are approximate.
- **Optional extras:**
  - ENIG instead of HASL: roughly +$15–20 at these sizes. It gives flatter pads for the 0.5 mm-pitch QFNs, but isn't required.
  - X-ray inspection if JLC asks for the QFNs: about $1.6 per board.
  - Shipping: roughly $3 (economy) to $25 (DHL), depending on country.

## BOM (bottom side, assembled by JLC)

| Part | LCSC | Qty/board | Library | Stock | Unit price (1–9) |
|---|---|---|---|---|---|
| U2 DRV8876RGTR | C1852100 | 1 | **Extended** | 1,978 | $1.395 |
| U3 MLX90393SLW-ABA-011-RE | C2649492 | 1 | **Extended** | 2,316 | $2.437 |
| U4 AP63205WU-7 | C2071056 | 1 | **Extended** | 36,323 | $0.411 |
| J1 HX TYPE-C 16PIN | C5178539 | 1 | **Extended** | 81,230 | $0.075 |
| L1 FTC252012S4R7MBCA | C5832374 | 1 | **Extended** | 64,036 | $0.038 |
| D1 1N5819WS | C191023 | 1 | Basic | 4.8 M | $0.014 |
| C6 22 µF 25 V 1206 | C12891 | 1 | Basic | 585 k | $0.173 |
| C3, C4 47 µF 6.3 V 0805 | C16780 | 2 | Basic | 1.2 M | $0.115 |
| C1 10 µF 25 V 0805 | C15850 | 1 | Basic | 4.9 M | $0.065 |
| C11 10 µF 6.3 V 0402 | C15525 | 1 | Basic | 9.6 M | $0.026 |
| C2, C7, C9, C10 100 nF 16 V 0402 | C1525 | 4 | Basic | 21.6 M | $0.0045 |
| C5 100 nF 50 V 0402 | C307331 | 1 | Basic | 12.5 M | $0.009 |
| C8 22 nF 50 V 0402 | C1532 | 1 | Basic | 611 k | $0.009 |
| C12 10 nF 50 V 0402 | C15195 | 1 | Basic | 4.0 M | $0.005 |
| R1 1.5 k 1 % 0402 | C25867 | 1 | Basic | 2.1 M | $0.003 |
| R2 10 k 0402 | C25744 | 1 | Basic | 21.7 M | $0.003 |
| R3, R4 2.2 k 0402 | C25879 | 2 | Basic | 2.2 M | $0.001 |

Parts cost is ≈ $4.9 per board at 1–9 pcs. U3 (≈50 %) and U2 (≈28 %) dominate. Feeder fees dominate small orders: the five Extended parts cost $15.35 per order, regardless of quantity.

**Not supplied by JLC (hand-fitted, top side):**

- ESP32-S3 SuperMini plus 2 × 9-pin 2.54 mm female headers (≈ $4–6 from AliExpress);
- J2 JST S2B-PH-K-S (C173752, ≈ $0.06);
- 2 × M2 SMD nuts SMTSOM225BTR (C5301773, $0.08 each; you can add them to the JLC order as loose parts).

## Ways to cut it

- **Fewer Extended parts.** J1 and L1 are Extended only because of their specific package; Basic or Preferred-Extended alternatives would save $3.07 each per order. The three ICs have no Basic equivalent.
- **Panelise** (e.g. 2 × 2 with mouse bites) when ordering 10 or more. The setup and feeder fees are per order, not per board.
- Check JLC's "Preferred Extended" list before each order. Those parts have no feeder fee on Economic PCBA.

> A firm quote needs the Gerber zip, BOM and CPL uploaded to cart.jlcpcb.com. All three are in `PCB/production/`. The figures above reproduce JLC's published fee structure with live part prices, so expect to land within a few dollars.
