#!/usr/bin/env python3
"""Build JLCPCB, CAD and analysis outputs for the main board."""
import argparse
import csv
import datetime
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
DEFAULT_BOARD = os.path.join(PROJECT, 'main-board.kicad_pcb')
DEFAULT_OUT = os.path.join(PROJECT, 'production')

OUTPUTS = [
    ('gerbers', 'JLCPCB Gerber + drill files (zip)'),
    ('bom', 'JLCPCB BOM (CSV)'),
    ('cpl', 'JLCPCB pick & place / CPL (CSV)'),
    ('netlist', 'IPC-D-356 netlist for JLC e-test'),
    ('step', 'STEP 3D model of the assembled PCB'),
    ('render', '3D images: top, bottom, isometric (PNG)'),
    ('dxf', 'Component layout DXF (board outline + part courtyards, no text)'),
    ('analysis', 'Current, magnetic-pickup and heat maps (SVG) + numbers, ~1 min'),
]
GROUPS = {'all': [o for o, _ in OUTPUTS], 'jlc': ['gerbers', 'bom', 'cpl', 'netlist']}
# production/ sub-folder of each output; the manifest stays at the top
FOLDERS = {'gerbers': 'JLC', 'bom': 'JLC', 'cpl': 'JLC', 'netlist': 'JLC',
           'step': 'CAD', 'render': 'CAD', 'dxf': 'CAD', 'analysis': 'analysis', 'drc': 'analysis'}

GERBER_LAYERS = ['F.Cu', 'B.Cu', 'F.Paste', 'B.Paste', 'F.SilkS', 'B.SilkS',
                 'F.Mask', 'B.Mask', 'Edge.Cuts']
# KiCad layer -> (DXF layer, DXF colour)
DXF_LAYERS = {'Edge.Cuts': ('Edge.Cuts', 2), 'F.CrtYd': ('F.Courtyard', 1),
              'B.CrtYd': ('B.Courtyard', 5)}
DXF_SKIP_REFS = r'(R|C|L|FB)\d'  # passives, left out of the DXF
DXF_HOLE_REFS = r'H\d'  # mounting holes, drawn as drill circles
DXF_PIN_REFS = r'U1$'  # parts whose pin holes are drawn too
RENDERS = [  # suffix, extra kicad-cli render args
    ('top', ['--side', 'top']),
    ('bottom', ['--side', 'bottom']),
    ('iso', ['--perspective', '--rotate', '-45,0,-30', '--zoom', '0.85', '--floor']),
]

# Fabrication Toolkit rotations, used when the plugin is absent
FALLBACK_ROTATIONS = [
    ('^Bosch_LGA-', 90), ('^CP_EIA-', 180), ('^CP_Elec_', 180), ('^C_Elec_', 180),
    ('^DFN-', 270), ('^D_SOT-23', 180), ('^HTSSOP-', 270), ('^JST_GH_SM', 180),
    ('^JST_PH_S', 180), ('^LQFP-', 270), ('^MSOP-', 270), ('^PowerPAK_SO-8_Single', 270),
    ('^QFN-', 90), ('^R_Array_Concave_', 90), ('^R_Array_Convex_', 90), ('^SC-74-6', 180),
    ('^SOIC-', 270), ('^SOIC127P798X216-8N', -90), ('^SOP-(?!18_)', 270), ('^SOP-18_', 0),
    ('^SOP-4_', 0), ('^SOT-143', 180), ('^SOT-223', 180), ('^SOT-23', 180),
    ('^SOT-353', 180), ('^SOT-363', 180), ('^SOT-89', 180), ('^SSOP-', 270),
    ('^SW_SPST_B3', 90), ('^TDSON-8-1', 270), ('^TO-277', 90), ('^TQFP-', 270),
    ('^TSOT-23', 180), ('^TSSOP-', 270), ('^UDFN-10', 270), ('^USON-10', 270),
    ('^VSON-8_', 270), ('^VSSOP-10_-', 270), ('^VSSOP-8_', 180), ('^qfn-', 90),
]
TOOLKIT_CSV = os.path.expanduser(
    '~/.var/app/org.kicad.KiCad/data/kicad/10.0/3rdparty/plugins/'
    'com_github_bennymeg_JLC-Plugin-for-KiCad/transformations.csv')
