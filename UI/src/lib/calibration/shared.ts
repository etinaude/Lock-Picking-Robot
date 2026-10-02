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
	return {
		mean,
		std: Math.sqrt(variance),
		min,
		max,
		range: max - min,
		maxAbs: Math.max(...values.map(Math.abs)),
		rms: Math.sqrt(values.reduce((sum, value) => sum + value * value, 0) / values.length)
	};
}

export const mm = (value: number) => value.toFixed(3);
// Rounded first so a tiny negative prints as +0.000, not -0.000
export const signedMM = (value: number) => {
	const rounded = Number(value.toFixed(3));
	return (rounded >= 0 ? '+' : '') + rounded.toFixed(3);
};
