// Fits a model of the carriage to logged PID runs, then runs the firmware's
// controller on that model to suggest gains that settle faster without
// overshooting.

import {
	stepMetrics,
	summariseRun,
	type PidGains,
	type PidRun,
	type PidSample,
	type PidStep,
	type RunSummary
} from './pid.ts';

// Mirrors firmware/common/config.h and motor.h
const MOTOR_MIN_PWM = 20;
const MAX_PWM = 254; // MOTOR_MAX_PWM
const OUTPUT_LIMIT = 255; // pid.SetOutputLimits
const AVERAGE_COUNT = 3; // DISTANCE_AVERAGE_COUNT
// Mirrors the step test in firmware/calibration/main.cpp
// PID_v1's sample time: it computes on each magnet reading, unless this
// hasn't passed since the last compute
const PID_SAMPLE_MS = 20;
const LOG_PERIOD_MS = 20; // PID_SAMPLE_MS in the step test
const SETTLED_MS = 1000;
const STEP_TIMEOUT_MS = 6000;
// Used when the runs don't show how often the magnet is read
const DEFAULT_SENSOR_PERIOD_MS = 27;

export interface Plant {
	// Speed (mm/s) per PWM above the deadzone, towards smaller then larger distances
	speedPerPWM: [number, number];
	deadzonePWM: number; // PWM spent overcoming friction before it moves
	timeConstantMs: number; // how quickly the speed follows the PWM
	sensorPeriodMs: number; // how often the magnet is read
}

export interface PlantFit {
	plant: Plant;
	rmsErrorMM: number; // replaying the logged PWM through the model
	steps: number;
}

