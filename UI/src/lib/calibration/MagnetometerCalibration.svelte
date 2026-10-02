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
	import {
		loadState,
		mm,
		moveBetweenProbes,
		parseProbe,
		pasteColumn,
		saveState,
		signedMM,
		summarise
	} from './shared.ts';

	const STORAGE_KEY = 'magnet-calibration';
	const PLACEHOLDER = `Set Distance (mm)  Magno reading(mT)  Predicted distance (mm)
3.00               12.7880            3.001
3.50               10.6120            3.502
...

Temperature: 25.40 C`;

	interface Saved {
		text: string;
		probes: string[];
		probeZeroInput: number | null;
		model: MagnetParams;
	}

	let text = $state('');
	let probes = $state<string[]>([]);
	let probeZeroInput = $state<number | null>(null);
	let model = $state<MagnetParams>({ ...FIRMWARE_DEFAULTS });
	let loaded = $state(false);
	let copied = $state(false);

	onMount(() => {
		const saved = loadState<Saved>(STORAGE_KEY);
		if (saved) {
			text = saved.text ?? '';
			probes = saved.probes ?? [];
			probeZeroInput = saved.probeZeroInput ?? null;
			model = { ...FIRMWARE_DEFAULTS, ...saved.model };
		}
		loaded = true;
	});

	$effect(() => {
		const snapshot = { text, probes, probeZeroInput, model };
		if (loaded) saveState(STORAGE_KEY, snapshot);
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
			const reading = parseProbe(probes[i]);
			return reading === null ? null : probeZero + reading;
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

	const currentStats = $derived(summarise(currentError.filter((v) => v !== null)));
	const calibratedStats = $derived(summarise(calibratedError.filter((v) => v !== null)));

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

	function onProbePaste(event: ClipboardEvent, index: number) {
		const next = pasteColumn(event, probes, index, rows.length);
		if (next) probes = next;
	}
</script>

<div class="mode">
	<header>
		<h2>Magnetometer</h2>
		<p class="note">
			Run <code>M</code> on the calibration firmware, paste its table, then type the probe reading for
			each set point.
		</p>
	</header>

	<div class="split">
		<section class="card">
			<h3>Firmware output</h3>
			<textarea bind:value={text} placeholder={PLACEHOLDER} rows="9" spellcheck="false"></textarea>
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
			<h3>Current model</h3>
			<p class="note">Prefilled with the firmware defaults in <code>magnet.h</code>.</p>
			<div class="fields">
				<label>Diameter (mm)<input type="number" step="0.1" bind:value={model.diameterMM} /></label>
				<label
					>Thickness (mm)<input type="number" step="0.1" bind:value={model.thicknessMM} /></label
				>
				<label>Remanence (mT)<input type="number" step="1" bind:value={model.remanenceMT} /></label>
				<label>Z offset (mm)<input type="number" step="0.01" bind:value={model.zOffsetMM} /></label>
				<label>
					Calibration temp (°C)
					<input type="number" step="0.1" bind:value={model.calibrationTempC} />
				</label>
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
	</div>

	<section class="card">
		<div class="card-head">
			<div>
				<h3>Probe readings</h3>
				<p class="note">
					{#if rows.length}
						{probeCount} of {rows.length} entered. Enter or ↓ moves to the next row; pasting a column
						fills the rows below.
					{:else}
						Paste the firmware output above to get a row for each set point.
					{/if}
				</p>
			</div>
			{#if rows.length}
				<button class="secondary" onclick={() => (probes = [])}>Clear readings</button>
			{/if}
		</div>

		{#if rows.length}
			<div class="table-wrap">
				<table>
					<thead>
						<tr>
							<th>#</th>
							<th>Set (mm)</th>
							<th class="probe-col">Probe (mm)</th>
							<th>True (mm)</th>
							<th>B<sub>z</sub> (mT)</th>
							<th>Firmware predicted (mm)</th>
							<th>Current error (mm)</th>
							<th>Calibrated error (mm)</th>
						</tr>
					</thead>
					<tbody>
						{#each rows as row, i (i)}
							<tr>
								<td class="muted">{i + 1}</td>
								<td>{row.setMM.toFixed(2)}</td>
								<td class="probe-col">
									<input
										class="probe"
										type="text"
										inputmode="decimal"
										placeholder="–"
										aria-label={`Probe reading at ${row.setMM} mm`}
										bind:value={probes[i]}
										onpaste={(event) => onProbePaste(event, i)}
										onkeydown={moveBetweenProbes}
									/>
								</td>
								<td>{trueMM[i] === null ? '–' : mm(trueMM[i]!)}</td>
								<td>{row.bzMT.toFixed(4)}</td>
								<td>{mm(row.predictedMM)}</td>
								<td>{currentError[i] === null ? '–' : signedMM(currentError[i]!)}</td>
								<td>{calibratedError[i] === null ? '–' : signedMM(calibratedError[i]!)}</td>
							</tr>
						{/each}
					</tbody>
				</table>
			</div>
		{/if}
	</section>

	{#if rows.length}
		<section class="card">
			<h3>Calibrated constants</h3>
			{#if calibrated && calibratedStats && currentStats}
				<div class="stats">
					<div>
						<span class="stat-label">Current model error</span>
						<span class="stat-value">±{currentStats.maxAbs.toFixed(3)} mm</span>
						<span class="stat-sub">rms {currentStats.rms.toFixed(3)} mm</span>
					</div>
					<div>
						<span class="stat-label">Calibrated model error</span>
						<span class="stat-value">±{calibratedStats.maxAbs.toFixed(3)} mm</span>
						<span class="stat-sub">rms {calibratedStats.rms.toFixed(3)} mm</span>
					</div>
				</div>
				<div class="card-head">
					<span class="note">Paste into <code>firmware/arm/magnet.h</code></span>
					<button onclick={copyCpp}>{copied ? 'Copied' : 'Copy'}</button>
				</div>
				<pre><code>{cpp}</code></pre>
			{:else}
				<p class="note">Enter at least two probe readings to fit the remanence and z offset.</p>
			{/if}
		</section>

		{#if currentChart.length}
			<section class="card">
				<h3>Current model vs measured</h3>
				<p class="note">
					Field predicted from the magnet size and strength
					{#if runTempC !== null}(at {runTempC.toFixed(1)} °C){/if}, with the readings over it.
					{#if probeCount < rows.length}
						{rows.length - probeCount} point{rows.length - probeCount === 1 ? '' : 's'} sit at the set
						distance until a probe reading is entered.
					{/if}
				</p>
				<Chart series={currentChart} xLabel="Distance (mm)" yLabel="B_z (mT)" />
			</section>
		{/if}

		{#if calibratedChart.length}
			<section class="card">
				<h3>Calibrated model vs measured</h3>
				<Chart series={calibratedChart} xLabel="Distance (mm)" yLabel="B_z (mT)" />
			</section>
		{/if}

		{#if errorChart.length}
			<section class="card">
				<h3>Position error</h3>
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
</div>
