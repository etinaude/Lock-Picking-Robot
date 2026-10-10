// Helpers shared by the calibration modes

export function loadState<T>(key: string): Partial<T> | null {
	try {
		return JSON.parse(localStorage.getItem(key) ?? 'null');
	} catch {
		return null; // storage blocked or corrupt; start fresh
	}
}

export function saveState(key: string, value: unknown) {
	try {
		localStorage.setItem(key, JSON.stringify(value));
	} catch {
		// storage blocked; nothing to keep
	}
}

export function parseProbe(value: string | undefined): number | null {
	const reading = parseFloat(value ?? '');
	return Number.isFinite(reading) ? reading : null;
}

// Pasting a column of readings into one cell fills it and the rows below.
// Returns the new probe list, or null to let a single value paste normally.
export function pasteColumn(
	event: ClipboardEvent,
	probes: string[],
	index: number,
	rowCount: number
): string[] | null {
	const values = (event.clipboardData?.getData('text') ?? '')
		.split(/[\r\n\t]+/)
		.map((value) => value.trim())
		.filter(Boolean);
	if (values.length < 2) return null;

	event.preventDefault();
	const next = [...probes];
	values.forEach((value, offset) => {
		if (index + offset < rowCount) next[index + offset] = value;
	});
	return next;
}

// Enter / arrow down moves to the next probe cell, arrow up to the previous
export function moveBetweenProbes(event: KeyboardEvent) {
	const step =
		event.key === 'Enter' || event.key === 'ArrowDown' ? 1 : event.key === 'ArrowUp' ? -1 : 0;
	if (step === 0) return;

	const input = event.currentTarget as HTMLInputElement;
	const inputs = [
		...(input.closest('table')?.querySelectorAll<HTMLInputElement>('input.probe') ?? [])
	];
	const next = inputs[inputs.indexOf(input) + step];
	if (!next) return;

	event.preventDefault();
	next.focus();
	next.select();
}

export function summarise(values: number[]) {
	if (values.length === 0) return null;
	const mean = values.reduce((sum, value) => sum + value, 0) / values.length;
	const variance = values.reduce((sum, value) => sum + (value - mean) ** 2, 0) / values.length;
	const min = Math.min(...values);
	const max = Math.max(...values);
	const sampleVariance =
		values.length > 1
			? values.reduce((sum, value) => sum + (value - mean) ** 2, 0) / (values.length - 1)
			: 0;

	// Spread of the error size, as sample statistics (n - 1) like Excel's
	// AVERAGE / STDEV / VAR over an abs-error column
	const abs = values.map(Math.abs);
	const meanAbs = abs.reduce((sum, value) => sum + value, 0) / abs.length;
	const absVariance =
		abs.length > 1
			? abs.reduce((sum, value) => sum + (value - meanAbs) ** 2, 0) / (abs.length - 1)
			: 0;

	return {
		n: values.length,
		mean,
		std: Math.sqrt(variance),
		sampleStd: Math.sqrt(sampleVariance),
		sampleVariance,
		min,
		max,
		range: max - min,
		maxAbs: Math.max(...abs),
		rms: Math.sqrt(values.reduce((sum, value) => sum + value * value, 0) / values.length),
		meanAbs,
		absStd: Math.sqrt(absVariance),
		absVariance
	};
}

export type Stats = NonNullable<ReturnType<typeof summarise>>;

// Rows of the error tables, shared so every mode reports error the same way
// Rows with an error size (`value`) are coloured like the per-reading errors
export const ERROR_STATS: {
	label: string;
	format: (stats: Stats) => string;
	value?: (stats: Stats) => number;
}[] = [
	{
		label: 'Average',
		format: (stats) => stats.meanAbs.toFixed(4),
		value: (stats) => stats.meanAbs
	},
	{ label: 'SD', format: (stats) => stats.absStd.toFixed(4) },
	{ label: 'Max', format: (stats) => stats.maxAbs.toFixed(4), value: (stats) => stats.maxAbs },
	{ label: 'RMS', format: (stats) => stats.rms.toFixed(4), value: (stats) => stats.rms }
];

// Colour class for an ERROR_STATS cell, or none for rows that aren't an error size
export function statClass(stat: (typeof ERROR_STATS)[number], stats: Stats | null) {
	return stats && stat.value ? errorClass(stat.value(stats)) : '';
}

// Position error sizes: good up to ERROR_GOOD_MM, bad from ERROR_BAD_MM
export const ERROR_GOOD_MM = 0.05;
export const ERROR_BAD_MM = 0.1;

// Class colouring an error cell; good errors keep the normal text colour
export function errorClass(value: number | null) {
	if (value === null) return '';
	const size = Math.abs(value);
	if (size <= ERROR_GOOD_MM) return '';
	return size < ERROR_BAD_MM ? 'error-fine' : 'error-bad';
}

export const mm = (value: number) => value.toFixed(3);
// Rounded first so a tiny negative prints as +0.000, not -0.000
export const signedMM = (value: number) => {
	const rounded = Number(value.toFixed(3));
	return (rounded >= 0 ? '+' : '') + rounded.toFixed(3);
};
