<script lang="ts">
	import { tick } from 'svelte';
	import { serial } from './serial.svelte.ts';

	const BAUD_RATES = [9600, 57600, 115200, 230400, 460800, 921600];
	const QUICK_COMMANDS = [
		{ command: 'M', label: 'Magnetometer sweep' },
		{ command: 'R', label: 'Repeatability ×10' },
		{ command: 'R5', label: 'Repeatability ×5' },
		{ command: 'P', label: 'PID tuning' },
		{ command: 'C', label: 'Motor current' }
	];

	let message = $state('');
	let terminal: HTMLDivElement | undefined = $state();
	let followOutput = $state(true);
	let copied = $state(false);

	// Keep the newest line in view unless the user has scrolled up to read
	$effect(() => {
		void serial.logs.length;
		if (!followOutput) return;
		tick().then(() => {
			if (terminal) terminal.scrollTop = terminal.scrollHeight;
		});
	});

	function onScroll() {
		if (!terminal) return;
		followOutput = terminal.scrollHeight - terminal.scrollTop - terminal.clientHeight < 24;
	}

	async function sendMessage() {
		const text = message.trim();
		if (!text) return;
		await serial.send(text);
		message = '';
	}

	function onKeyDown(event: KeyboardEvent) {
		if (event.key === 'Enter') {
			sendMessage();
		} else if (event.key === 'ArrowUp' && serial.lastCommand) {
			event.preventDefault();
			message = serial.lastCommand;
		}
	}

	async function copyReceived() {
		try {
			await navigator.clipboard.writeText(serial.receivedText());
			copied = true;
			setTimeout(() => (copied = false), 1500);
		} catch {
			serial.log('Clipboard blocked by the browser', 'system');
		}
	}

	const telemetryEntries = $derived(Object.entries(serial.telemetry));
</script>

