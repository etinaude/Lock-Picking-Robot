#!/usr/bin/env python3
"""Current, magnetic-pickup and heat maps for the main board."""
import base64, math, struct, sys, zlib
import numpy as np
import pcbnew

GRID = 0.08                     # mm
T_CU, T_PLATE, T_FR4 = 35e-6, 20e-6, 1.6e-3
RHO_CU, K_CU, K_FR4 = 1.72e-8, 385.0, 0.3
HALL_BELOW = 0.5e-3             # Hall plate depth below the bottom copper
H_CONV = (10.0, 12.0)           # W/m2K top, bottom: still air, convection only
H_RAD = (18.0, 24.0)            # ... plus radiation (the realistic case)
RJC_U2 = 7.1                    # DRV8876 RGT junction to exposed pad, K/W

SENSOR = 'U3'
MOTOR = dict(supply=('+12V', [('J1', '+12V')], [('U2', '9')]),
             ret=('GND', [('U2', 'GND')], [('J1', 'GND')]),
             leads=[('/MOT_OUT1', [('U2', '6')], [('J2', '1')]), ('/MOT_OUT2', [('J2', '2')], [('U2', '8')])],
             cable=(('J2', '1'), ('J2', '2')))          # cable leaves J2 towards the top edge
SUPPLY = [('+5V', [('L1', '2')], [('D1', '2')]), ('VBUS', [('D1', '1')], [('U1', '5V')]),
          ('GND', [('U1', 'GND')], [('U4', '4')]), ('Net-(U4-SW)', [('U4', '5')], [('L1', '1')])]
SUPPLY_BODIES = [(('L1', '1'), ('L1', '2'), -2.2), (('D1', '2'), ('D1', '1'), -2.1),
                 (('U4', '4'), ('U4', '5'), -2.1), (('U1', '5V'), ('U1', 'GND'), 8.0)]   # line currents, z mm
HEAT_U2 = [('U2', '17', 1.0)]
HEAT_BUCK = [('U4', None, 0.05), ('L1', None, 0.04), ('D1', None, 0.04)]


# ------------------------------------------------------------------------- geometry

