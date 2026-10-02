// Parses the calibration firmware's PID step test (command P) and measures
// each step's response.

export interface PidGains {
	kp: number;
	ki: number;
	kd: number;
}

// DEFAULT_KP / KI / KD in firmware/common/motor.h
export const DEFAULT_GAINS: PidGains = { kp: 3.0, ki: 0.1, kd: 1.0 };
// DISTANCE_DEADBAND and TRAVEL_MIN_MM / MAX_MM in firmware/common/config.h,
// for output without them in its header
const DEFAULT_DEADBAND_MM = 0.05;
const DEFAULT_TRAVEL_MM: [number, number] = [3.0, 10.0];
// The mechanical stops are about this far past the travel. Older firmware
// printed it as "Grace" in the header.
const DEFAULT_GRACE_MM = 0.5;
// The firmware logs every PID_SAMPLE_MS
export const SAMPLE_MS = 20;
// A step counts as settled once it stays in the deadband this long to the end
// of its log. The firmware ends a step after a second there, or at its timeout.
const MIN_SETTLED_MS = 500;
// The final error is averaged over the end of each step
const FINAL_WINDOW_MS = 500;

export interface PidSample {
	timeMs: number; // since the step's target was set
	distanceMM: number; // the rolling average the PID runs on
	pwm: number; // signed, positive drives towards larger distances
	currentMA: number;
}

export interface PidStep {
	number: number;
	targetMM: number;
	samples: PidSample[];
}

export interface PidRun {
	gains: PidGains | null;
	deadbandMM: number;
	travelMM: [number, number]; // targets are clamped to this
	graceMM: number; // room past the travel before the mechanical stops
	steps: PidStep[];
	temperatureC: number | null;
}

export interface StepMetrics {
	fromMM: number;
	toMM: number;
	reachMs: number | null; // first time in the deadband
	riseMs: number | null; // 10% to 90% of the move
	settleMs: number | null; // stayed in the deadband from here; null if it never did
	overshootMM: number; // furthest past the target, 0 if it never passed it
	finalErrorMM: number; // distance minus target at the end of the step
	crossings: number; // swings from one side of the deadband to the other
	pastLimitMM: number; // furthest beyond the travel, 0 if it stayed inside
	peakCurrentMA: number;
}

export interface RunSummary {
	totalSettleMs: number | null; // null if any step didn't settle
	timeouts: number;
	maxOvershootMM: number;
	maxFinalErrorMM: number; // largest size of the final error
	maxPastLimitMM: number;
}

const HEADER =
	/PID step test\s+Kp\s+(\S+)\s+Ki\s+(\S+)\s+Kd\s+(\S+)(?:\s+Deadband\s+(\S+)\s+mm)?(?:\s+Travel\s+([\d.]+)-([\d.]+)\s+mm)?(?:\s+Grace\s+(\S+))?/i;

// Rows are "step target time distance pwm current"
export function parsePidRun(text: string): PidRun {
	let gains: PidGains | null = null;
	let deadbandMM = DEFAULT_DEADBAND_MM;
	let travelMM = DEFAULT_TRAVEL_MM;
	let graceMM = DEFAULT_GRACE_MM;
	let temperatureC: number | null = null;
	const steps: PidStep[] = [];

	for (const line of text.split(/\r?\n/)) {
		const header = line.match(HEADER);
		if (header) {
			const [kp, ki, kd, deadband, travelMin, travelMax, grace] = header.slice(1).map(Number);
			if ([kp, ki, kd].every(Number.isFinite)) gains = { kp, ki, kd };
			if (Number.isFinite(deadband) && deadband > 0) deadbandMM = deadband;
			if (travelMin < travelMax) travelMM = [travelMin, travelMax];
			if (grace >= 0) graceMM = grace;
			continue;
		}

		const temperature = line.match(/Temperature:\s*(-?\d+(?:\.\d+)?)/i);
		if (temperature) {
			temperatureC = Number(temperature[1]);
			continue;
		}

		const fields = line.trim().split(/\s+/).map(Number);
		if (fields.length !== 6 || !fields.every(Number.isFinite)) continue;
		const [number, targetMM, timeMs, distanceMM, pwm, currentMA] = fields;
		if (!Number.isInteger(number)) continue;

		let step = steps.at(-1);
		if (step?.number !== number) {
			step = { number, targetMM, samples: [] };
			steps.push(step);
		}
		step.samples.push({ timeMs, distanceMM, pwm, currentMA });
	}
	return { gains, deadbandMM, travelMM, graceMM, steps, temperatureC };
}

