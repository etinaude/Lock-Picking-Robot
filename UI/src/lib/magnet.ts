// Mirrors the magnet model in firmware/common/magnet.h so the UI predicts exactly
// what the firmware will compute.

export const MAGNET_TEMPCO_PER_C = -0.0012;

export interface MagnetParams {
	remanenceMT: number;
	diameterMM: number;
	thicknessMM: number;
	zOffsetMM: number;
	calibrationTempC: number;
}

// Starting point for every magnet: datasheet N52 (Br 1.43-1.48 T) in the 4 x 2 mm
// size. Suppliers differ, so each magnet is fitted from here rather than from the
// last one's calibration. The z offset is the sensor-to-magnet gap at 0 mm from
// the original design, not a property of the magnet.
export const NOMINAL_N52: MagnetParams = {
	remanenceMT: 1450.0,
	diameterMM: 4.0,
	thicknessMM: 2.0,
	zOffsetMM: 3.5,
	calibrationTempC: 25.0
};

export interface CalibrationRow {
	setMM: number;
	bzMT: number;
	predictedMM: number;
	timeMs: number | null; // repeatability tables only; null on timeout
}

export interface ParsedCalibration {
	rows: CalibrationRow[];
	temperatureC: number | null;
	hasTime: boolean; // repeatability table, with a time-to-target column
}

// On-axis field of a cylindrical magnet at distance z (mm) from its face
export function fieldAt(z: number, remanenceMT: number, radiusMM: number, thicknessMM: number) {
	const z1 = z + thicknessMM;
	const r2 = radiusMM * radiusMM;
	return (remanenceMT / 2) * (z1 / Math.sqrt(z1 * z1 + r2) - z / Math.sqrt(z * z + r2));
}

// Field at a distance from the magnet face, including the temperature drift
export function fieldAtFace(faceMM: number, p: MagnetParams, tempC: number | null) {
	const scale = tempC === null ? 1 : 1 + MAGNET_TEMPCO_PER_C * (tempC - p.calibrationTempC);
	return fieldAt(faceMM, p.remanenceMT, p.diameterMM / 2, p.thicknessMM) * scale;
}

// Field the sensor sees at a carriage position
export function fieldAtMotion(motionMM: number, p: MagnetParams, tempC: number | null) {
	return fieldAtFace(motionMM + p.zOffsetMM, p, tempC);
}

// Inverse of fieldAt. The field falls monotonically with distance, so bisection
// always converges (the firmware's Newton loop lands on the same root).
export function solveDistance(bzMT: number, p: MagnetParams) {
	let near = 0.001;
	let far = 500;
	for (let i = 0; i < 60; i++) {
		const mid = (near + far) / 2;
		if (fieldAt(mid, p.remanenceMT, p.diameterMM / 2, p.thicknessMM) > bzMT) near = mid;
		else far = mid;
	}
	return (near + far) / 2;
}

// Carriage position the firmware would report for a raw reading
export function predictMotion(bzMT: number, p: MagnetParams, tempC: number | null) {
	const scale = tempC === null ? 1 : 1 + MAGNET_TEMPCO_PER_C * (tempC - p.calibrationTempC);
	return solveDistance(bzMT / scale, p) - p.zOffsetMM;
}

// Accepts the tables printed by the calibration firmware (M or R), with any
// step size. Any line whose first three fields are numbers is a row.
export function parseCalibration(text: string): ParsedCalibration {
	const rows: CalibrationRow[] = [];
	let temperatureC: number | null = null;
	let hasTime = false;

	for (const line of text.split(/\r?\n/)) {
		const temperature = line.match(/Temperature:\s*(-?\d+(?:\.\d+)?)/i);
		if (temperature) {
			temperatureC = Number(temperature[1]);
			continue;
		}

		const fields = line.trim().split(/\s+/);
		if (fields.length < 3) continue;
		const [setMM, bzMT, predictedMM] = fields.slice(0, 3).map(Number);
		if (![setMM, bzMT, predictedMM].every(Number.isFinite)) continue;

		if (fields.length > 3) hasTime = true;
		const time = fields.length > 3 ? Number(fields[3]) : NaN;
		rows.push({ setMM, bzMT, predictedMM, timeMs: Number.isFinite(time) ? time : null });
	}
	return { rows, temperatureC, hasTime };
}

export interface FitPoint {
	motionMM: number; // true position from the probe
	bzMT: number;
}

export interface FitResult {
	remanenceMT: number;
	zOffsetMM: number;
}