class Board:
    def __init__(self, path):
        self.b = pcbnew.LoadBoard(path)
        box = self.b.GetBoardEdgesBoundingBox()
        mm = pcbnew.ToMM
        self.x0, self.y0 = mm(box.GetLeft()), mm(box.GetTop())
        self.nx = int(math.ceil((mm(box.GetRight()) - self.x0) / GRID))
        self.ny = int(math.ceil((mm(box.GetBottom()) - self.y0) / GRID))
        self.xs = self.x0 + GRID * (np.arange(self.nx) + 0.5)
        self.ys = self.y0 + GRID * (np.arange(self.ny) + 0.5)
        self.layers = (pcbnew.F_Cu, pcbnew.B_Cu)
        self.fps = {fp.GetReference(): fp for fp in self.b.Footprints()}
        self.edges = self._edges()
        self.board = self._fill(self.edges['outline']) & ~self._fill_any(self.edges['cutouts'])
        self._cache = {}

    # even-odd scanline fill of contours, clipped to the grid
    def _fill(self, contours):
        m = np.zeros((self.ny, self.nx), bool)
        if not contours:
            return m
        pts = np.vstack(contours)
        i0 = max(0, int((pts[:, 0].min() - self.x0) / GRID) - 1)
        i1 = min(self.nx, int((pts[:, 0].max() - self.x0) / GRID) + 2)
        j0 = max(0, int((pts[:, 1].min() - self.y0) / GRID) - 1)
        j1 = min(self.ny, int((pts[:, 1].max() - self.y0) / GRID) + 2)
        if i1 <= i0 or j1 <= j0:
            return m
        yc = self.ys[j0:j1]
        diff = np.zeros((j1 - j0, i1 - i0 + 1), np.int32)
        for c in contours:
            a, b = c, np.roll(c, -1, axis=0)
            cross = (a[None, :, 1] <= yc[:, None]) != (b[None, :, 1] <= yc[:, None])
            r, e = np.nonzero(cross)
            xi = a[e, 0] + (yc[r] - a[e, 1]) * (b[e, 0] - a[e, 0]) / (b[e, 1] - a[e, 1])
            col = np.clip(np.ceil((xi - self.x0) / GRID - 0.5).astype(int) - i0, 0, i1 - i0)
            np.add.at(diff, (r, col), 1)
        m[j0:j1, i0:i1] = (np.cumsum(diff, axis=1)[:, :-1] % 2).astype(bool)
        return m

    def _fill_any(self, contours):
        m = np.zeros((self.ny, self.nx), bool)
        for c in contours:
            m |= self._fill([c])
        return m

    def _polyset(self, ps):
        out = []
        for o in range(ps.OutlineCount()):
            chains = [ps.COutline(o)] + [ps.CHole(o, h) for h in range(ps.HoleCount(o))]
            out.append([np.array([(c.CPoint(i).x, c.CPoint(i).y) for i in range(c.PointCount())], float) / 1e6
                        for c in chains if c.PointCount() > 2])
        return out

    def _edges(self):
        outline, cutouts = [], []
        segs = []
        for d in self.b.GetDrawings():
            if d.GetLayer() != pcbnew.Edge_Cuts:
                continue
            s = d.GetShape()
            if s == pcbnew.SHAPE_T_CIRCLE:
                c, r = d.GetCenter(), pcbnew.ToMM(d.GetRadius())
                t = np.linspace(0, 2 * math.pi, 64, endpoint=False)
                cutouts.append(np.c_[pcbnew.ToMM(c.x) + r * np.cos(t), pcbnew.ToMM(c.y) + r * np.sin(t)])
            elif s == pcbnew.SHAPE_T_ARC:
                segs.append(_arc_points(*(np.array([pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)])
                                          for p in (d.GetStart(), d.GetArcMid(), d.GetEnd()))))
            elif s == pcbnew.SHAPE_T_SEGMENT:
                segs.append(np.array([[pcbnew.ToMM(d.GetStart().x), pcbnew.ToMM(d.GetStart().y)],
                                      [pcbnew.ToMM(d.GetEnd().x), pcbnew.ToMM(d.GetEnd().y)]]))
            else:   # rect / poly: take the shape's own polygon
                ps = pcbnew.SHAPE_POLY_SET()
                d.TransformShapeToPolygon(ps, pcbnew.Edge_Cuts, 0, pcbnew.FromMM(0.005), pcbnew.ERROR_INSIDE)
                outline += [c[0] for c in self._polyset(ps)]
        # chain the segments/arcs into the outline
        while segs:
            chain = list(segs.pop(0))
            grown = True
            while grown:
                grown = False
                for k, s in enumerate(segs):
                    for cand in (s, s[::-1]):
                        if np.hypot(*(cand[0] - chain[-1])) < 1e-3:
                            chain += list(cand[1:]); segs.pop(k); grown = True; break
                    if grown:
                        break
            outline.append(np.array(chain))
        return dict(outline=outline, cutouts=cutouts)

    def copper(self, layer, net=None):
        """Copper raster of a layer, optionally one net."""
        key = (layer, net)
        if key in self._cache:
            return self._cache[key]
        m = np.zeros((self.ny, self.nx), bool)
        err = pcbnew.FromMM(0.005)
        items = [t for t in self.b.GetTracks() if t.IsOnLayer(layer)]
        items += [p for fp in self.b.Footprints() for p in fp.Pads() if p.IsOnLayer(layer)]
        for it in items:
            if net is not None and it.GetNetname() != net:
                continue
            ps = pcbnew.SHAPE_POLY_SET()
            it.TransformShapeToPolygon(ps, layer, 0, err, pcbnew.ERROR_INSIDE)
            for contours in self._polyset(ps):
                m |= self._fill(contours)
        for z in self.b.Zones():
            if z.GetIsRuleArea() or not z.IsOnLayer(layer) or (net is not None and z.GetNetname() != net):
                continue
            for contours in self._polyset(z.GetFilledPolysList(layer)):
                m |= self._fill(contours)
        m &= self.board
        self._cache[key] = m
        return m

    def pads(self, spec, net=None):
        """Pads matching (ref, number or net name) specs."""
        out = []
        for ref, what in spec:
            for p in self.fps[ref].Pads():
                if what is None or p.GetNumber() == what or p.GetNetname() == what:
                    if net is None or p.GetNetname() == net:
                        out.append(p)
        return out

    def pad_mask(self, pads, layer):
        m = np.zeros((self.ny, self.nx), bool)
        for p in pads:
            if p.IsOnLayer(layer):
                ps = pcbnew.SHAPE_POLY_SET()
                p.TransformShapeToPolygon(ps, layer, 0, pcbnew.FromMM(0.005), pcbnew.ERROR_INSIDE)
                for contours in self._polyset(ps):
                    m |= self._fill(contours)
        return m

    def barrels(self, net=None):
        """Via and PTH barrels: (row, col, S, W/K)."""
        out, seen = [], set()
        for t in self.b.GetTracks():
            if t.Type() == pcbnew.PCB_VIA_T and (net is None or t.GetNetname() == net):
                out.append((t.GetPosition(), pcbnew.ToMM(t.GetDrillValue())))
        for fp in self.b.Footprints():
            for p in fp.Pads():
                if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and (net is None or p.GetNetname() == net):
                    out.append((p.GetPosition(), pcbnew.ToMM(min(p.GetDrillSizeX(), p.GetDrillSizeY()))))
        res = []
        for pos, drill in out:
            j, i = self.cell(pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y))
            wall = math.pi * drill * 1e-3 * T_PLATE
            res.append((j, i, wall / (RHO_CU * T_FR4), K_CU * wall / T_FR4))
        return res

    def cell(self, x, y):
        return (int(np.clip((y - self.y0) / GRID, 0, self.ny - 1)), int(np.clip((x - self.x0) / GRID, 0, self.nx - 1)))

    def pad_xy(self, ref, num):
        p = next(p for p in self.fps[ref].Pads() if p.GetNumber() == num)
        return pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)


