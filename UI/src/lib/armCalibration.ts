// Each arm board's calibration, as in the ARM_CALIBRATIONS table in
// firmware/common/defaults.h. The firmware prints the row it's using as a
// "Calibration row:" line at boot and after every calibration run.

export interface ArmCalibration {
	mac: string;
	id: number;
	minPWM: number;
	kp: number;
	ki: number;
	kd: number;
	remanenceMT: number;
	zOffsetMM: number;
	calibrationTempC: number;
}

// DEFAULT_CALIBRATION in defaults.h
export const DEFAULT_CALIBRATION: ArmCalibration = {
	mac: '',
	id: 0,
	minPWM: 30,
	kp: 250,
	ki: 0,
	kd: 8,
	remanenceMT: 921.7,
	zOffsetMM: 2.324,
	calibrationTempC: 31.04
};

const NUMBER = /^-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?f?$/;

// e.g. {"28:84:85:67:44:74", 1, 30.0f, 250.0000, 0.0000, 8.0000, 921.7f, 2.324f, 31.04f},
export function parseCalibrationRow(text: string): ArmCalibration | null {
	const body = text.match(/\{(.*)\}/)?.[1];
	if (!body) return null;
	const [mac = '', ...fields] = body.split(',').map((field) => field.trim());
	if (!/^"[^"]*"$/.test(mac) || fields.length !== 8 || !fields.every((f) => NUMBER.test(f)))
		return null;

	const [id, minPWM, kp, ki, kd, remanenceMT, zOffsetMM, calibrationTempC] = fields.map((field) =>
		parseFloat(field)
	) as [number, number, number, number, number, number, number, number];
	return {
		mac: mac.slice(1, -1),
		id,
		minPWM,
		kp,
		ki,
		kd,
		remanenceMT,
		zOffsetMM,
		calibrationTempC
	};
}

// The values the firmware's JSON command takes, e.g. {"kp": 250, "kd": 8}, with
// the limits setParameter in firmware/arm/calibration.h checks. The board
// keeps them until it resets.
export type ParameterKey = Exclude<keyof ArmCalibration, 'mac' | 'id'>;

const nonNegative = (value: number) => (value < 0 ? 'Must be 0 or more' : null);

export const PARAMETERS: {
	key: ParameterKey;
	label: string;
	check?: (value: number) => string | null;
}[] = [
	{
		key: 'minPWM',
		label: 'Min PWM',
		check: (value) => (value < 0 || value > 254 ? 'Must be 0-254' : null)
	},
	// PID_v1 ignores all three gains if any is negative
	{ key: 'kp', label: 'Kp', check: nonNegative },
	{ key: 'ki', label: 'Ki', check: nonNegative },
	{ key: 'kd', label: 'Kd', check: nonNegative },
	{
		key: 'remanenceMT',
		label: 'Remanence (mT)',
		check: (value) => (value <= 0 ? 'Must be more than 0' : null)
	},
	{ key: 'zOffsetMM', label: 'Z offset (mm)' },
	{ key: 'calibrationTempC', label: 'Calibration temp (°C)' }
];

export type Parameters = Record<ParameterKey, number | null>;

export function pickParameters(calibration: ArmCalibration): Parameters {
	return Object.fromEntries(PARAMETERS.map(({ key }) => [key, calibration[key]])) as Parameters;
}

// What's wrong with a value, or null if the firmware will take it
export function parameterProblem(parameter: (typeof PARAMETERS)[number], value: number | null) {
	if (value === null || !Number.isFinite(value)) return 'Needs a number';
	return parameter.check?.(value) ?? null;
}

// The row the board reported, or the defaults under its MAC if it hasn't
export function boardCalibration(
	reported: ArmCalibration | null,
	mac = 'MAC ADDRESS'
): ArmCalibration {
	return reported ?? { ...DEFAULT_CALIBRATION, mac };
}

const pad = (text: string, width: number) =>
	text.length < width ? text.padEnd(width) : `${text} `;

// Fixed digits with the trailing zeros trimmed, keeping one: 250.0, 0.0234
const decimal = (value: number, digits: number) =>
	value
		.toFixed(digits)
		.replace(/(\.\d*?)0+$/, '$1')
		.replace(/\.$/, '.0');

// The table's header comment and a row, with the column widths used in
// defaults.h so the row lines up when pasted in
export function calibrationRow(calibration: ArmCalibration) {
	const columns: [label: string, width: number, value: string][] = [
		['MAC', 21, `"${calibration.mac}"`],
		['ID', 4, String(calibration.id)],
		['Min PWM', 9, `${decimal(calibration.minPWM, 1)}f`],
		['Kp', 9, decimal(calibration.kp, 4)],
		['Ki', 9, decimal(calibration.ki, 4)],
		['Kd', 9, decimal(calibration.kd, 4)],
		['Remanence mT', 14, `${decimal(calibration.remanenceMT, 1)}f`],
		['Z offset mm', 13, `${decimal(calibration.zOffsetMM, 3)}f`],
		['Cal temp C', 0, `${decimal(calibration.calibrationTempC, 2)}f`]
	];
	const last = columns.length - 1;
	const header = columns
		.map(([label, width], i) => (i < last ? pad(label, width) : label))
		.join('');
	const row = columns
		.map(([, width, value], i) => (i < last ? pad(`${value},`, width) : `${value}},`))
		.join('');
	return `//  ${header}\n   {${row}`;
}
