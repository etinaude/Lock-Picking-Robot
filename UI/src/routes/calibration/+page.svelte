<script lang="ts">
	import { onMount } from 'svelte';
	import Chart, { type Series } from '#lib/Chart.svelte';
	import {
		FIRMWARE_DEFAULTS,
		fieldAtMotion,
		fitCalibration,
		parseCalibration,
		predictMotion,
		type MagnetParams
	} from '#lib/magnet.ts';

	const STORAGE_KEY = 'magnet-calibration';
	const PLACEHOLDER = `Set Distance (mm)  Magno reading(mT)  Predicted distance (mm)
3.00               12.7880            3.001
3.50               10.6120            3.502
...

Temperature: 25.40 C`;

	let text = $state('');
	let probes = $state<string[]>([]);
	let probeZeroInput = $state<number | null>(null);
	let model = $state<MagnetParams>({ ...FIRMWARE_DEFAULTS });
	let loaded = $state(false);
	let copied = $state(false);

	onMount(() => {
		try {
			const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? 'null');
			if (saved) {
				text = saved.text ?? '';
				probes = saved.probes ?? [];
				probeZeroInput = saved.probeZeroInput ?? null;
				model = { ...FIRMWARE_DEFAULTS, ...saved.model };
			}
		} catch {
			// storage blocked or corrupt; start fresh
		}
		loaded = true;
	});

	$effect(() => {
		const snapshot = JSON.stringify({ text, probes, probeZeroInput, model });
		if (!loaded) return;
		try {
			localStorage.setItem(STORAGE_KEY, snapshot);
		} catch {
			// storage blocked; nothing to keep
		}
	});

	const parsed = $derived(parseCalibration(text));
	const rows = $derived(parsed.rows);
	const runTempC = $derived(parsed.temperatureC);
	const probeZero = $derived(probeZeroInput ?? rows[0]?.setMM ?? 0);

	const modelValid = $derived(
		[model.remanenceMT, model.diameterMM, model.thicknessMM].every(
			(v) => Number.isFinite(v) && v > 0
		) && [model.zOffsetMM, model.calibrationTempC].every(Number.isFinite)
	);

	// True carriage position from the probe, or null until a reading is entered
	const trueMM = $derived(
		rows.map((_, i) => {
			const reading = parseFloat(probes[i] ?? '');
			return Number.isFinite(reading) ? probeZero + reading : null;
		})
	);
	const probeCount = $derived(trueMM.filter((v) => v !== null).length);

	const fit = $derived(
		modelValid
			? fitCalibration(
					rows.flatMap((row, i) =>
						trueMM[i] === null ? [] : [{ motionMM: trueMM[i]!, bzMT: row.bzMT }]
					),
					model
				)
			: null
	);

	// The fit is of raw readings, so it holds at the temperature of the run
	const calibrated = $derived<MagnetParams | null>(
		fit ? { ...model, ...fit, calibrationTempC: runTempC ?? model.calibrationTempC } : null
	);

	const currentError = $derived(
		rows.map((row, i) =>
			trueMM[i] === null || !modelValid
				? null
				: predictMotion(row.bzMT, model, runTempC) - trueMM[i]!
		)
	);
	const calibratedError = $derived(
		rows.map((row, i) =>
			trueMM[i] === null || !calibrated
				? null
				: predictMotion(row.bzMT, calibrated, runTempC) - trueMM[i]!
		)
	);

	function stats(errors: (number | null)[]) {
		const values = errors.filter((v): v is number => v !== null);
		if (values.length === 0) return null;
		return {
			max: Math.max(...values.map(Math.abs)),
			rms: Math.sqrt(values.reduce((sum, v) => sum + v * v, 0) / values.length)
		};
	}
	const currentStats = $derived(stats(currentError));
	const calibratedStats = $derived(stats(calibratedError));

	// Measured points sit at the probe position, or the set distance until entered
	const measuredXs = $derived(rows.map((row, i) => trueMM[i] ?? row.setMM));
	const measuredSeries = $derived<Series>({
		name: 'Measured',
		color: 'var(--series-2)',
		kind: 'points',
		data: rows.map((row, i) => ({ x: measuredXs[i], y: row.bzMT }))
	});

	function curve(name: string, color: string, params: MagnetParams): Series {
		const low = Math.max(Math.min(...measuredXs) - 0.5, 0.2 - params.zOffsetMM);
		const high = Math.max(...measuredXs) + 0.5;
		const data = Array.from({ length: 121 }, (_, i) => {
			const x = low + ((high - low) * i) / 120;
			return { x, y: fieldAtMotion(x, params, runTempC) };
		});
		return { name, color, kind: 'line', data };
	}

	const currentChart = $derived(
		rows.length && modelValid
			? [curve('Current model', 'var(--series-1)', model), measuredSeries]
			: []
	);
	const calibratedChart = $derived(
		rows.length && calibrated
			? [curve('Calibrated model', 'var(--series-3)', calibrated), measuredSeries]
			: []
	);
	const errorChart = $derived<Series[]>(
		calibrated
			? [
					{
						name: 'Current model',
						color: 'var(--series-1)',
						kind: 'points',
						data: rows.flatMap((_, i) =>
							currentError[i] === null ? [] : [{ x: trueMM[i]!, y: currentError[i]! }]
						)
					},
					{
						name: 'Calibrated model',
						color: 'var(--series-3)',
						kind: 'points',
						data: rows.flatMap((_, i) =>
							calibratedError[i] === null ? [] : [{ x: trueMM[i]!, y: calibratedError[i]! }]
						)
					}
				]
			: []
	);

	const cpp = $derived(
		calibrated
			? [
					`const float DEFAULT_REMANENCE_MT = ${calibrated.remanenceMT.toFixed(1)}f;`,
					`const float DEFAULT_Z_OFFSET_MM = ${calibrated.zOffsetMM.toFixed(3)}f;`,
					`const float DEFAULT_CALIBRATION_TEMP_C = ${calibrated.calibrationTempC.toFixed(2)}f;`,
					`const float MAGNET_R_MM = ${(calibrated.diameterMM / 2).toFixed(2)}f;`,
					`const float MAGNET_T_MM = ${calibrated.thicknessMM.toFixed(2)}f;`,
					'',
					'// or at runtime:',
					`// magnet.setCalibration(${calibrated.remanenceMT.toFixed(1)}f, ${calibrated.zOffsetMM.toFixed(3)}f, ${calibrated.calibrationTempC.toFixed(2)}f);`
				].join('\n')
			: ''
	);

	async function copyCpp() {
		try {
			await navigator.clipboard.writeText(cpp);
			copied = true;
			setTimeout(() => (copied = false), 1500);
		} catch {
			// clipboard blocked; the text is selectable in the block
		}
	}

	// Pasting a column of readings into one cell fills it and the rows below
	function onProbePaste(event: ClipboardEvent, index: number) {
		const pasted = event.clipboardData?.getData('text') ?? '';
		const values = pasted
			.split(/[\r\n\t]+/)
			.map((v) => v.trim())
			.filter(Boolean);
		if (values.length < 2) return;
		event.preventDefault();
		const next = [...probes];
		values.forEach((value, offset) => {
			if (index + offset < rows.length) next[index + offset] = value;
		});
		probes = next;
	}

	const mm = (v: number) => v.toFixed(3);
	const signedMM = (v: number) => (v >= 0 ? '+' : '') + v.toFixed(3);