def _arc_points(s, m, e, n=24):
    (x1, y1), (x2, y2), (x3, y3) = s, m, e
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    if abs(d) < 1e-12:
        return np.array([s, e])
    s1, s2, s3 = x1 * x1 + y1 * y1, x2 * x2 + y2 * y2, x3 * x3 + y3 * y3
    cx = (s1 * (y2 - y3) + s2 * (y3 - y1) + s3 * (y1 - y2)) / d
    cy = (s1 * (x3 - x2) + s2 * (x1 - x3) + s3 * (x2 - x1)) / d
    a1, a2, a3 = (math.atan2(y - cy, x - cx) for x, y in (s, m, e))
    sweep = (a3 - a1) % (2 * math.pi)
    if (a2 - a1) % (2 * math.pi) > sweep:
        sweep -= 2 * math.pi
    t = a1 + sweep * np.linspace(0, 1, n)
    r = math.hypot(x1 - cx, y1 - cy)
    return np.c_[cx + r * np.cos(t), cy + r * np.sin(t)]


# ------------------------------------------------------------------------- solvers

def pcg(apply, b, diag, free, tol=1e-9, maxit=40000):
    """Jacobi-preconditioned conjugate gradient on the free cells."""
    x = np.zeros_like(b)
    r = np.where(free, b - apply(x), 0.0)
    inv = np.where(free & (diag > 0), 1.0 / np.where(diag > 0, diag, 1), 0.0)
    z = inv * r; p = z.copy(); rz = np.sum(r * z)
    bn = math.sqrt(np.sum(b * b)) or 1.0
    for it in range(maxit):
        q = np.where(free, apply(p), 0.0)
        a = rz / np.sum(p * q)
        x += a * p; r -= a * q
        if math.sqrt(np.sum(r * r)) < tol * bn:
            break
        z = inv * r; rz2 = np.sum(r * z); p = z + (rz2 / rz) * p; rz = rz2
    else:
        print(f'warning: solver stopped after {maxit} iterations', file=sys.stderr)
    return x, it


