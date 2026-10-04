Lock Picking Robot v2 - production outputs
Generated 2026-10-03 23:20 by generate_production.py from ../lock-picking.kicad_pcb (git b6886f1-dirty)
Outputs: gerbers, bom, cpl, netlist, step, render, dxf

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
    DXF layers: Edge.Cuts, F.Fab, B.Fab, F.Courtyard, B.Courtyard (part outlines + refs, courtyards, pad outlines; no tracks or zones)

JLCPCB order: upload the Gerber zip, choose PCB Assembly (Economic, assemble BOTTOM side), then upload the BOM and CPL. Check every part's rotation in JLC's placement preview before paying.
Hand-fitted, not in the BOM/CPL: U1 (ESP32-S3 SuperMini + sockets), J2, H1, H2.

Warnings:
  - STEP: 3D model file not found for U2
  - STEP: footprints with no 3D model assigned: U1
