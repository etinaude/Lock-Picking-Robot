Lock Picking Robot v2 - production outputs
Generated 2026-10-04 20:18 by generate_production.py from ../lock-picking.kicad_pcb (git 7517c4b-dirty)
Outputs: gerbers, bom, cpl, netlist, step, render, dxf, analysis

Lock-Picking-Robot_v2_DRC_report.txt
    KiCad DRC report (with schematic parity) taken before generating
Lock-Picking-Robot_v2_JLCPCB_gerbers.zip
    upload to JLCPCB (11 files: lock-picking-B_Cu.gbl, lock-picking-B_Mask.gbs, lock-picking-B_Paste.gbp, lock-picking-B_Silkscreen.gbo, lock-picking-Edge_Cuts.gm1, lock-picking-F_Cu.gtl, lock-picking-F_Mask.gts, lock-picking-F_Paste.gtp, lock-picking-F_Silkscreen.gto, lock-picking-NPTH.drl, lock-picking-PTH.drl)
Lock-Picking-Robot_v2_gerbers
    the same Gerber/drill files unzipped, plus drill maps (PDF) and the job file
Lock-Picking-Robot_v2_JLCPCB_BOM.csv
    22 parts on 17 lines
Lock-Picking-Robot_v2_JLCPCB_CPL.csv
    22 placements, side(s): Bottom; rotation corrections from /home/etienne/.var/app/org.kicad.KiCad/data/kicad/10.0/3rdparty/plugins/com_github_bennymeg_JLC-Plugin-for-KiCad/transformations.csv
Lock-Picking-Robot_v2_netlist.ipc
    IPC-D-356 netlist (optional upload for the electrical-test comparison)
Lock-Picking-Robot_v2_3D.step
    board + component bodies (copper not included)
Lock-Picking-Robot_v2_3D_top.png
    3D render, top view
Lock-Picking-Robot_v2_3D_bottom.png
    3D render, bottom view
Lock-Picking-Robot_v2_3D_iso.png
    3D render, iso view
Lock-Picking-Robot_v2_component_layout.dxf
    97 lines/arcs/circles on DXF layers Edge.Cuts, F.Courtyard, B.Courtyard: board outline, part courtyards, mounting-hole drills (H1, H2) and pin drills (U1) only (no text, pads or copper); passives left out: C1, C2, C3, C4, C5, C6, C7, C8, C9, C10, C11, C12, L1, R1, R2, R3, R4
Lock-Picking-Robot_v2_current_motor.svg
    motor current (1 A) on both layers and its field at U3: net -6.08 uT/A
Lock-Picking-Robot_v2_current_5V.svg
    ESP 5 V supply loop current (1 A) and its field at U3
Lock-Picking-Robot_v2_thermal.svg
    temperature rise per W in U2: U2 junction 111.1 K/W, U3 35.4 K/W (still air, convection + radiation)
Lock-Picking-Robot_v2_analysis.txt
    the numbers behind the maps

JLCPCB order: upload the Gerber zip, choose PCB Assembly (Economic, assemble BOTTOM side), then upload the BOM and CPL. Check every part's rotation in JLC's placement preview before paying.
Hand-fitted, not in the BOM/CPL: U1 (ESP32-S3 SuperMini + sockets), J2, H1, H2.

Warnings:
  - STEP: 3D model file not found for H1, H2, J1, J2
  - STEP: footprints with no 3D model assigned: U1