class Network:
    """Two-layer conductance grid: lateral, vertical, to ambient."""
    def __init__(self, ny, nx):
        self.gx = np.zeros((2, ny, nx - 1)); self.gy = np.zeros((2, ny - 1, nx))
        self.gz = np.zeros((ny, nx)); self.gd = np.zeros((2, ny, nx))

    def apply(self, p):
        out = self.gd * p
        f = self.gx * (p[:, :, :-1] - p[:, :, 1:]); out[:, :, :-1] += f; out[:, :, 1:] -= f
        f = self.gy * (p[:, :-1, :] - p[:, 1:, :]); out[:, :-1, :] += f; out[:, 1:, :] -= f
        f = self.gz * (p[0] - p[1]); out[0] += f; out[1] -= f
        return out

    def diag(self):
        dg = self.gd.copy()
        dg[:, :, :-1] += self.gx; dg[:, :, 1:] += self.gx
        dg[:, :-1, :] += self.gy; dg[:, 1:, :] += self.gy
        dg[0] += self.gz; dg[1] += self.gz
        return dg


def solve_path(B, net, src, snk):
    """DC current of one path, scaled to 1 A."""
    cu = np.stack([B.copper(L, net) for L in B.layers])
    N = Network(B.ny, B.nx); g = T_CU / RHO_CU
    N.gx[:] = g * (cu[:, :, :-1] & cu[:, :, 1:]); N.gy[:] = g * (cu[:, :-1, :] & cu[:, 1:, :])
    for j, i, ge, _ in B.barrels(net):
        if cu[0, j, i] or cu[1, j, i]:
            N.gz[j, i] += ge
    hi = np.stack([B.pad_mask(B.pads(src, net), L) for L in B.layers]) & cu
    lo = np.stack([B.pad_mask(B.pads(snk, net), L) for L in B.layers]) & cu
    assert hi.any() and lo.any(), (net, src, snk)
    fixed = hi | lo; phi0 = hi.astype(float)
    free = cu & ~fixed
    b = -N.apply(phi0)
    x, _ = pcg(N.apply, np.where(free, b, 0.0), N.diag(), free)
    phi = np.where(free, x, phi0)
    ix = N.gx * (phi[:, :, :-1] - phi[:, :, 1:]); iy = N.gy * (phi[:, :-1, :] - phi[:, 1:, :])
    out = N.apply(phi)                       # net current leaving each cell
    i_total = out[hi].sum()
    assert i_total > 0, f'{net}: source and sink are not connected'
    return ix / i_total, iy / i_total, 1.0 / i_total


def bz_sheet(B, ix, iy, hall):
    """Per-cell B_z (T) at the Hall plate."""
    z = (0.0, -T_FR4); h = GRID * 1e-3; dens = np.zeros((B.ny, B.nx))
    X, Y = np.meshgrid(B.xs, B.ys)
    for k in (0, 1):
        mx, my = (X[:, :-1] + GRID / 2) * 1e-3, -Y[:, :-1] * 1e-3            # +x links
        rx, ry, rz = hall[0] - mx, hall[1] - my, hall[2] - z[k]
        dens[:, :-1] += 1e-7 * ix[k] * h * ry / (rx * rx + ry * ry + rz * rz) ** 1.5
        mx, my = X[:-1, :] * 1e-3, -(Y[:-1, :] + GRID / 2) * 1e-3          # +y (KiCad) links: dl=(0,-h)
        rx, ry = hall[0] - mx, hall[1] - my
        dens[:-1, :] += 1e-7 * iy[k] * h * rx / (rx * rx + ry * ry + rz * rz) ** 1.5
    return dens


def bz_line(pts, hall, n=400):
    """B_z (T) of 1 A along a polyline (mm)."""
    B = 0.0
    for p, q in zip(pts, pts[1:]):
        a = np.array([p[0], -p[1], p[2]]) * 1e-3; c = np.array([q[0], -q[1], q[2]]) * 1e-3
        m = a + np.outer((np.arange(n) + 0.5) / n, c - a); dl = (c - a) / n
        R = hall - m
        B += np.sum((dl[0] * R[:, 1] - dl[1] * R[:, 0]) / np.linalg.norm(R, axis=1) ** 3)
    return 1e-7 * B