function median(values: number[]) {
	const sorted = [...values].sort((a, b) => a - b);
	const middle = sorted.length >> 1;
	return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

// The carriage and magnet: the speed follows the PWM with a lag, and the PID
// sees the rolling median of periodic magnet reads
class Rig {
	positionMM: number;
	measuredMM: number;
	#plant: Plant;
	#speed = 0; // mm/ms
	#alpha: number;
	#reads: number[];
	#readIndex = 0;
	#timeMs = 0;
	#nextReadMs = 0;

	constructor(plant: Plant, startMM: number) {
		this.#plant = plant;
		this.positionMM = startMM;
		this.measuredMM = startMM;
		this.#reads = new Array(AVERAGE_COUNT).fill(startMM);
		this.#alpha = 1 - Math.exp(-1 / plant.timeConstantMs);
	}

	// Advances 1 ms with this signed PWM applied; true if the magnet was read
	tick(pwm: number) {
		const read = this.#timeMs >= this.#nextReadMs;
		if (read) {
			this.#reads[this.#readIndex] = this.positionMM;
			this.#readIndex = (this.#readIndex + 1) % AVERAGE_COUNT;
			this.measuredMM = median(this.#reads);
			this.#nextReadMs += this.#plant.sensorPeriodMs;
		}
		const speedPerPWM = this.#plant.speedPerPWM[pwm > 0 ? 1 : 0];
		const target =
			(Math.sign(pwm) * speedPerPWM * Math.max(0, Math.abs(pwm) - this.#plant.deadzonePWM)) / 1000;
		this.#speed += (target - this.#speed) * this.#alpha;
		this.positionMM += this.#speed;
		this.#timeMs++;
		return read;
	}
}

// Steps that start from rest: the first follows a pause at neutral, the rest
// need the step before them to have settled
function restingSteps(runs: PidRun[]) {
	return runs.flatMap((run) =>
		run.steps.filter(
			(step, i) =>
				(i === 0 || stepMetrics(run.steps[i - 1], run).settleMs !== null) &&
				step.samples.length >= 5 &&
				step.samples.some((sample) => sample.pwm !== 0)
		)
	);
}

// The logged distance only changes when the magnet is read, so while the
// carriage is moving the share of samples that changed gives the read period.
// Driven but stuck (below the friction) doesn't count as moving.
const MOVING_MM = 0.02; // across the two neighbouring samples

function sensorPeriod(steps: PidStep[]) {
	let pairs = 0;
	let changes = 0;
	for (const { samples } of steps) {
		for (let i = 1; i + 1 < samples.length; i++) {
			if (Math.abs(samples[i + 1].distanceMM - samples[i - 1].distanceMM) < MOVING_MM) continue;
			pairs++;
			if (samples[i].distanceMM !== samples[i - 1].distanceMM) changes++;
		}
	}
	if (changes < 20) return DEFAULT_SENSOR_PERIOD_MS;
	return Math.min(Math.max((LOG_PERIOD_MS * pairs) / changes, 10), 100);
}

// Fastest logged speed over 100 ms, for the fit's starting guess (mm/s)
function topSpeed(steps: PidStep[]) {
	let top = 0;
	for (const { samples } of steps) {
		for (let i = 5; i < samples.length; i++) {
			const ms = samples[i].timeMs - samples[i - 5].timeMs;
			if (ms > 0)
				top = Math.max(
					top,
					(Math.abs(samples[i].distanceMM - samples[i - 5].distanceMM) / ms) * 1000
				);
		}
	}
	return top;
}

// Plays a step's logged PWM through the model; returns the squared error
// against the logged distance, summed over its samples
function replayError(plant: Plant, samples: PidSample[]) {
	const rig = new Rig(plant, samples[0].distanceMM);
	const end = samples[samples.length - 1].timeMs;
	let index = 0;
	let squared = 0;
	for (let t = 0; t <= end; t++) {
		const before = index;
		while (index + 1 < samples.length && samples[index + 1].timeMs <= t) index++;
		if (t === 0 || index !== before) squared += (rig.measuredMM - samples[index].distanceMM) ** 2;
		rig.tick(samples[index].pwm);
	}
	return squared;
}

function nelderMead(
	cost: (p: number[]) => number,
	start: number[],
	scale: number[],
	maxEvals = 300
) {
	const n = start.length;
	const simplex = [start, ...scale.map((s, i) => start.map((v, j) => (i === j ? v + s : v)))].map(
		(p) => ({ p, cost: cost(p) })
	);
	let evals = simplex.length;

	while (evals < maxEvals) {
		simplex.sort((a, b) => a.cost - b.cost);
		const best = simplex[0];
		const worst = simplex[n];
		if (Math.abs(worst.cost - best.cost) <= 1e-10 + 1e-6 * Math.abs(best.cost)) break;

		const centroid = start.map(
			(_, j) => simplex.slice(0, n).reduce((sum, v) => sum + v.p[j], 0) / n
		);
		const along = (t: number) => centroid.map((c, j) => c + t * (worst.p[j] - c));
		const tryPoint = (t: number) => {
			const p = along(t);
			evals++;
			return { p, cost: cost(p) };
		};

		const reflected = tryPoint(-1);
		if (reflected.cost < best.cost) {
			const expanded = tryPoint(-2);
			simplex[n] = expanded.cost < reflected.cost ? expanded : reflected;
		} else if (reflected.cost < simplex[n - 1].cost) {
			simplex[n] = reflected;
		} else {
			const contracted = tryPoint(reflected.cost < worst.cost ? -0.5 : 0.5);
			if (contracted.cost < Math.min(reflected.cost, worst.cost)) {
				simplex[n] = contracted;
			} else {
				for (let i = 1; i <= n; i++) {
					const p = simplex[i].p.map((v, j) => best.p[j] + (v - best.p[j]) / 2);
					simplex[i] = { p, cost: cost(p) };
					evals++;
				}
			}
		}
	}
	return simplex.reduce((a, b) => (b.cost < a.cost ? b : a));
}

// Least squares on the logged distance, replaying every resting step's PWM
export function fitPlant(runs: PidRun[]): PlantFit | null {
	const steps = restingSteps(runs);
	if (!steps.length) return null;
	const sampleCount = steps.reduce((sum, step) => sum + step.samples.length, 0);
	const sensorPeriodMs = sensorPeriod(steps);

	// Speeds and the time constant are fitted as logs, so they stay positive
	const toPlant = ([down, up, deadzone, lag]: number[]): Plant => ({
		speedPerPWM: [Math.exp(down), Math.exp(up)],
		deadzonePWM: deadzone,
		timeConstantMs: Math.exp(lag),
		sensorPeriodMs
	});
	const cost = (p: number[]) => {
		if (p[2] < 0 || p[2] > 240 || p[3] < Math.log(2) || p[3] > Math.log(2000)) return Infinity;
		const plant = toPlant(p);
		return steps.reduce((sum, step) => sum + replayError(plant, step.samples), 0) / sampleCount;
	};

	// Friction trades off against speed, so start from a few deadzones
	const speed = Math.log(Math.max(topSpeed(steps), 0.5) / (MAX_PWM - 50));
	const best = [10, 50, 90]
		.map((deadzone) =>
			nelderMead(cost, [speed, speed, deadzone, Math.log(60)], [0.4, 0.4, 25, 0.7])
		)
		.reduce((a, b) => (b.cost < a.cost ? b : a));

	return { plant: toPlant(best.p), rmsErrorMM: Math.sqrt(best.cost), steps: steps.length };
}

export interface TestPlan {
	startMM: number;
	targets: number[];
	deadbandMM: number;
	travelMM: [number, number];
	graceMM: number;
}

export function planFrom(run: PidRun): TestPlan {
	return {
		startMM: run.steps[0]?.samples[0]?.distanceMM ?? 6,
		targets: run.steps.map((step) => step.targetMM),
		deadbandMM: run.deadbandMM,
		travelMM: run.travelMM,
		graceMM: run.graceMM
	};
}

const clamp = (value: number, low: number, high: number) => Math.min(Math.max(value, low), high);

// Motor.drive
const drivePWM = (output: number) =>
	output === 0 ? 0 : Math.sign(output) * Math.min(Math.abs(output) + MOTOR_MIN_PWM, MAX_PWM);

// The firmware's step test on the model: Motor.update on each magnet reading,
// running PID_v1 (derivative on measurement, reset on each new target and
// inside the deadband) and braking rather than driving away from the target
export function simulateRun(plant: Plant, gains: PidGains, plan: TestPlan): PidRun {
	const rig = new Rig(plant, plan.startMM);
	const seconds = PID_SAMPLE_MS / 1000;
	const ki = gains.ki * seconds; // PID_v1 folds the sample time into the gains
	const kd = gains.kd / seconds;
	const steps: PidStep[] = [];
	let pwm = 0;
	let clockMs = 0;
	let lastComputeMs = -Infinity;

	plan.targets.forEach((target, i) => {
		const setpoint = clamp(target, plan.travelMM[0], plan.travelMM[1]);
		let output = 0;
		let outputSum = 0;
		let lastInput = rig.measuredMM;
		let settledSince: number | null = null;
		const samples: PidSample[] = [];

		for (let t = 0; ; t++) {
			if (t % LOG_PERIOD_MS === 0) {
				const distanceMM = rig.measuredMM;
				samples.push({ timeMs: t, distanceMM, pwm, currentMA: 0 });
				if (Math.abs(distanceMM - target) <= plan.deadbandMM) {
					settledSince ??= t;
					if (t - settledSince >= SETTLED_MS) break;
				} else {
					settledSince = null;
				}
				if (t >= STEP_TIMEOUT_MS) break;
			}

			if (rig.tick(pwm)) {
				const input = rig.measuredMM;
				if (Math.abs(input - setpoint) <= plan.deadbandMM) {
					output = 0;
					outputSum = 0;
					lastInput = input;
					pwm = 0;
				} else {
					if (clockMs - lastComputeMs >= PID_SAMPLE_MS) {
						const error = setpoint - input;
						outputSum = clamp(outputSum + ki * error, -OUTPUT_LIMIT, OUTPUT_LIMIT);
						output = clamp(
							gains.kp * error + outputSum - kd * (input - lastInput),
							-OUTPUT_LIMIT,
							OUTPUT_LIMIT
						);
						lastInput = input;
						lastComputeMs = clockMs;
					}
					pwm = output > 0 !== setpoint > input ? 0 : drivePWM(output);
				}
			}
			clockMs++;
		}
		steps.push({ number: i + 1, targetMM: target, samples });
	});

	return { ...plan, gains, steps, temperatureC: null };
}

export interface Suggestion {
	gains: PidGains;
	predicted: RunSummary; // on the fitted model
	worstOvershootMM: number; // across the model and its faster / laggier variants
	simulated: PidRun;
	baseline: RunSummary; // the starting gains on the fitted model
}

// Overshoot inside half the deadband is free; past that it costs a lot of
// settle time, so the search only trades it away for a big speed-up
const FREE_OVERSHOOT_FRACTION = 0.5;
const OVERSHOOT_COST_MS_PER_MM = 300_000;
const CROSSING_COST_MS = 1000;
const HIT_STOP_COST_MS = 1_000_000;

// Grid of starting points, then a pattern search from the best
const KP_GRID = [0.5, 1, 2, 3, 5, 8, 12, 18, 27, 40, 60, 90];
const KI_GRID = [0, 0.03, 0.1, 0.3, 1];
const KD_GRID = [0, 0.1, 0.3, 1, 3, 10];
// Below these a gain is dropped to 0 (Kp is kept)
const SMALLEST: PidGains = { kp: 0.1, ki: 0.005, kd: 0.02 };

const roundGain = (value: number) => +value.toPrecision(3);

export function suggestGains(plant: Plant, plan: TestPlan, start: PidGains): Suggestion {
	// The model is only approximate, so gains must also work if the carriage is
	// faster, or slower and laggier, than fitted
	const plants: Plant[] = [
		plant,
		{ ...plant, speedPerPWM: [plant.speedPerPWM[0] * 1.25, plant.speedPerPWM[1] * 1.25] },
		{
			...plant,
			speedPerPWM: [plant.speedPerPWM[0] * 0.85, plant.speedPerPWM[1] * 0.85],
			timeConstantMs: plant.timeConstantMs * 1.5
		}
	];
	const freeOvershootMM = plan.deadbandMM * FREE_OVERSHOOT_FRACTION;

	const evaluate = (gains: PidGains) => {
		let cost = 0;
		let worstOvershootMM = 0;
		for (const variant of plants) {
			const run = simulateRun(variant, gains, plan);
			const metrics = run.steps.map((step) => stepMetrics(step, run));
			const summary = summariseRun(metrics);
			worstOvershootMM = Math.max(worstOvershootMM, summary.maxOvershootMM);
			const runCost =
				metrics.reduce((sum, step) => sum + (step.settleMs ?? STEP_TIMEOUT_MS * 3), 0) +
				Math.max(0, summary.maxOvershootMM - freeOvershootMM) * OVERSHOOT_COST_MS_PER_MM +
				metrics.reduce((sum, step) => sum + step.crossings, 0) * CROSSING_COST_MS +
				(summary.maxPastLimitMM >= plan.graceMM ? HIT_STOP_COST_MS : 0);
			cost = Math.max(cost, runCost);
		}
		return { gains, cost, worstOvershootMM };
	};

	let best = evaluate(start);
	for (const kp of KP_GRID)
		for (const ki of KI_GRID)
			for (const kd of KD_GRID) {
				const candidate = evaluate({ kp, ki, kd });
				if (candidate.cost < best.cost) best = candidate;
			}

	const keys = ['kp', 'ki', 'kd'] as const;
	for (let factor = 2; factor > 1.05;) {
		let improved = false;
		for (const key of keys) {
			for (const scale of [factor, 1 / factor]) {
				const value = best.gains[key];
				let next = value === 0 ? (scale > 1 ? SMALLEST[key] * 2 : 0) : value * scale;
				if (next < SMALLEST[key]) next = key === 'kp' ? SMALLEST.kp : 0;
				if (next === value) continue;
				const candidate = evaluate({ ...best.gains, [key]: next });
				if (candidate.cost < best.cost) {
					best = candidate;
					improved = true;
				}
			}
		}
		if (!improved) factor = Math.sqrt(factor);
	}

	const gains = {
		kp: roundGain(best.gains.kp),
		ki: roundGain(best.gains.ki),
		kd: roundGain(best.gains.kd)
	};
	const simulated = simulateRun(plant, gains, plan);
	const baselineRun = simulateRun(plant, start, plan);
	return {
		gains,
		predicted: summariseRun(simulated.steps.map((step) => stepMetrics(step, simulated))),
		worstOvershootMM: evaluate(gains).worstOvershootMM,
		simulated,
		baseline: summariseRun(baselineRun.steps.map((step) => stepMetrics(step, baselineRun)))
	};
}