LCSC_FIELDS = ['LCSC Part #', 'LCSC Part', 'LCSC PN', 'LCSC P/N', 'LCSC Part No.',
               'LCSC Part Number', 'JLCPCB Part #', 'JLCPCB Part', 'LCSC', 'JLC']


# --------------------------------------------------------------------------- kicad-cli

class KiCadCli:
    def __init__(self, board, out_dir):
        flatpak = shutil.which('flatpak') and subprocess.run(
            ['flatpak', 'info', 'org.kicad.KiCad'], capture_output=True).returncode == 0
        # the flatpak sandbox only sees granted folders
        grants = {os.path.dirname(os.path.dirname(board)), out_dir, HERE}
        sandbox = ['flatpak', 'run'] + [f'--filesystem={g}' for g in sorted(grants)]
        if shutil.which('kicad-cli'):
            self.cmd = ['kicad-cli']
        elif flatpak:
            self.cmd = sandbox + ['--command=kicad-cli', 'org.kicad.KiCad']
        else:
            sys.exit('kicad-cli not found (install KiCad 10 or the org.kicad.KiCad flatpak)')
        # KiCad's Python (pcbnew + numpy) runs board_analysis.py
        if subprocess.run([sys.executable, '-c', 'import pcbnew, numpy'], capture_output=True).returncode == 0:
            self.python = [sys.executable]
        elif flatpak:
            self.python = sandbox + ['--command=python3', 'org.kicad.KiCad']
        else:
            self.python = None

    def run(self, *args, check=True):
        return self._run(self.cmd, args, check, 'kicad-cli')

    def run_python(self, *args):
        if not self.python:
            raise RuntimeError("KiCad's Python (pcbnew + numpy) not found")
        return self._run(self.python, args, True, 'python')[1]

    @staticmethod
    def _run(cmd, args, check, what):
        p = subprocess.run(cmd + list(args), capture_output=True, text=True)
        out = '\n'.join(l for l in (p.stdout + p.stderr).splitlines()
                        if l.strip() and not l.startswith('F:') and 'Debug:' not in l and 'swig/python' not in l)
        if check and p.returncode != 0:
            raise RuntimeError(f'{what} {" ".join(os.path.basename(a) for a in args[:3])} failed:\n{out}')
        return p.returncode, out


# --------------------------------------------------------------------------- board parsing

_TOKEN = re.compile(r'\s*(\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+)', re.S)


def parse_sexpr(text):
    stack = [[]]
    for m in _TOKEN.finditer(text):
        t = m.group(1)
        if t == '(':
            stack.append([])
        elif t == ')':
            node = stack.pop()
            stack[-1].append(node)
        else:
            if t.startswith('"'):
                t = t[1:-1].replace('\\"', '"').replace('\\\\', '\\')
            stack[-1].append(t)
    return stack[0][0]


def children(node, name):
    return [c for c in node if isinstance(c, list) and c and c[0] == name]


def child(node, name):
    found = children(node, name)
    return found[0] if found else None