def sheet_density(ix, iy):
    """|K| per cell, A/mm, for both layers."""
    kx = np.zeros(ix.shape[:1] + (ix.shape[1], ix.shape[2] + 1)); ky = np.zeros_like(kx)
    kx[:, :, :-1] += ix / 2; kx[:, :, 1:] += ix / 2; ky[:, :-1, :] += iy / 2; ky[:, 1:, :] += iy / 2
    return np.hypot(kx, ky) / GRID


def thermal(B, sources, h_top, h_bot):
    cu = np.stack([B.copper(L) for L in B.layers]) & B.board
    A = (GRID * 1e-3) ** 2
    g = np.where(cu, K_CU * T_CU, 0.0) + K_FR4 * T_FR4 / 2
    g = np.where(B.board, g, 0.0)
    N = Network(B.ny, B.nx)
    with np.errstate(divide='ignore', invalid='ignore'):
        N.gx[:] = np.nan_to_num(2 * g[:, :, :-1] * g[:, :, 1:] / (g[:, :, :-1] + g[:, :, 1:]))
        N.gy[:] = np.nan_to_num(2 * g[:, :-1, :] * g[:, 1:, :] / (g[:, :-1, :] + g[:, 1:, :]))
    N.gz[:] = np.where(B.board, K_FR4 * A / T_FR4, 0.0)
    for j, i, _, gt in B.barrels():
        N.gz[j, i] += gt
    N.gd[0] = np.where(B.board, h_top * A, 0.0); N.gd[1] = np.where(B.board, h_bot * A, 0.0)
    q = np.zeros((2, B.ny, B.nx))
    for ref, num, watts in sources:          # heat enters the copper on the part's own side
        k = 1 if B.fps[ref].IsFlipped() else 0
        m = B.pad_mask(B.pads([(ref, num)]), B.layers[k]) & B.board
        q[k][m] += watts / m.sum()
    free = np.broadcast_to(B.board, q.shape)
    T, _ = pcg(N.apply, q, N.diag(), free, tol=1e-10)
    return T


# ------------------------------------------------------------------------- drawing

CMAPS = {
    'heat': [(0, (0, 0, 4)), (0.25, (87, 16, 110)), (0.5, (188, 55, 84)), (0.75, (249, 142, 9)), (1, (252, 255, 164))],
    'div': [(0, (5, 48, 97)), (0.25, (67, 147, 195)), (0.5, (247, 247, 247)), (0.75, (214, 96, 77)), (1, (103, 0, 31))],
}


def colourise(v, cmap, lo, hi):
    stops = CMAPS[cmap]; t = np.clip((v - lo) / (hi - lo), 0, 1)
    out = np.zeros(v.shape + (3,))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        m = (t >= t0) & (t <= t1); f = ((t - t0) / (t1 - t0))[m][:, None]
        out[m] = np.array(c0) + f * (np.array(c1) - np.array(c0))
    return out.astype(np.uint8)