<div class="mode">
	<header>
		<h2>Serial monitor</h2>
		<p class="note">USB serial to the arm. The connection stays open while you switch tabs.</p>
	</header>

	{#if !serial.supported}
		<section class="card placeholder">
			This browser has no Web Serial support. Open the page in Chrome or Edge.
		</section>
	{:else}
		<div class="monitor">
			<section class="card terminal-card">
				<div class="toolbar">
					<span class="status" class:online={serial.connected}>
						{serial.connected ? 'Connected' : serial.connecting ? 'Connecting...' : 'Offline'}
					</span>
					<label class="check">
						<input type="checkbox" bind:checked={serial.showTelemetry} />
						Show telemetry lines
					</label>
					<span class="spacer"></span>
					<button class="secondary" onclick={copyReceived} disabled={serial.logs.length === 0}>
						{copied ? 'Copied' : 'Copy output'}
					</button>
					<button class="secondary" onclick={() => serial.clear()}>Clear</button>
				</div>

				<div
					class="terminal"
					bind:this={terminal}
					onscroll={onScroll}
					role="log"
					aria-live="polite"
				>
					{#each serial.logs as entry (entry.id)}
						<div class="line {entry.type}">
							<span class="time">{entry.time}</span>
							<span class="text">{entry.type === 'sent' ? `> ${entry.text}` : entry.text}</span>
						</div>
					{:else}
						<div class="line system">
							<span class="text">
								{serial.connected ? 'Waiting for data...' : 'Connect to a port to see its output.'}
							</span>
						</div>
					{/each}
				</div>

				<div class="input-bar">
					<input
						type="text"
						placeholder={serial.connected
							? 'Type a command, Enter to send, ↑ for the last one'
							: 'Connect first to send'}
						bind:value={message}
						onkeydown={onKeyDown}
						disabled={!serial.connected}
					/>
					<button onclick={sendMessage} disabled={!serial.connected}>Send</button>
				</div>
			</section>

			<aside>
				<section class="card">
					<h3>Connection</h3>
					<div class="fields">
						<label>
							Baud rate
							<select bind:value={serial.baudRate} disabled={serial.connected}>
								{#each BAUD_RATES as rate (rate)}
									<option value={rate}>{rate}</option>
								{/each}
							</select>
						</label>
					</div>
					<div class="actions">
						{#if serial.connected}
							<button class="secondary" onclick={() => serial.disconnect()}>Disconnect</button>
							<button class="secondary" onclick={() => serial.reset()}>Reset board</button>
						{:else}
							<button onclick={() => serial.request()} disabled={serial.connecting}>
								{serial.connecting ? 'Connecting...' : 'Connect'}
							</button>
						{/if}
					</div>
				</section>

				<section class="card">
					<h3>Calibration commands</h3>
					<div class="commands">
						{#each QUICK_COMMANDS as quick (quick.command)}
							<button
								class="secondary command"
								onclick={() => serial.send(quick.command)}
								disabled={!serial.connected}
							>
								<code>{quick.command}</code>
								{quick.label}
							</button>
						{/each}
					</div>
				</section>

				<section class="card">
					<h3>Live values</h3>
					{#if telemetryEntries.length}
						<dl>
							{#each telemetryEntries as [name, value] (name)}
								<dt>{name}</dt>
								<dd>{value.toFixed(3)}</dd>
							{/each}
						</dl>
					{:else}
						<p class="note">The firmware's <code>&gt;name:value</code> lines show up here.</p>
					{/if}
				</section>
			</aside>
		</div>
	{/if}
</div>

<style>
	.monitor {
		display: grid;
		grid-template-columns: minmax(0, 1fr) 280px;
		gap: 16px;
		align-items: start;
	}

	@media (max-width: 900px) {
		.monitor {
			grid-template-columns: minmax(0, 1fr);
		}
	}

	.terminal-card {
		display: flex;
		flex-direction: column;
		gap: 12px;
	}

	.toolbar {
		display: flex;
		flex-wrap: wrap;
		align-items: center;
		gap: 8px 12px;
	}

	.spacer {
		flex: 1;
	}

	.status {
		display: inline-flex;
		align-items: center;
		gap: 6px;
		font-size: 14px;
		color: var(--text-secondary);
	}

	.status::before {
		content: '';
		width: 8px;
		height: 8px;
		border-radius: 50%;
		background: var(--baseline);
	}

	.status.online {
		color: var(--text-primary);
	}

	.status.online::before {
		background: #0ca30c;
	}

	label.check {
		flex-direction: row;
		align-items: center;
		gap: 6px;
	}

	label.check input {
		width: auto;
	}

	.terminal {
		height: 60vh;
		min-height: 320px;
		overflow-y: auto;
		padding: 8px 10px;
		border: 1px solid var(--border);
		border-radius: 6px;
		background: #fff;
		font-family: ui-monospace, 'SFMono-Regular', Menlo, monospace;
		font-size: 13px;
		line-height: 1.5;
	}

	.line {
		display: flex;
		gap: 10px;
	}

	.time {
		flex: none;
		color: var(--text-muted);
	}

	.text {
		white-space: pre-wrap;
		overflow-wrap: anywhere;
	}

	.line.sent .text {
		color: var(--series-1);
		font-weight: 600;
	}

	.line.system .text {
		color: var(--text-secondary);
		font-style: italic;
	}

	.input-bar {
		display: flex;
		gap: 8px;
	}

	.input-bar input {
		font-family: ui-monospace, 'SFMono-Regular', Menlo, monospace;
	}

	aside {
		display: flex;
		flex-direction: column;
		gap: 16px;
	}

	select {
		padding: 8px;
		border: 1px solid var(--baseline);
		border-radius: 6px;
		background: #fff;
		font: inherit;
	}

	.actions {
		display: flex;
		flex-wrap: wrap;
		gap: 8px;
	}

	.commands {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}

	.command {
		display: flex;
		align-items: center;
		gap: 10px;
		text-align: left;
	}

	.command code {
		min-width: 22px;
		color: var(--text-secondary);
	}

	button:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	dl {
		display: grid;
		grid-template-columns: minmax(0, 1fr) auto;
		gap: 4px 12px;
		margin: 0;
		font-size: 14px;
	}

	dt {
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
		color: var(--text-secondary);
	}

	dd {
		margin: 0;
		text-align: right;
		font-variant-numeric: tabular-nums;
	}
</style>
