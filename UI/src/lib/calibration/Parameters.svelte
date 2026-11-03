<script lang="ts">
	import { onDestroy } from 'svelte';
	import {
		DEFAULT_CALIBRATION,
		PARAMETERS,
		boardCalibration,
		calibrationRow,
		parameterProblem,
		pickParameters,
		type ArmCalibration,
		type Parameters
	} from '#lib/armCalibration.ts';
	import { runner } from '#lib/calibration/runner.svelte.ts';
	import { moveBetweenProbes } from '#lib/calibration/shared.ts';
	import { serial } from '#lib/serial/serial.svelte.ts';

	// The firmware answers V with its Calibration row, or a "Parameters not set"
	// line if the JSON after it doesn't parse
	const REJECTED = 'Parameters not set:';
	const CALIBRATION_ROW = 'Calibration row:';
	const REPLY_TIMEOUT_MS = 3000;

	let values = $state<Parameters>(pickParameters(serial.calibration ?? DEFAULT_CALIBRATION));
	// Until it's typed in, the form follows what the board reports
	let edited = $state(false);
	let waiting = $state(false);
	let message = $state<{ text: string; error: boolean } | null>(null);
	let copied = $state(false);

	$effect(() => {
		const reported = serial.calibration;
		if (reported && !edited) values = pickParameters(reported);
	});

	const problems = $derived(
		PARAMETERS.map((parameter) => parameterProblem(parameter, values[parameter.key]))
	);
	const valid = $derived(problems.every((problem) => problem === null));
	const command = $derived(`V ${JSON.stringify(values)}`);
	const ready = $derived(
		serial.connected && serial.calibrationFirmware && !runner.running && !waiting
	);
	const row = $derived(
		valid
			? calibrationRow({
					...boardCalibration(serial.calibration, serial.board?.mac),
					...(values as Record<keyof Parameters, number>)
				} satisfies ArmCalibration)
			: ''
	);

	let stopListening: (() => void) | null = null;
	let timer: ReturnType<typeof setTimeout> | undefined;

	function request(command: string, success: string) {
		finish(null);
		waiting = true;
		stopListening = serial.onLine((line) => {
			if (line === null) {
				finish({ text: 'Connection lost before the board replied', error: true });
			} else if (line.startsWith(REJECTED)) {
				finish({ text: `Not set: ${line.slice(REJECTED.length).trim()}`, error: true });
			} else if (line.startsWith(CALIBRATION_ROW)) {
				edited = false; // show what the board now reports
				finish({ text: success, error: false });
			}
		});
		timer = setTimeout(
			() =>
				finish({
					text: "No reply. The board's firmware may be too old for the parameters command.",
					error: true
				}),
			REPLY_TIMEOUT_MS
		);
		serial.send(command);
	}

	function finish(result: { text: string; error: boolean } | null) {
		stopListening?.();
		stopListening = null;
		clearTimeout(timer);
		waiting = false;
		message = result;
	}

	onDestroy(() => finish(null));

	async function copyRow() {
		try {
			await navigator.clipboard.writeText(row);
			copied = true;
			setTimeout(() => (copied = false), 1500);
		} catch {
			// clipboard blocked; the text is selectable in the block
		}
	}
</script>

