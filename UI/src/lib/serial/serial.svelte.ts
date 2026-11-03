// USB serial connection to the arm, shared app-wide so switching tabs doesn't
// drop the port or the log.

import { parseCalibrationRow, type ArmCalibration } from '#lib/armCalibration.ts';

export type LogType = 'system' | 'sent' | 'received';

export interface LogEntry {
	id: number;
	time: string;
	text: string;
	type: LogType;
}

const MAX_LOG_LINES = 2000;
// Teleplot lines from the firmware, e.g. ">currentDistance:6.012"
const TELEMETRY_LINE = /^>([^:]+):\s*(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\s*$/;

// Printed by the arm firmware at boot and after every run. Older arm firmware
// treated any line as a target distance, so commands like M are only safe once
// this has been seen.
const CALIBRATION_MENU = 'Calibration commands:';

// Printed at boot, after every run and after a JSON parameters command:
// "MAC: 28:84:85:67:44:74 (arm 1)", "(arm N)" only if it's in defaults.h, then
// the calibration in use as a defaults.h row
const MAC_LINE = /^MAC:\s*((?:[0-9A-F]{2}:){5}[0-9A-F]{2})(?:\s*\(arm (\d+)\))?/i;
const CALIBRATION_ROW = 'Calibration row:';

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

// The S3 can drop off USB on a reset and come back as a new connection, which
// mustn't reset it again
const RESET_RECONNECT_MS = 5000;

// Gets each received line that isn't telemetry, then null when the port closes
export type LineListener = (line: string | null) => void;

class SerialConnection {
	supported = $state(false);
	connected = $state(false);
	connecting = $state(false);
	baudRate = $state(115200);
	showTelemetry = $state(false);
	logs = $state<LogEntry[]>([]);
	telemetry = $state<Record<string, number>>({});
	lastCommand = $state('');
	calibrationFirmware = $state(false);
	// id is null for a board that isn't in defaults.h yet
	board = $state<{ mac: string; id: number | null } | null>(null);
	calibration = $state<ArmCalibration | null>(null);

	#port: SerialPort | null = null;
	#reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
	#readLoop: Promise<void> | null = null;
	#nextId = 0;
	#started = false;
	#listeners = new Set<LineListener>();
	#lastReset = 0;

	// Called once from the browser; reconnects to a port the user already granted
	async init() {
		if (this.#started) return;
		this.#started = true;

		const serial = navigator.serial;
		this.supported = !!serial;
		if (!serial) return;

		// A granted port being plugged back in (or the S3 re-enumerating after a reset)
		serial.addEventListener('connect', (event) => {
			if (!this.connected && !this.connecting) this.open(event.target as SerialPort);
		});

		const [port] = await serial.getPorts();
		if (port) {
			this.log('Found a previously used port, reconnecting...', 'system');
			await this.open(port);
		}
	}

	async request() {
		try {
			const port = await navigator.serial!.requestPort();
			await this.open(port);
		} catch (error) {
			this.log(`Port picker: ${(error as Error).message}`, 'system');
		}
	}

	async open(port: SerialPort) {
		this.connecting = true;
		try {
			await port.open({ baudRate: this.baudRate });
			this.#port = port;
			this.connected = true;
			this.calibrationFirmware = false;
			this.board = null;
			this.calibration = null;

			const { usbVendorId, usbProductId } = port.getInfo();
			const id =
				usbVendorId === undefined
					? ''
					: ` (USB ${usbVendorId.toString(16).padStart(4, '0')}:${usbProductId
							?.toString(16)
							.padStart(4, '0')})`;
			this.log(`Connected at ${this.baudRate} baud${id}`, 'system');
			this.#readLoop = this.#read(port);
		} catch (error) {
			this.log(`Connection failed: ${(error as Error).message}`, 'system');
			return;
		} finally {
			this.connecting = false;
		}

		// So the board prints its MAC, calibration and menu, which it only does
		// at boot and after a run
		if (Date.now() - this.#lastReset > RESET_RECONNECT_MS) await this.reset();
	}

	async disconnect() {
		const port = this.#port;
		if (!port) return;
		this.#port = null; // tells the read loop the close was on purpose

		await this.#reader?.cancel().catch(() => {});
		await this.#readLoop;
		await port.close().catch(() => {});
		this.#closed();
		this.log('Disconnected', 'system');
	}

	// Returns an unsubscribe function
	onLine(listener: LineListener) {
		this.#listeners.add(listener);
		return () => this.#listeners.delete(listener);
	}

	async send(text: string) {
		const writable = this.#port?.writable;
		if (!writable) return;

		const writer = writable.getWriter();
		try {
			await writer.write(new TextEncoder().encode(text + '\n'));
			this.lastCommand = text;
			this.log(text, 'sent');
		} catch (error) {
			this.log(`Send failed: ${(error as Error).message}`, 'system');
		} finally {
			writer.releaseLock();
		}
	}

	// ESP32 auto-reset: pulse EN low through RTS while DTR keeps IO0 high
	async reset() {
		if (!this.#port) return;
		this.#lastReset = Date.now();
		try {
			await this.#port.setSignals({ dataTerminalReady: false, requestToSend: true });
			await sleep(100);
			await this.#port.setSignals({ requestToSend: false });
			this.calibrationFirmware = false; // confirmed again by the boot menu
			this.log('Reset the board', 'system');
		} catch (error) {
			this.log(`Reset failed: ${(error as Error).message}`, 'system');
		}
	}

	clear() {
		this.logs = [];
	}

	// Received text only, ready to paste into the calibration tabs
	receivedText() {
		return this.logs
			.filter((entry) => entry.type === 'received')
			.map((entry) => entry.text)
			.join('\n');
	}

	log(text: string, type: LogType) {
		this.logs.push({ id: this.#nextId++, time: new Date().toLocaleTimeString(), text, type });
		if (this.logs.length > MAX_LOG_LINES) this.logs.splice(0, this.logs.length - MAX_LOG_LINES);
	}

	async #read(port: SerialPort) {
		const decoder = new TextDecoder();
		let buffer = '';

		// readable comes back after recoverable errors (e.g. a framing error)
		while (port.readable && this.#port === port) {
			this.#reader = port.readable.getReader();
			try {
				while (true) {
					const { value, done } = await this.#reader.read();
					if (done) break;
					buffer += decoder.decode(value, { stream: true });
					const lines = buffer.split(/\r?\n/);
					buffer = lines.pop() ?? '';
					for (const line of lines) this.#handleLine(line);
				}
			} catch (error) {
				if (this.#port === port) this.log(`Read error: ${(error as Error).message}`, 'system');
			} finally {
				this.#reader.releaseLock();
				this.#reader = null;
			}
		}
		if (buffer) this.#handleLine(buffer);

		// Still the active port, so the device went away rather than being closed
		if (this.#port === port) {
			this.#port = null;
			await port.close().catch(() => {});
			this.#closed();
			this.log('Connection lost', 'system');
		}
	}

	#closed() {
		this.connected = false;
		this.calibrationFirmware = false;
		for (const listener of this.#listeners) listener(null);
	}

	#handleLine(line: string) {
		const telemetry = line.match(TELEMETRY_LINE);
		if (telemetry) {
			this.telemetry[telemetry[1]] = Number(telemetry[2]);
			if (this.showTelemetry) this.log(line, 'received');
			return;
		}

		if (line.startsWith(CALIBRATION_MENU)) this.calibrationFirmware = true;
		const mac = line.match(MAC_LINE);
		if (mac) {
			const [, address = '', id] = mac;
			this.board = { mac: address.toUpperCase(), id: id === undefined ? null : Number(id) };
		}
		if (line.startsWith(CALIBRATION_ROW)) this.calibration = parseCalibrationRow(line);
		this.log(line, 'received');
		for (const listener of this.#listeners) listener(line);
	}
}

export const serial = new SerialConnection();