export function stepMetrics(
	step: PidStep,
	run: Pick<PidRun, 'deadbandMM' | 'travelMM'>
): StepMetrics {
	const { deadbandMM, travelMM } = run;
	const { samples, targetMM } = step;
	const fromMM = samples[0].distanceMM;
	const moveMM = targetMM - fromMM;
	const error = (sample: PidSample) => sample.distanceMM - targetMM;
	const inBand = (sample: PidSample) => Math.abs(error(sample)) <= deadbandMM;
	const last = samples[samples.length - 1];

	// Moves inside the deadband have no meaningful rise
	const progressAt = (fraction: number) =>
		samples.find((sample) => (sample.distanceMM - fromMM) / moveMM >= fraction)?.timeMs;
	const rise10 = progressAt(0.1);
	const rise90 = progressAt(0.9);
	const riseMs =
		Math.abs(moveMM) > deadbandMM && rise10 !== undefined && rise90 !== undefined
			? rise90 - rise10
			: null;

	const lastOutside = samples.findLastIndex((sample) => !inBand(sample));
	const settledAt = lastOutside + 1 < samples.length ? samples[lastOutside + 1].timeMs : null;
	const settleMs =
		settledAt !== null && last.timeMs - settledAt >= MIN_SETTLED_MS ? settledAt : null;

	// Past the target in the direction of travel
	const direction = Math.sign(moveMM) || 1;
	const overshootMM = Math.max(0, ...samples.map((sample) => direction * error(sample)));

	const finalSamples = samples.filter((sample) => sample.timeMs >= last.timeMs - FINAL_WINDOW_MS);
	const finalErrorMM =
		finalSamples.reduce((sum, sample) => sum + error(sample), 0) / finalSamples.length;

	// Jitter inside the deadband doesn't count, only passing right through it
	let crossings = 0;
	let side = 0;
	for (const sample of samples) {
		const now = error(sample) > deadbandMM ? 1 : error(sample) < -deadbandMM ? -1 : 0;
		if (now !== 0 && side !== 0 && now !== side) crossings++;
		if (now !== 0) side = now;
	}

	const distances = samples.map((sample) => sample.distanceMM);
	const pastLimitMM = Math.max(
		0,
		travelMM[0] - Math.min(...distances),
		Math.max(...distances) - travelMM[1]
	);

	return {
		fromMM,
		toMM: targetMM,
		reachMs: samples.find(inBand)?.timeMs ?? null,
		riseMs,
		settleMs,
		overshootMM,
		finalErrorMM,
		crossings,
		pastLimitMM,
		peakCurrentMA: Math.max(...samples.map((sample) => sample.currentMA))
	};
}

export function summariseRun(metrics: StepMetrics[]): RunSummary {
	const settled = metrics.flatMap((step) => (step.settleMs === null ? [] : [step.settleMs]));
	return {
		totalSettleMs:
			settled.length === metrics.length ? settled.reduce((sum, ms) => sum + ms, 0) : null,
		timeouts: metrics.length - settled.length,
		maxOvershootMM: Math.max(0, ...metrics.map((step) => step.overshootMM)),
		maxFinalErrorMM: Math.max(0, ...metrics.map((step) => Math.abs(step.finalErrorMM))),
		maxPastLimitMM: Math.max(0, ...metrics.map((step) => step.pastLimitMM))
	};
}

export const formatGain = (value: number) => String(+value.toFixed(4));

// The firmware's PID library ignores negative gains
export const validGains = (gains: PidGains) =>
	[gains.kp, gains.ki, gains.kd].every((value) => Number.isFinite(value) && value >= 0);

export const pidCommand = (gains: PidGains) =>
	`P ${formatGain(gains.kp)} ${formatGain(gains.ki)} ${formatGain(gains.kd)}`;

// Colour for going past the travel: into the grace is a warning, through it
// would have hit the stop
export const pastLimitClass = (pastMM: number, graceMM: number) =>
	pastMM <= 0 ? '' : pastMM < graceMM ? 'error-fine' : 'error-bad';