<div class="mode">
	<header>
		<h2>Parameters</h2>
		<p class="note">
			Sends this board's calibration values as V and one JSON object. It uses them until it resets,
			so paste the row into <code>defaults.h</code> to keep them.
		</p>
	</header>

	<div class="columns">
		<div class="main-col">
			<section class="card">
				<div class="card-head">
					<h3>Values</h3>
					<div class="actions">
						<button
							class="secondary"
							onclick={() => request('V', 'Read the values the board is using.')}
							disabled={!ready}
						>
							Read from board
						</button>
						<button
							onclick={() => request(command, 'Sent. The board uses these until it resets.')}
							disabled={!ready || !valid}
						>
							{waiting ? 'Waiting...' : 'Send to board'}
						</button>
					</div>
				</div>

				{#if !serial.connected}
					<p class="note">Connect to the arm on the Serial monitor tab first.</p>
				{:else if !serial.calibrationFirmware}
					<p class="note">
						Waiting to see the firmware's calibration menu.
						<button class="link" onclick={() => serial.reset()}>Reset the board</button> to check.
					</p>
				{:else if runner.running}
					<p class="note">The board ignores input until the run finishes.</p>
				{:else if !serial.calibration}
					<p class="note">The board hasn't reported its values yet. Read them from the board.</p>
				{/if}
				{#if message}
					<p class="note" class:warn={message.error} aria-live="polite">{message.text}</p>
				{/if}

				<div class="table-wrap">
					<table>
						<thead>
							<tr>
								<th class="name">Parameter</th>
								<th>On board</th>
								<th class="probe-col">Send</th>
							</tr>
						</thead>
						<tbody>
							{#each PARAMETERS as parameter, i (parameter.key)}
								{@const reported = serial.calibration?.[parameter.key]}
								<tr>
									<th class="name" scope="row">
										{parameter.label}
										<code>{parameter.key}</code>
									</th>
									<td class="muted">{reported ?? '–'}</td>
									<td class="probe-col">
										<input
											class="probe"
											class:changed={reported !== undefined && values[parameter.key] !== reported}
											type="number"
											step="any"
											aria-label={parameter.label}
											aria-invalid={problems[i] !== null}
											bind:value={values[parameter.key]}
											oninput={() => (edited = true)}
											onkeydown={moveBetweenProbes}
										/>
										{#if problems[i]}
											<div class="warn problem">{problems[i]}</div>
										{/if}
									</td>
								</tr>
							{/each}
						</tbody>
					</table>
				</div>
			</section>
		</div>

		<aside class="side-col">
			<section class="card">
				<h3>Command</h3>
				<p class="note">
					What Send writes to the board, always with all the values. <code>V</code> on its own prints
					the values in use.
				</p>
				<pre><code>{command}</code></pre>
			</section>

			<section class="card">
				<div class="card-head">
					<h3>Row for defaults.h</h3>
					{#if row}
						<button onclick={copyRow}>{copied ? 'Copied' : 'Copy'}</button>
					{/if}
				</div>
				{#if row}
					<p class="note">
						Paste into <code>ARM_CALIBRATIONS</code> in <code>firmware/common/defaults.h</code>,
						replacing this board's row if it has one.
					</p>
					<pre><code>{row}</code></pre>
				{:else}
					<p class="note">Fix the values marked in red to see the row.</p>
				{/if}
			</section>
		</aside>
	</div>
</div>

<style>
	.actions {
		display: flex;
		gap: 8px;
	}

	button:disabled {
		opacity: 0.5;
		cursor: default;
	}

	button.link {
		padding: 0;
		border: 0;
		background: none;
		color: var(--series-1);
		font-size: inherit;
		text-decoration: underline;
	}

	table {
		margin-top: 4px;
	}

	.name {
		text-align: left;
	}

	tbody th.name {
		color: var(--text-primary);
		font-weight: 400;
		vertical-align: middle;
	}

	/* No spinners, like the probe cells on the other tabs */
	input.probe {
		appearance: textfield;
	}

	input.probe::-webkit-inner-spin-button,
	input.probe::-webkit-outer-spin-button {
		appearance: none;
		margin: 0;
	}

	.name code {
		margin-left: 6px;
		color: var(--text-muted);
		font-size: 12px;
	}

	/* Values that differ from the board's stand out */
	input.changed {
		color: var(--series-1);
		font-weight: 600;
	}

	input[aria-invalid='true'] {
		border-color: var(--critical);
	}

	.problem {
		margin-top: 4px;
		font-size: 12px;
		white-space: normal;
	}

	/* Wrap rather than cut long lines in the narrow column; Copy takes the raw text */
	pre {
		font-size: 12px;
		white-space: pre-wrap;
		overflow-wrap: anywhere;
	}
</style>