def load_board(path):
    pcb = parse_sexpr(open(path, encoding='utf-8').read())
    tb = child(pcb, 'title_block') or []
    title = (child(tb, 'title') or [None, 'board'])[1]
    rev = (child(tb, 'rev') or [None, ''])[1]
    footprints = []
    for fp in children(pcb, 'footprint'):
        at = child(fp, 'at')
        x, y = float(at[1]), float(at[2])
        rot = float(at[3]) if len(at) > 3 else 0.0
        props = {p[1]: p[2] for p in children(fp, 'property') if len(p) > 2}
        attr = child(fp, 'attr') or []
        flags = set(attr[1:])
        dnp = 'dnp' in flags or (child(fp, 'dnp') or [None, 'no'])[1] == 'yes' \
            or props.get('Value', '').upper() == 'DNP'
        pads = []
        for pad in children(fp, 'pad'):
            pat = child(pad, 'at')
            size = child(pad, 'size')
            pad_rot = float(pat[3]) if len(pat) > 3 else rot
            drill = child(pad, 'drill')
            pads.append(dict(x=float(pat[1]), y=float(pat[2]), rot=pad_rot - rot,
                             w=float(size[1]), h=float(size[2]), shape=pad[3],
                             prims=_custom_pad_points(pad),
                             drill=float(drill[1]) if drill and drill[1] != 'oval' else None))
        lib_id = fp[1]
        footprints.append(dict(ref=props.get('Reference', '?'), value=props.get('Value', ''),
                               lib_id=lib_id, name=lib_id.split(':')[-1],
                               layer=child(fp, 'layer')[1], x=x, y=y, rot=rot, props=props,
                               in_bom='exclude_from_bom' not in flags and not dnp,
                               in_pos='exclude_from_pos_files' not in flags and not dnp,
                               smd='smd' in flags, pads=pads,
                               models=[m[1] for m in children(fp, 'model')],
                               shapes=_shapes(fp, 'fp_', DXF_LAYERS, (x, y, rot)),
                               holes=[(to_board((x, y, rot), p['x'], p['y']), p['drill'] / 2)
                                      for p in pads if p['drill']]))
    return dict(title=title, rev=rev, footprints=footprints,
                shapes=_shapes(pcb, 'gr_', DXF_LAYERS))


def to_board(place, x, y):
    """Footprint-local point to board coordinates."""
    px, py, rot = place
    a = math.radians(rot)
    return px + x * math.cos(a) + y * math.sin(a), py - x * math.sin(a) + y * math.cos(a)


def _shapes(node, prefix, layers, place=(0.0, 0.0, 0.0)):
    """Lines, arcs and circles of gr_/fp_ items, board coordinates."""
    def xy(item):
        return to_board(place, float(item[1]), float(item[2]))

    def outline(pts):
        return [(layer, 'line', a, b) for a, b in zip(pts, pts[1:] + pts[:1])]

    out = []
    for g in node:
        if not (isinstance(g, list) and g and g[0].startswith(prefix)):
            continue
        kind = g[0][len(prefix):]
        layer = (child(g, 'layer') or [None, None])[1]
        if layer not in layers:
            continue
        if kind == 'line':
            out.append((layer, 'line', xy(child(g, 'start')), xy(child(g, 'end'))))
        elif kind == 'arc':
            out.append((layer, 'arc', xy(child(g, 'start')), xy(child(g, 'mid')), xy(child(g, 'end'))))
        elif kind == 'circle':
            centre = xy(child(g, 'center'))
            out.append((layer, 'circle', centre, math.dist(centre, xy(child(g, 'end')))))
        elif kind == 'rect':
            (x1, y1), (x2, y2) = child(g, 'start')[1:3], child(g, 'end')[1:3]
            out += outline([xy([None, x, y]) for x, y in ((x1, y1), (x2, y1), (x2, y2), (x1, y2))])
        elif kind == 'poly':
            out += outline([xy(p) for p in child(g, 'pts')[1:] if p[0] == 'xy'])
    return out


def _custom_pad_points(pad):
    prims = child(pad, 'primitives')
    pts = []
    if prims:
        for poly in children(prims, 'gr_poly'):
            pts += [(float(p[1]), float(p[2])) for p in child(poly, 'pts')[1:] if p[0] == 'xy']
    return pts


def placement_xy(fp):
    """CPL position: SMD anchor, else the pad-box centre."""
    origin = 'Anchor' if fp['smd'] else 'Center'
    override = (fp['props'].get('FT Origin') or fp['props'].get('Origin') or '').strip().capitalize()
    if override in ('Anchor', 'Center'):
        origin = override
    return (fp['x'], fp['y']) if origin == 'Anchor' else pad_centre(fp)