// Least squares (Levenberg-Marquardt) on effective remanence and z offset,
// minimising the relative field error so near and far points weigh equally.
export function fitCalibration(points: FitPoint[], start: MagnetParams): FitResult | null {
	if (points.length < 2) return null;
	const radius = start.diameterMM / 2;
	const minMotion = Math.min(...points.map((point) => point.motionMM));

	const residuals = ([remanence, zOffset]: number[]) => {
		if (remanence <= 0 || zOffset + minMotion <= 0) return null;
		return points.map(
			(point) =>
				(fieldAt(point.motionMM + zOffset, remanence, radius, start.thicknessMM) - point.bzMT) /
				point.bzMT
		);
	};
	const cost = (r: number[]) => r.reduce((sum, value) => sum + value * value, 0);

	let params = [start.remanenceMT, start.zOffsetMM];
	let r = residuals(params);
	if (!r) return null;
	let currentCost = cost(r);
	let lambda = 1e-3;

	for (let iteration = 0; iteration < 200; iteration++) {
		// Numerical Jacobian
		const jacobian = params.map((value, j) => {
			const step = 1e-6 * Math.max(Math.abs(value), 1);
			const shifted = [...params];
			shifted[j] += step;
			const rShifted = residuals(shifted)!;
			return rShifted.map((value2, i) => (value2 - r![i]) / step);
		});

		// Normal equations for the two parameters
		const a = [0, 1].map((j) =>
			[0, 1].map((k) => jacobian[j].reduce((sum, value, i) => sum + value * jacobian[k][i], 0))
		);
		const g = [0, 1].map((j) => jacobian[j].reduce((sum, value, i) => sum + value * r![i], 0));

		let improved = false;
		while (lambda < 1e12) {
			const a00 = a[0][0] * (1 + lambda);
			const a11 = a[1][1] * (1 + lambda);
			const det = a00 * a11 - a[0][1] * a[1][0];
			const step = [(-g[0] * a11 + g[1] * a[0][1]) / det, (-g[1] * a00 + g[0] * a[1][0]) / det];
			const candidate = [params[0] + step[0], params[1] + step[1]];
			const rCandidate = residuals(candidate);

			if (rCandidate && cost(rCandidate) < currentCost) {
				const gain = currentCost - cost(rCandidate);
				params = candidate;
				r = rCandidate;
				currentCost = cost(rCandidate);
				lambda /= 10;
				improved = gain > 1e-15;
				break;
			}
			lambda *= 10;
		}
		if (!improved) break;
	}

	return { remanenceMT: params[0], zOffsetMM: params[1] };
}

export interface Regression {
	coefficients: number[]; // c0 + c1·ln(B) + c2·ln(B)² + ...
	minBzMT: number; // fitted field range; the polynomial is unreliable outside it
	maxBzMT: number;
}

// Empirical alternative to the physical model: carriage position as a polynomial
// in ln(B_z), fitted by ordinary least squares on position, so it minimises the
// RMS position error directly. Uses raw readings, so it holds at the run temperature.
export function fitRegression(points: FitPoint[], maxDegree = 3): Regression | null {
	const degree = Math.min(maxDegree, points.length - 1);
	if (degree < 1) return null;
	const terms = degree + 1;

	// Normal equations (XᵀX)c = Xᵀy, solved by Gaussian elimination
	const a = Array.from({ length: terms }, () => new Array<number>(terms + 1).fill(0));
	for (const point of points) {
		const u = Math.log(point.bzMT);
		const powers = Array.from({ length: terms }, (_, j) => u ** j);
		for (let j = 0; j < terms; j++) {
			for (let k = 0; k < terms; k++) a[j][k] += powers[j] * powers[k];
			a[j][terms] += powers[j] * point.motionMM;
		}
	}

	for (let col = 0; col < terms; col++) {
		let pivot = col;
		for (let row = col + 1; row < terms; row++)
			if (Math.abs(a[row][col]) > Math.abs(a[pivot][col])) pivot = row;
		[a[col], a[pivot]] = [a[pivot], a[col]];
		if (Math.abs(a[col][col]) < 1e-12) return null; // repeated readings, nothing to fit
		for (let row = 0; row < terms; row++) {
			if (row === col) continue;
			const factor = a[row][col] / a[col][col];
			for (let k = col; k <= terms; k++) a[row][k] -= factor * a[col][k];
		}
	}

	const fields = points.map((point) => point.bzMT);
	return {
		coefficients: a.map((row, j) => row[terms] / row[j]),
		minBzMT: Math.min(...fields),
		maxBzMT: Math.max(...fields)
	};
}

export function predictRegression(bzMT: number, regression: Regression) {
	const u = Math.log(bzMT);
	return regression.coefficients.reduce((sum, c, j) => sum + c * u ** j, 0);
}
