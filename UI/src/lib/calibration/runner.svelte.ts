// Runs a calibration on the board from the sidebar, then hands the table it
// prints to that mode's tab (even if the user has switched tabs meanwhile).

import { DEFAULT_GAINS, type PidGains } from '#lib/pid.ts';
import { serial } from '#lib/serial/serial.svelte.ts';

export type RunMode = 'pid' | 'magnetometer' | 'repeatability';

const MODE_LABELS: Record<RunMode, string> = {
	pid: 'PID tuning',
	magnetometer: 'Magnetometer',
	repeatability: 'Repeatability'
};

// The firmware prints the table, then the temperature, once it's back at neutral
const TABLE_HEADER: Record<RunMode, RegExp> = {
	pid: /^PID step test/,
	magnetometer: /^Set Distance \(mm\)/,
	repeatability: /^Set Distance \(mm\)/
};
const TABLE_END = /^Temperature:/;

interface Run {
	mode: RunMode;
	command: string;
	points: number; // set points reached so far
	last: string; // e.g. "3.50 mm"
}

class CalibrationRunner {
	running = $state<Run | null>(null);
	message = $state<{ text: string; error: boolean } | null>(null);
	// Finished tables waiting for their tab, which clears them once shown
	results = $state<Record<RunMode, string | null>>({
		pid: null,
		magnetometer: null,
		repeatability: null
	});
	// Set on the PID tab, sent by the sidebar's run button
	pidGains = $state<PidGains>({ ...DEFAULT_GAINS });

	#unsubscribe: (() => void) | null = null;

	run(mode: RunMode, command: string) {
		if (this.running || !serial.connected || !serial.calibrationFirmware) return;
		this.running = { mode, command, points: 0, last: '' };
		this.message = null;

		const lines: string[] = [];
		this.#unsubscribe = serial.onLine((line) => {
			if (line === null) return this.#finish('Connection lost before the table arrived', true);
			lines.push(line);

			// Each move prints "-> 3.50 mm"; the final one back to neutral isn't a point
			if (this.running && line.startsWith('->') && !line.includes('neutral')) {
				this.running.points++;
				this.running.last = line.slice(2).trim();
			}

			if (TABLE_END.test(line)) {
				let start = lines.length - 1;
				while (start >= 0 && !TABLE_HEADER[mode].test(lines[start])) start--;
				if (start < 0) return this.#finish('The run ended without a table', true);

				this.results[mode] = lines.slice(start).join('\n');
				this.#finish(`Table added to the ${MODE_LABELS[mode]} tab`, false);
			}
		});
		serial.send(command);
	}

	// The firmware ignores input mid-run, so this only stops listening
	cancel() {
		this.#finish(
			'Stopped listening. The board finishes the run on its own; reset it to stop sooner.',
			true
		);
	}

	#finish(text: string, error: boolean) {
		this.#unsubscribe?.();
		this.#unsubscribe = null;
		this.running = null;
		this.message = { text, error };
	}
}

export const runner = new CalibrationRunner();