def pad_centre(fp):
    """Centre of the pads' bounding box, board coordinates."""
    if not fp['pads']:
        return fp['x'], fp['y']
    xs, ys = [], []
    for p in fp['pads']:
        a = math.radians(p['rot'])
        c, s = abs(math.cos(a)), abs(math.sin(a))
        if p['shape'] == 'circle':
            hx = hy = p['w'] / 2
        else:
            hx = p['w'] / 2 * c + p['h'] / 2 * s
            hy = p['w'] / 2 * s + p['h'] / 2 * c
        xs += [p['x'] - hx, p['x'] + hx]
        ys += [p['y'] - hy, p['y'] + hy]
        for px, py in p['prims']:  # custom pad primitives are pad-local
            ca, sa = math.cos(a), math.sin(a)
            xs.append(p['x'] + px * ca + py * sa)
            ys.append(p['y'] - px * sa + py * ca)
    return to_board((fp['x'], fp['y'], fp['rot']), (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)


def rotation_table():
    if os.path.isfile(TOOLKIT_CSV):
        rows = []
        with open(TOOLKIT_CSV, newline='', encoding='utf-8') as f:
            for r in csv.reader(f):
                if not r or r[0].startswith('#') or r[0] == 'Regex To Match':
                    continue
                try:
                    rows.append((r[0].strip(), float(r[1] or 0)))
                except (IndexError, ValueError):
                    pass
        if rows:
            return rows, TOOLKIT_CSV
    return FALLBACK_ROTATIONS, 'built-in copy'


def jlc_rotation(fp, table):
    rot = fp['rot']
    if fp['layer'] == 'B.Cu':
        rot = 180.0 - rot  # JLC wants rotation seen from above
    for regex, extra in table:
        if re.search(regex, fp['name']):
            rot += extra
            break
    rot += float(fp['props'].get('FT Rotation Offset') or fp['props'].get('Rotation Offset') or 0)
    return rot % 360.0


def lcsc_of(fp):
    for key in LCSC_FIELDS:
        if fp['props'].get(key):
            return fp['props'][key]
    return ''


def natural_key(ref):
    m = re.match(r'([A-Za-z#]*)(\d*)', ref)
    return (m.group(1), int(m.group(2) or 0), ref)


def write_dxf(path, shapes):
    """Write an R12 DXF (mm, Y up) of the shapes."""
    def num(v):
        t = f'{v:.6f}'.rstrip('0').rstrip('.')
        return '0' if t == '-0' else t

    def pt(p, code=10):
        return [str(code), num(p[0]), str(code + 10), num(-p[1])]

    out = ['0', 'SECTION', '2', 'HEADER', '9', '$ACADVER', '1', 'AC1009',
           '9', '$INSUNITS', '70', '4', '0', 'ENDSEC',
           '0', 'SECTION', '2', 'TABLES',
           '0', 'TABLE', '2', 'LTYPE', '70', '1',
           '0', 'LTYPE', '2', 'CONTINUOUS', '70', '0', '3', 'Solid line', '72', '65', '73', '0', '40', '0',
           '0', 'ENDTAB', '0', 'TABLE', '2', 'LAYER', '70', str(len(DXF_LAYERS))]
    for name, colour in DXF_LAYERS.values():
        out += ['0', 'LAYER', '2', name, '70', '0', '62', str(colour), '6', 'CONTINUOUS']
    out += ['0', 'ENDTAB', '0', 'ENDSEC', '0', 'SECTION', '2', 'ENTITIES']
    for layer, kind, *geo in shapes:
        head = ['8', DXF_LAYERS[layer][0]]
        if kind == 'circle':
            out += ['0', 'CIRCLE', *head, *pt(geo[0]), '40', num(geo[1])]
            continue
        if kind == 'arc':
            (x1, y1), (x2, y2), (x3, y3) = [(x, -y) for x, y in geo]
            d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
            if abs(d) > 1e-9:
                s1, s2, s3 = x1 * x1 + y1 * y1, x2 * x2 + y2 * y2, x3 * x3 + y3 * y3
                cx = (s1 * (y2 - y3) + s2 * (y3 - y1) + s3 * (y1 - y2)) / d
                cy = (s1 * (x3 - x2) + s2 * (x1 - x3) + s3 * (x2 - x1)) / d
                # centre onto the start/end bisector: ends meet exactly
                mx, my, nx, ny = (x1 + x3) / 2, (y1 + y3) / 2, y1 - y3, x3 - x1
                t = ((cx - mx) * nx + (cy - my) * ny) / (nx * nx + ny * ny)
                cx, cy = mx + t * nx, my + t * ny
                snapped = round(cx, 5), round(cy, 5)  # clean centre if still exact
                if abs(math.dist(snapped, (x1, y1)) - math.dist(snapped, (x3, y3))) < 1e-9:
                    cx, cy = snapped
                a1, a2, a3 = [round(math.degrees(math.atan2(y - cy, x - cx)), 6) % 360
                              for x, y in ((x1, y1), (x2, y2), (x3, y3))]
                if (a2 - a1) % 360 > (a3 - a1) % 360:  # DXF arcs run counter-clockwise
                    a1, a3 = a3, a1
                out += ['0', 'ARC', *head, *pt((cx, -cy)), '40', num(math.dist((cx, cy), (x1, y1))),
                        '50', num(a1), '51', num(a3)]
                continue
            geo = [geo[0], geo[2]]  # degenerate (straight) arc
        out += ['0', 'LINE', *head, *pt(geo[0]), *pt(geo[1], 11)]
    out += ['0', 'ENDSEC', '0', 'EOF']
    with open(path, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')


# --------------------------------------------------------------------------- generators

class Generator:
    def __init__(self, board_path, out_dir, board, prefix):
        self.board_path = board_path
        self.out = out_dir
        self.board = board
        self.prefix = prefix
        self.cli = KiCadCli(board_path, out_dir)
        self.made = []
        self.warnings = []

    def path(self, suffix, output=None):
        folder = os.path.join(self.out, FOLDERS[output]) if output else self.out
        os.makedirs(folder, exist_ok=True)
        return os.path.join(folder, f'{self.prefix}_{suffix}')

    def note(self, path, what):
        self.made.append((os.path.relpath(path, self.out), what))
        print(f'  -> {os.path.relpath(path, self.out)}')

    def preflight_drc(self, allow_errors):
        rpt = self.path('DRC_report.txt', 'drc')
        code, out = self.cli.run('pcb', 'drc', '--schematic-parity', '--severity-all',
                                 '-o', rpt, self.board_path, check=False)
        text = open(rpt, encoding='utf-8').read() if os.path.exists(rpt) else out
        errors = len(re.findall(r';\s*error', text))
        warnings = len(re.findall(r';\s*warning', text))
        unconnected = re.search(r'Found (\d+) unconnected', out)
        parity = re.search(r'Found (\d+) schematic parity', out)
        print(f'  DRC: {errors} errors, {warnings} warnings, '
              f'{unconnected.group(1) if unconnected else "?"} unconnected, '
              f'{parity.group(1) if parity else "?"} schematic parity issues')
        self.note(rpt, 'KiCad DRC report (with schematic parity) taken before generating')
        bad = errors or (unconnected and int(unconnected.group(1))) or (parity and int(parity.group(1)))
        if bad and not allow_errors:
            sys.exit('DRC found errors / unconnected items / parity issues; fix them or rerun with --ignore-drc')
        if bad:
            self.warnings.append('DRC reported errors; outputs were generated anyway (--ignore-drc)')

    def gerbers(self):
        with tempfile.TemporaryDirectory(dir=self.out, prefix='.tmp_') as tmp:  # inside the sandbox grant
            self.cli.run('pcb', 'export', 'gerbers', '--layers', ','.join(GERBER_LAYERS),
                         '--no-x2', '--subtract-soldermask', '--check-zones', '-o', tmp + '/',
                         self.board_path)
            self.cli.run('pcb', 'export', 'drill', '--format', 'excellon', '--drill-origin', 'absolute',
                         '--excellon-units', 'mm', '--excellon-zeros-format', 'decimal',
                         '--excellon-separate-th', '--generate-map', '--map-format', 'pdf',
                         '-o', tmp + '/', self.board_path)
            files = sorted(f for f in os.listdir(tmp) if not f.endswith('.pdf') and not f.endswith('.gbrjob'))
            zpath = self.path('JLCPCB_gerbers.zip', 'gerbers')
            with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
                for f in files:
                    z.write(os.path.join(tmp, f), f)
        self.note(zpath, f'upload to JLCPCB ({len(files)} files: {", ".join(files)})')

    def bom(self):
        rows = {}
        for fp in self.board['footprints']:
            if not fp['in_bom']:
                continue
            key = (fp['value'], fp['name'], lcsc_of(fp))
            r = rows.setdefault(key, dict(refs=[], fp=fp))
            r['refs'].append(fp['ref'])
        path = self.path('JLCPCB_BOM.csv', 'bom')
        missing = []
        with open(path, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #', 'Quantity',
                        'Manufacturer', 'MPN'])
            for (value, name, lcsc), r in sorted(rows.items(), key=lambda kv: natural_key(sorted(kv[1]['refs'], key=natural_key)[0])):
                refs = sorted(r['refs'], key=natural_key)
                props = r['fp']['props']
                if not lcsc:
                    missing += refs
                w.writerow([value, ','.join(refs), name, lcsc, len(refs),
                            props.get('Manufacturer', ''), props.get('MPN', '')])
        if missing:
            self.warnings.append(f'BOM parts without an LCSC number: {", ".join(missing)}')
        self.note(path, f'{sum(len(r["refs"]) for r in rows.values())} parts on {len(rows)} lines')

    def cpl(self):
        table, source = rotation_table()
        path = self.path('JLCPCB_CPL.csv', 'cpl')
        sides = set()
        n = 0
        with open(path, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
            for fp in sorted(self.board['footprints'], key=lambda fp: natural_key(fp['ref'])):
                if not fp['in_pos']:
                    continue
                x, y = placement_xy(fp)
                side = 'Bottom' if fp['layer'] == 'B.Cu' else 'Top'
                sides.add(side)
                w.writerow([fp['ref'], f'{x:.4f}mm', f'{-y:.4f}mm', side,
                            f'{jlc_rotation(fp, table):g}'])
                n += 1
        if len(sides) > 1:
            self.warnings.append('Parts on both sides: JLC Economic quotes this as double-sided assembly')
        self.note(path, f'{n} placements, side(s): {", ".join(sorted(sides))}; rotation corrections from {source}')

    def netlist(self):
        path = self.path('netlist.ipc', 'netlist')
        self.cli.run('pcb', 'export', 'ipcd356', '-o', path, self.board_path)
        self.note(path, 'IPC-D-356 netlist (optional upload for the electrical-test comparison)')

    def step(self):
        path = self.path('3D.step', 'step')
        _, out = self.cli.run('pcb', 'export', 'step', '--subst-models', '--force',
                              '-o', path, self.board_path)
        missing = sorted(set(re.findall(r'Could not add 3D model for (\S+?)\.', out)))
        no_model = [fp['ref'] for fp in self.board['footprints']
                    if not fp['models'] and not fp['ref'].startswith('TP')]
        if missing:
            self.warnings.append(f'STEP: 3D model file not found for {", ".join(missing)}')
        if no_model:
            self.warnings.append(f'STEP: footprints with no 3D model assigned: {", ".join(no_model)}')
        self.note(path, 'board + component bodies (copper not included)')

    def render(self):
        for suffix, args in RENDERS:
            path = self.path(f'3D_{suffix}.png', 'render')
            self.cli.run('pcb', 'render', '--quality', 'high', '-w', '2000', '-h', '1500',
                         '--background', 'opaque', *args, '-o', path, self.board_path)
            self.note(path, f'3D render, {suffix} view')

    def dxf(self):
        path = self.path('component_layout.dxf', 'dxf')
        shapes = list(self.board['shapes'])
        skipped, holes, pins, no_courtyard = [], [], [], []
        for fp in sorted(self.board['footprints'], key=lambda fp: natural_key(fp['ref'])):
            if re.match(DXF_SKIP_REFS, fp['ref']):
                skipped.append(fp['ref'])
                drawn = []
            elif re.match(DXF_HOLE_REFS, fp['ref']):
                holes.append(fp['ref'])
                drawn = [('Edge.Cuts', 'circle', c, r) for c, r in fp['holes']]
            else:
                drawn = [s for s in fp['shapes'] if s[0] != 'Edge.Cuts']
                if not drawn:
                    no_courtyard.append(fp['ref'])
                if re.match(DXF_PIN_REFS, fp['ref']):
                    pins.append(fp['ref'])
                    side = 'B.CrtYd' if fp['layer'] == 'B.Cu' else 'F.CrtYd'
                    drawn += [(side, 'circle', c, r) for c, r in fp['holes']]
            shapes += drawn + [s for s in fp['shapes'] if s[0] == 'Edge.Cuts']
        write_dxf(path, shapes)
        if no_courtyard:
            self.warnings.append(f'DXF: no courtyard drawn, so not shown: {", ".join(no_courtyard)}')
        self.note(path, f'{len(shapes)} lines/arcs/circles on DXF layers '
                  f'{", ".join(n for n, _ in DXF_LAYERS.values())}: board outline, part courtyards, '
                  f'mounting-hole drills ({", ".join(holes)}) and pin drills ({", ".join(pins)}) '
                  f'only (no text, pads or copper); '
                  f'passives left out: {", ".join(skipped)}')

    def analysis(self):
        folder = os.path.dirname(self.path('analysis.txt', 'analysis'))
        out = self.cli.run_python(os.path.join(HERE, 'board_analysis.py'), self.board_path,
                                  os.path.join(folder, self.prefix))
        if 'warning:' in out:
            self.warnings.append('analysis: ' + '; '.join(l for l in out.splitlines() if 'warning:' in l))
        num = lambda pat: (re.search(pat, out) or [None, '?'])[1]
        field = num(r'NET\s+([-+\d.]+) uT/A   \(sign')
        tj, u3 = num(r'radiation : .*?U2 junction\s+([\d.]+)'), num(r'radiation : .*?U3\s+([\d.]+) K')
        self.note(self.path('current_motor.svg', 'analysis'),
                  f'motor current (1 A) on both layers and its field at U3: net {field} uT/A')
        self.note(self.path('current_5V.svg', 'analysis'), 'ESP 5 V supply loop current (1 A) and its field at U3')
        self.note(self.path('thermal.svg', 'analysis'), f'temperature rise per W in U2: U2 junction {tj} K/W, '
                  f'U3 {u3} K/W (still air, convection + radiation)')
        self.note(self.path('analysis.txt', 'analysis'), 'the numbers behind the maps')

    def manifest(self, chosen):
        try:
            git = subprocess.run(['git', '-C', os.path.dirname(self.board_path), 'describe',
                                  '--always', '--dirty'], capture_output=True, text=True).stdout.strip()
        except OSError:
            git = ''
        path = self.path('manifest.txt')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(f'{self.board["title"]} {self.board["rev"]} - production outputs\n')
            f.write(f'Generated {datetime.datetime.now():%Y-%m-%d %H:%M} by scripts/generate_production.py '
                    f'from {os.path.relpath(self.board_path, self.out)}'
                    f'{" (git " + git + ")" if git else ""}\n')
            f.write(f'Outputs: {", ".join(chosen)}\n\n')
            for name, what in self.made:
                f.write(f'{name}\n    {what}\n')
            f.write('\nJLCPCB order (files in JLC/): upload the Gerber zip, choose PCB Assembly (Economic, '
                    'assemble BOTTOM side), then upload the BOM and CPL. Check every part\'s '
                    'rotation in JLC\'s placement preview before paying.\n'
                    'Hand-fitted, not in the BOM/CPL: U1 (ESP32-S3 SuperMini + sockets), J2, H1, H2.\n'
                    'Design notes: PCB/README.md\n')
            if self.warnings:
                f.write('\nWarnings:\n' + ''.join(f'  - {w}\n' for w in self.warnings))
        print(f'  -> {os.path.relpath(path, self.out)}')


# --------------------------------------------------------------------------- CLI

def parse_selection(text):
    chosen = []
    for tok in re.split(r'[\s,]+', text.strip().lower()):
        if not tok:
            continue
        if tok in GROUPS:
            chosen += GROUPS[tok]
        elif tok.isdigit() and 1 <= int(tok) <= len(OUTPUTS):
            chosen.append(OUTPUTS[int(tok) - 1][0])
        elif tok in dict(OUTPUTS):
            chosen.append(tok)
        else:
            raise ValueError(f'unknown output "{tok}"')
    return [o for o, _ in OUTPUTS if o in chosen]


def ask():
    print('Which outputs should be generated?')
    for i, (name, what) in enumerate(OUTPUTS, 1):
        print(f'  {i}) {name:8} {what}')
    print('Enter numbers or names separated by commas, "jlc" for 1-4, or press Enter for all.')
    while True:
        try:
            text = input('Generate [all]: ')
        except EOFError:
            text = ''
        if not text.strip():
            return GROUPS['all']
        try:
            chosen = parse_selection(text)
            if chosen:
                return chosen
        except ValueError as e:
            print(f'  {e}')


def clean(out, prefix):
    """Delete earlier outputs (files starting with the prefix) here and in the sub-folders."""
    for folder in [out] + [os.path.join(out, f) for f in sorted(set(FOLDERS.values()))]:
        if not os.path.isdir(folder):
            continue
        for name in os.listdir(folder):
            p = os.path.join(folder, name)
            if name.startswith(prefix + '_') or name.startswith('.tmp_'):
                shutil.rmtree(p) if os.path.isdir(p) and not os.path.islink(p) else os.remove(p)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 epilog='outputs: ' + ', '.join(o for o, _ in OUTPUTS) +
                                        ' (groups: all, jlc). Details: PCB/README.md')
    ap.add_argument('--board', default=DEFAULT_BOARD, help='path to the .kicad_pcb (default: %(default)s)')
    ap.add_argument('--out', default=DEFAULT_OUT, help='output directory (default: %(default)s)')
    sel = ap.add_mutually_exclusive_group()
    sel.add_argument('--only', help='comma-separated outputs to generate, e.g. gerbers,bom,cpl')
    sel.add_argument('--all', action='store_true', help='generate everything without asking')
    ap.add_argument('--clean', action='store_true', help='delete earlier outputs of this board first')
    ap.add_argument('--skip-drc', action='store_true', help='do not run the DRC pre-flight check')
    ap.add_argument('--ignore-drc', action='store_true', help='generate even if DRC finds errors')
    args = ap.parse_args()

    if args.only:
        try:
            chosen = parse_selection(args.only)
        except ValueError as e:
            ap.error(str(e))
    elif args.all or not sys.stdin.isatty():
        chosen = GROUPS['all']
    else:
        chosen = ask()

    board_path = os.path.abspath(args.board)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    board = load_board(board_path)
    prefix = re.sub(r'[^A-Za-z0-9.-]+', '-', board['title']).strip('-') + \
        (f'_{board["rev"]}' if board['rev'] else '')
    if args.clean:
        clean(out, prefix)
    gen = Generator(board_path, out, board, prefix)
    print(f'{board["title"]} {board["rev"]}: generating {", ".join(chosen)} into {out}')
    if not args.skip_drc:
        print('[drc]')
        gen.preflight_drc(args.ignore_drc)
    for name in chosen:
        print(f'[{name}]')
        getattr(gen, name)()
    gen.manifest(chosen)
    if gen.warnings:
        print('\nWarnings:')
        for w in gen.warnings:
            print(f'  - {w}')
    print('Done.')


if __name__ == '__main__':
    main()
