// USB serial connection to the arm, shared app-wide so switching tabs doesn't
// drop the port or the log.

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

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

class SerialConnection {
	supported = $state(false);
	connected = $state(false);
	connecting = $state(false);
	baudRate = $state(115200);
	showTelemetry = $state(false);
	logs = $state<LogEntry[]>([]);
	telemetry = $state<Record<string, number>>({});
	lastCommand = $state('');

	#port: SerialPort | null = null;
	#reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
	#readLoop: Promise<void> | null = null;
	#nextId = 0;
	#started = false;

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
		} finally {
			this.connecting = false;
		}
	}

	async disconnect() {
		const port = this.#port;
		if (!port) return;
		this.#port = null; // tells the read loop the close was on purpose

		await this.#reader?.cancel().catch(() => {});
		await this.#readLoop;
		await port.close().catch(() => {});
		this.connected = false;
		this.log('Disconnected', 'system');
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
		try {
			await this.#port.setSignals({ dataTerminalReady: false, requestToSend: true });
			await sleep(100);
			await this.#port.setSignals({ requestToSend: false });
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
			this.connected = false;
			await port.close().catch(() => {});
			this.log('Connection lost', 'system');
		}
	}

	#handleLine(line: string) {
		const telemetry = line.match(TELEMETRY_LINE);
		if (telemetry) {
			this.telemetry[telemetry[1]] = Number(telemetry[2]);
			if (!this.showTelemetry) return;
		}
		this.log(line, 'received');
	}
}

export const serial = new SerialConnection();