</script>

<svelte:head>
	<title>Magnet Calibration</title>
</svelte:head>

<main>
	<header>
		<a href="/">← Home</a>
		<h1>Magnetometer calibration</h1>
		<p>
			Run <code>M</code> (or <code>R</code>) on the <code>calibration</code> firmware, paste the table
			below, then enter the probe reading for each row.
		</p>
	</header>

	<section class="card">
		<h2>1. Paste the firmware output</h2>
		<textarea bind:value={text} placeholder={PLACEHOLDER} rows="8" spellcheck="false"></textarea>
		<p class="note">
			{rows.length} rows ·
			{#if runTempC !== null}
				run temperature {runTempC.toFixed(2)} °C
			{:else if rows.length}
				<span class="warn">no "Temperature:" line, so no temperature correction</span>
			{:else}
				waiting for a table
			{/if}
		</p>
	</section>

	<section class="card">
		<h2>2. Current model</h2>
		<p class="note">Prefilled with the firmware defaults in <code>magnet.h</code>.</p>
		<div class="fields">
			<label
				>Magnet diameter (mm)<input type="number" step="0.1" bind:value={model.diameterMM} /></label
			>
			<label
				>Magnet thickness (mm)<input
					type="number"
					step="0.1"
					bind:value={model.thicknessMM}
				/></label
			>
			<label>Remanence (mT)<input type="number" step="1" bind:value={model.remanenceMT} /></label>
			<label>Z offset (mm)<input type="number" step="0.01" bind:value={model.zOffsetMM} /></label>
			<label
				>Calibration temp (°C)<input
					type="number"
					step="0.1"
					bind:value={model.calibrationTempC}
				/></label
			>
			<label>
				Probe zeroed at (mm)
				<input
					type="number"
					step="0.01"
					placeholder={String(rows[0]?.setMM ?? 0)}
					bind:value={probeZeroInput}
				/>
			</label>
		</div>
		<button class="secondary" onclick={() => (model = { ...FIRMWARE_DEFAULTS })}>
			Reset to firmware defaults
		</button>
	</section>

	{#if rows.length}
		<section class="card">
			<div class="card-head">
				<h2>3. Probe readings</h2>
				<button class="secondary" onclick={() => (probes = [])}>Clear readings</button>
			</div>
			<p class="note">
				{probeCount} of {rows.length} entered. Pasting a column into a cell fills the rows below it.
			</p>
			<div class="table-wrap">
				<table>
					<thead>
						<tr>
							<th>Set (mm)</th>
							<th>B<sub>z</sub> (mT)</th>
							<th>Firmware predicted (mm)</th>
							{#if parsed.hasTime}<th>Time to target (ms)</th>{/if}
							<th>Probe (mm)</th>
							<th>True (mm)</th>
							<th>Current error (mm)</th>
							<th>Calibrated error (mm)</th>
						</tr>
					</thead>
					<tbody>
						{#each rows as row, i (i)}
							<tr>
								<td>{row.setMM.toFixed(2)}</td>
								<td>{row.bzMT.toFixed(4)}</td>
								<td>{mm(row.predictedMM)}</td>
								{#if parsed.hasTime}<td>{row.timeMs ?? 'timeout'}</td>{/if}
								<td>
									<input
										class="probe"
										type="text"
										inputmode="decimal"
										aria-label={`Probe reading at ${row.setMM} mm`}
										bind:value={probes[i]}
										onpaste={(event) => onProbePaste(event, i)}
									/>
								</td>
								<td>{trueMM[i] === null ? '–' : mm(trueMM[i]!)}</td>
								<td>{currentError[i] === null ? '–' : signedMM(currentError[i]!)}</td>
								<td>{calibratedError[i] === null ? '–' : signedMM(calibratedError[i]!)}</td>
							</tr>
						{/each}
					</tbody>
				</table>
			</div>
		</section>

		{#if currentChart.length}
			<section class="card">
				<h2>Current model vs measured</h2>
				<p class="note">
					Field predicted from the magnet size and strength above
					{#if runTempC !== null}(at {runTempC.toFixed(1)} °C){/if}, with the readings over it.
					{#if probeCount < rows.length}
						{rows.length - probeCount} point{rows.length - probeCount === 1 ? '' : 's'} sit at the set
						distance until a probe reading is entered.
					{/if}
				</p>
				<Chart series={currentChart} xLabel="Distance (mm)" yLabel="B_z (mT)" />
			</section>
		{/if}

		<section class="card">
			<h2>Calibrated constants</h2>
			{#if calibrated && calibratedStats && currentStats}
				<div class="stats">
					<div>
						<span class="stat-label">Current model error</span>
						<span class="stat-value">±{currentStats.max.toFixed(3)} mm</span>
						<span class="stat-sub">rms {currentStats.rms.toFixed(3)} mm</span>
					</div>
					<div>
						<span class="stat-label">Calibrated model error</span>
						<span class="stat-value">±{calibratedStats.max.toFixed(3)} mm</span>
						<span class="stat-sub">rms {calibratedStats.rms.toFixed(3)} mm</span>
					</div>
				</div>
				<div class="code-head">
					<span class="note">Paste into <code>firmware/arm/magnet.h</code></span>
					<button onclick={copyCpp}>{copied ? 'Copied' : 'Copy'}</button>
				</div>
				<pre><code>{cpp}</code></pre>
			{:else}
				<p class="note">Enter at least two probe readings to fit the remanence and z offset.</p>
			{/if}
		</section>

		{#if calibratedChart.length}
			<section class="card">
				<h2>Calibrated model vs measured</h2>
				<Chart series={calibratedChart} xLabel="Distance (mm)" yLabel="B_z (mT)" />
			</section>
		{/if}

		{#if errorChart.length}
			<section class="card">
				<h2>Position error</h2>
				<p class="note">Predicted minus true distance for each reading.</p>
				<Chart
					series={errorChart}
					xLabel="True distance (mm)"
					yLabel="Error (mm)"
					zeroLine
					formatY={signedMM}
				/>
			</section>
		{/if}
	{/if}
</main>

<style>
	main {
		max-width: 1080px;
		margin: 0 auto;
		padding: 24px 16px 64px;
		display: flex;
		flex-direction: column;
		gap: 16px;
	}

	header a {
		color: var(--text-secondary);
		font-size: 14px;
	}

	h1 {
		margin: 8px 0 4px;
		font-size: 24px;
	}

	h2 {
		margin: 0 0 8px;
		font-size: 16px;
	}

	header p,
	.note {
		margin: 0 0 12px;
		color: var(--text-secondary);
		font-size: 14px;
	}

	.warn {
		color: var(--critical);
	}

	.card {
		background: var(--surface-1);
		border: 1px solid var(--border);
		border-radius: 8px;
		padding: 16px;
		min-width: 0;
	}

	.card-head,
	.code-head {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 12px;
	}

	textarea,
	input {
		box-sizing: border-box;
		width: 100%;
		padding: 8px;
		border: 1px solid var(--baseline);
		border-radius: 6px;
		background: var(--page);
		color: var(--text-primary);
		font: inherit;
	}

	textarea {
		font-family: ui-monospace, 'SFMono-Regular', Menlo, monospace;
		font-size: 13px;
		resize: vertical;
	}

	.fields {
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
		gap: 12px;
		margin-bottom: 12px;
	}

	label {
		display: flex;
		flex-direction: column;
		gap: 4px;
		font-size: 13px;
		color: var(--text-secondary);
	}

	button {
		padding: 6px 12px;
		border: 1px solid var(--series-1);
		border-radius: 6px;
		background: var(--series-1);
		color: #fff;
		font: inherit;
		font-size: 14px;
		cursor: pointer;
	}

	button.secondary {
		background: transparent;
		border-color: var(--baseline);
		color: var(--text-primary);
	}

	.table-wrap {
		overflow-x: auto;
	}

	table {
		width: 100%;
		border-collapse: collapse;
		font-size: 14px;
		font-variant-numeric: tabular-nums;
	}

	th,
	td {
		padding: 6px 8px;
		text-align: right;
		white-space: nowrap;
		border-bottom: 1px solid var(--gridline);
	}

	th {
		color: var(--text-secondary);
		font-weight: 500;
	}

	input.probe {
		width: 90px;
		padding: 4px 6px;
		text-align: right;
	}

	.stats {
		display: flex;
		flex-wrap: wrap;
		gap: 32px;
		margin-bottom: 16px;
	}

	.stats div {
		display: flex;
		flex-direction: column;
	}

	.stat-label,
	.stat-sub {
		color: var(--text-secondary);
		font-size: 13px;
	}

	.stat-value {
		font-size: 24px;
		font-weight: 600;
	}

	pre {
		margin: 8px 0 0;
		padding: 12px;
		overflow-x: auto;
		border-radius: 6px;
		background: var(--page);
		border: 1px solid var(--border);
		font-size: 13px;
	}
</style>