def png(rgb):
    h, w, _ = rgb.shape
    raw = b''.join(b'\x00' + rgb[y].tobytes() for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def svg_figure(B, path, title, panels, notes, labels):
    """Write an SVG of heatmap panels with labels."""
    S = 12.0                                        # px per mm
    pw, ph = B.nx * GRID * S, B.ny * GRID * S
    pad, top, bar = 30, 60, 52
    W = pad + len(panels) * (pw + bar + pad)
    Hh = top + ph + 30 + 18 * len(notes) + 20
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.0f}" height="{Hh:.0f}" viewBox="0 0 {W:.0f} {Hh:.0f}" '
         f'font-family="Helvetica, Arial, sans-serif">',
         f'<rect width="100%" height="100%" fill="white"/>',
         f'<text x="{pad}" y="26" font-size="17" font-weight="bold">{esc(title)}</text>']
    for k, (sub, val, cmap, lo, hi, unit, shown, context) in enumerate(panels):
        ox = pad + k * (pw + bar + pad); oy = top
        rgb = np.full((B.ny, B.nx, 3), 255, np.uint8)
        rgb[B.board] = (236, 236, 236)
        if context is not None:
            rgb[context & B.board] = (205, 205, 205)
        col = colourise(val, cmap, lo, hi)
        rgb[shown] = col[shown]
        data = base64.b64encode(png(rgb)).decode()
        o.append(f'<text x="{ox}" y="{oy - 8}" font-size="13">{esc(sub)}</text>')
        o.append(f'<image x="{ox}" y="{oy}" width="{pw:.1f}" height="{ph:.1f}" preserveAspectRatio="none" '
                 f'href="data:image/png;base64,{data}"/>')
        tx = lambda x: ox + (x - B.x0) * S; ty = lambda y: oy + (y - B.y0) * S
        for c in B.edges['outline'] + B.edges['cutouts']:
            pts = ' '.join(f'{tx(x):.1f},{ty(y):.1f}' for x, y in c)
            o.append(f'<polygon points="{pts}" fill="none" stroke="#444" stroke-width="1"/>')
        for text, x, y, style in labels:
            X, Y = tx(x), ty(y)
            if style == 'cross':
                o.append(f'<path d="M{X - 9:.1f},{Y:.1f}h18M{X:.1f},{Y - 9:.1f}v18" stroke="#00c8ff" stroke-width="2.5"/>')
                o.append(f'<text x="{X + 8:.1f}" y="{Y - 6:.1f}" font-size="12" fill="#00a0d0" font-weight="bold">{esc(text)}</text>')
            else:
                o.append(f'<text x="{X:.1f}" y="{Y:.1f}" font-size="10" fill="#1a3a8a" text-anchor="middle" '
                         f'stroke="white" stroke-width="2.5" paint-order="stroke">{esc(text)}</text>')
        # colour bar
        bx, by, bh = ox + pw + 10, oy, ph
        stops = ''.join(f'<stop offset="{t}" stop-color="rgb{c}"/>' for t, c in CMAPS[cmap])
        o.append(f'<defs><linearGradient id="g{k}" x1="0" y1="1" x2="0" y2="0">{stops}</linearGradient></defs>')
        o.append(f'<rect x="{bx}" y="{by}" width="12" height="{bh:.1f}" fill="url(#g{k})" stroke="#888"/>')
        for f in (0, 0.25, 0.5, 0.75, 1):
            v = lo + f * (hi - lo)
            o.append(f'<text x="{bx + 15}" y="{by + bh * (1 - f) + 4:.1f}" font-size="10">{v:.3g}</text>')
        o.append(f'<text x="{bx}" y="{by + bh + 16:.1f}" font-size="10">{esc(unit)}</text>')
    for k, n in enumerate(notes):
        o.append(f'<text x="{pad}" y="{top + ph + 34 + 18 * k:.0f}" font-size="12">{esc(n)}</text>')
    o.append('</svg>')
    open(path, 'w', encoding='utf-8').write('\n'.join(o))


def robust_max(v, q=99.0):
    v = np.abs(v[v != 0])
    return float(np.percentile(v, q)) if v.size else 1.0


# ------------------------------------------------------------------------- main

