// The parts of the Web Serial API this app uses (not yet in TypeScript's DOM lib)

interface SerialPortInfo {
	usbVendorId?: number;
	usbProductId?: number;
}

interface SerialOptions {
	baudRate: number;
	dataBits?: number;
	stopBits?: number;
	parity?: 'none' | 'even' | 'odd';
	bufferSize?: number;
	flowControl?: 'none' | 'hardware';
}

interface SerialOutputSignals {
	dataTerminalReady?: boolean;
	requestToSend?: boolean;
	break?: boolean;
}

interface SerialPort extends EventTarget {
	readonly readable: ReadableStream<Uint8Array> | null;
	readonly writable: WritableStream<Uint8Array> | null;
	open(options: SerialOptions): Promise<void>;
	close(): Promise<void>;
	getInfo(): SerialPortInfo;
	setSignals(signals: SerialOutputSignals): Promise<void>;
}

interface Serial extends EventTarget {
	getPorts(): Promise<SerialPort[]>;
	requestPort(options?: { filters?: SerialPortInfo[] }): Promise<SerialPort>;
}

interface Navigator {
	// Missing in Firefox and Safari
	readonly serial?: Serial;
}