def main(board_path, prefix):
    B = Board(board_path)
    s = B.fps[SENSOR].GetPosition(); sx, sy = pcbnew.ToMM(s.x), pcbnew.ToMM(s.y)
    hall = np.array([sx * 1e-3, -sy * 1e-3, -T_FR4 - HALL_BELOW])
    labels = [(SENSOR, sx, sy, 'cross')] + [(r, pcbnew.ToMM(B.fps[r].GetPosition().x), pcbnew.ToMM(B.fps[r].GetPosition().y), 'ref')
                                            for r in ('U2', 'U4', 'L1', 'D1', 'J1', 'J2') if r in B.fps]
    lines = [f'Board analysis of {board_path}', f'grid {GRID} mm; field at {SENSOR} Hall plate ({sx:.2f}, {sy:.2f}), '
             f'{HALL_BELOW * 1e3:.1f} mm below the bottom copper; B_z sign: + = towards the top face', '']
    any_cu = np.stack([B.copper(L) for L in B.layers])

    # ---- motor loop
    parts, dens, K = {}, np.zeros((B.ny, B.nx)), np.zeros((2, B.ny, B.nx))
    res = {}
    for key, (net, src, snk) in (('+12V supply J1 -> U2 VM', MOTOR['supply']), ('GND return U2 -> J1', MOTOR['ret'])):
        ix, iy, r = solve_path(B, net, src, snk)
        d = bz_sheet(B, ix, iy, hall); parts[key] = d.sum() * 1e6; dens += d; K += sheet_density(ix, iy); res[key] = r
    d_leads = np.zeros_like(dens)
    for net, src, snk in MOTOR['leads']:
        ix, iy, _ = solve_path(B, net, src, snk)
        d_leads += bz_sheet(B, ix, iy, hall); K += sheet_density(ix, iy)
    parts['motor leads U2 -> J2'] = d_leads.sum() * 1e6; dens += d_leads
    (ra, na), (rb, nb) = MOTOR['cable']
    xa, ya = B.pad_xy(ra, na); xb, yb = B.pad_xy(rb, nb)
    parts['motor cable (2 wires, 2 mm above, off the top edge)'] = 1e6 * (
        bz_line([(xa, ya, 2.0), (xa, ya - 200, 2.0)], hall) + bz_line([(xb, yb - 200, 2.0), (xb, yb, 2.0)], hall))
    net_motor = sum(parts.values())
    lines.append('MOTOR LOOP, per amp of motor current (B_z at the Hall plate)')
    lines += [f'  {k:52} {v:+7.2f} uT/A' for k, v in parts.items()]
    lines.append(f'  {"NET":52} {net_motor:+7.2f} uT/A   (sign flips with the motor direction)')
    lines.append(f'  path resistance: +12V {res["+12V supply J1 -> U2 VM"] * 1e3:.1f} mOhm, GND return {res["GND return U2 -> J1"] * 1e3:.1f} mOhm')
    shown = K > 0
    outside = ~np.stack([B.pad_mask(B.pads([('J1', None), ('U2', None)]), L) for L in B.layers])
    kmax = np.where(outside, K, 0)
    peak = np.unravel_index(np.argmax(kmax), kmax.shape)
    lines.append(f'  peak sheet current outside J1/U2 pads: {kmax[peak]:.2f} A/mm per A at '
                 f'({B.xs[peak[2]]:.2f}, {B.ys[peak[1]]:.2f}) on {"F.Cu" if peak[0] == 0 else "B.Cu"}')
    lines.append('')
    km = robust_max(K, 98)
    dm = robust_max(dens, 99.5) / (GRID * GRID)
    svg_figure(B, f'{prefix}_current_motor.svg', f'Motor current, 1 A: sheet current and field at {SENSOR} (net {net_motor:+.1f} uT/A)',
               [('F.Cu |K| (A/mm per A)', K[0], 'heat', 0, km, 'A/mm', shown[0], any_cu[0]),
                ('B.Cu |K| (A/mm per A)', K[1], 'heat', 0, km, 'A/mm', shown[1], any_cu[1]),
                (f'B_z contribution at {SENSOR}, both layers', dens * 1e6 / (GRID * GRID), 'div', -dm * 1e6, dm * 1e6,
                 'uT/A per mm2', B.board, None)],
               [f'{k}: {v:+.2f} uT/A' for k, v in parts.items()] + [f'net {net_motor:+.2f} uT/A at {SENSOR}; grey = other copper'],
               labels)

    # ---- ESP 5 V supply loop
    parts5, dens5, K5 = {}, np.zeros((B.ny, B.nx)), np.zeros((2, B.ny, B.nx))
    for net, src, snk in SUPPLY:
        ix, iy, _ = solve_path(B, net, src, snk)
        d = bz_sheet(B, ix, iy, hall); parts5[f'{net} copper'] = d.sum() * 1e6; dens5 += d; K5 += sheet_density(ix, iy)
    bodies = sum(bz_line([(*B.pad_xy(*a), z), (*B.pad_xy(*b), z)], hall) for a, b, z in SUPPLY_BODIES) * 1e6
    parts5['L1 / D1 / U4 / module bodies (line currents)'] = bodies
    net5 = sum(parts5.values())
    lines.append('ESP 5 V SUPPLY LOOP, per amp of ESP current')
    lines += [f'  {k:52} {v:+7.2f} uT/A' for k, v in parts5.items()]
    lines.append(f'  {"NET":52} {net5:+7.2f} uT/A   (a near-cancellation of large terms: model-sensitive)')
    lines.append('')
    km5 = robust_max(K5, 98); dm5 = robust_max(dens5, 99.5) / (GRID * GRID)
    svg_figure(B, f'{prefix}_current_5V.svg', f'ESP 5 V supply, 1 A: sheet current and field at {SENSOR} (net {net5:+.1f} uT/A)',
               [('F.Cu |K| (A/mm per A)', K5[0], 'heat', 0, km5, 'A/mm', K5[0] > 0, any_cu[0]),
                ('B.Cu |K| (A/mm per A)', K5[1], 'heat', 0, km5, 'A/mm', K5[1] > 0, any_cu[1]),
                (f'B_z contribution at {SENSOR}, both layers', dens5 * 1e6 / (GRID * GRID), 'div', -dm5 * 1e6, dm5 * 1e6,
                 'uT/A per mm2', B.board, None)],
               [f'{k}: {v:+.2f} uT/A' for k, v in parts5.items()] + [f'net {net5:+.2f} uT/A at {SENSOR}'], labels)

    # ---- heat
    lines.append('THERMAL, steady state, still air, no frame contact (upper bound)')
    ep = np.stack([B.pad_mask(B.pads([HEAT_U2[0][:2]]), L) for L in B.layers])
    sj, si = B.cell(sx, sy)
    maps = {}
    for name, (ht, hb) in (('convection only', H_CONV), ('convection + radiation', H_RAD)):
        T = thermal(B, HEAT_U2, ht, hb)
        under = T[1][ep[1]].mean()
        lines.append(f'  1 W in U2, {name:23}: board under U2 {under:5.1f} K, U2 junction {under + RJC_U2:5.1f} K/W, '
                     f'{SENSOR} {T[1, sj, si]:5.1f} K, board average {T[:, B.board].mean():5.1f} K')
        maps[name] = T
    Tb = thermal(B, HEAT_BUCK, *H_RAD)
    lines.append(f'  buck + D1 losses ({", ".join(f"{r} {w} W" for r, _, w in HEAT_BUCK)}), with radiation: '
                 f'{SENSOR} {Tb[1, sj, si]:.1f} K, hottest {Tb.max():.1f} K')
    T = maps['convection + radiation']
    svg_figure(B, f'{prefix}_thermal.svg', f'Temperature rise, still air, convection + radiation ({SENSOR} marked)',
               [('bottom side, 1 W in U2 (K)', T[1], 'heat', 0, float(T[1][B.board].max()), 'K per W', B.board, None),
                ('top side, 1 W in U2 (K)', T[0], 'heat', 0, float(T[1][B.board].max()), 'K per W', B.board, None),
                ('bottom side, buck + D1 losses 0.13 W (K)', Tb[1], 'heat', 0, float(Tb[1][B.board].max()), 'K', B.board, None)],
               [f'1 W in U2: U2 junction {T[1][ep[1]].mean() + RJC_U2:.0f} K above ambient, {SENSOR} +{T[1, sj, si]:.1f} K, '
                f'board average +{T[:, B.board].mean():.0f} K', f'buck + D1 (0.13 W): {SENSOR} +{Tb[1, sj, si]:.1f} K',
                'Model: copper + FR4 + via barrels, h = 18/24 W/m2K top/bottom, no heat path into the frame (an upper bound).'],
               labels)
    open(f'{prefix}_analysis.txt', 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main(*sys.argv[1:3])
