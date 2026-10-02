<script lang="ts">
	import { onMount } from 'svelte';
	import Chart, { type Series } from '#lib/Chart.svelte';
	import {
		NOMINAL_N52,
		fieldAtFace,
		fitCalibration,
		fitRegression,
		parseCalibration,
		predictMotion,
		predictRegression,
		type FitPoint,
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
		statClass,
		summarise,
		errorClass,
		ERROR_STATS
	} from './shared.ts';
	import { runner } from './runner.svelte.ts';

	const STORAGE_KEY = 'magnet-calibration';
	const MODEL_VERSION = 2;
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
		modelVersion: number;
	}

	let text = $state('');
	let probes = $state<string[]>([]);
	let probeZeroInput = $state<number | null>(null);
	let model = $state<MagnetParams>({ ...NOMINAL_N52 });
	let loaded = $state(false);
	let copied = $state(false);

	onMount(() => {
		const saved = loadState<Saved>(STORAGE_KEY);
		if (saved) {
			text = saved.text ?? '';
			probes = saved.probes ?? [];
			probeZeroInput = saved.probeZeroInput ?? null;
			// Models saved before the N52 baseline (version 2) were a previous
			// magnet's calibration, so start those from nominal instead
			if (saved.modelVersion === MODEL_VERSION) model = { ...NOMINAL_N52, ...saved.model };
		}
		loaded = true;
	});

	$effect(() => {
		const snapshot = { text, probes, probeZeroInput, model, modelVersion: MODEL_VERSION };
		if (loaded) saveState(STORAGE_KEY, snapshot);
	});

	// A run started from the sidebar replaces the table, and the old probe readings with it
	$effect(() => {
		const result = runner.results.magnetometer;
		if (!loaded || result === null) return;
		text = result;
		probes = [];
		runner.results.magnetometer = null;
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

	const fitPoints = $derived<FitPoint[]>(
		rows.flatMap((row, i) =>
			trueMM[i] === null || row.bzMT <= 0 ? [] : [{ motionMM: trueMM[i]!, bzMT: row.bzMT }]
		)
	);
	const fit = $derived(modelValid ? fitCalibration(fitPoints, model) : null);
	const regression = $derived(fitRegression(fitPoints));

	// The fit is of raw readings, so it holds at the temperature of the run
	const calibrated = $derived<MagnetParams | null>(
		fit ? { ...model, ...fit, calibrationTempC: runTempC ?? model.calibrationTempC } : null
	);

	const nominalError = $derived(
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

	const regressionError = $derived(
		rows.map((row, i) =>
			trueMM[i] === null || !regression || row.bzMT <= 0
				? null
				: predictRegression(row.bzMT, regression) - trueMM[i]!
		)
	);

	const nominalStats = $derived(summarise(nominalError.filter((v) => v !== null)));
	const calibratedStats = $derived(summarise(calibratedError.filter((v) => v !== null)));
	const regressionStats = $derived(summarise(regressionError.filter((v) => v !== null)));

	const FACE_RANGE: [number, number] = [0, 15];
	const formatField = (value: number) => value.toPrecision(3);

	// The field charts use distance from the magnet face, so the datasheet curve
	// and the readings share an axis. A reading sits at its probe position (or
	// set distance until entered) plus that model's z offset.
	function measuredAtFace(zOffsetMM: number): Series {
		return {
			name: 'Measured',
			color: 'var(--series-2)',
			kind: 'points',
			data: rows.map((row, i) => ({ x: (trueMM[i] ?? row.setMM) + zOffsetMM, y: row.bzMT }))
		};
	}

	function faceCurve(
		name: string,
		color: string,
		params: MagnetParams,
		tempC: number | null
	): Series {
		const data = Array.from({ length: 151 }, (_, i) => {
			const x = FACE_RANGE[0] + ((FACE_RANGE[1] - FACE_RANGE[0]) * i) / 150;
			return { x, y: fieldAtFace(x, params, tempC) };
		});
		return { name, color, kind: 'line', data };
	}

	// The regression maps field to position, so its curve is traced by sweeping
	// the field across the fitted range only; the polynomial is wild outside it
	function regressionCurve(zOffsetMM: number): Series {
		const { minBzMT, maxBzMT } = regression!;
		const data = Array.from({ length: 101 }, (_, i) => {
			const y = minBzMT * (maxBzMT / minBzMT) ** (i / 100);
			return { x: predictRegression(y, regression!) + zOffsetMM, y };
		}).sort((a, b) => a.x - b.x);
		return { name: 'Regression', color: 'var(--series-4)', kind: 'line', data };
	}

	// One chart from the start: the nominal model is always there, and the readings
	// and calibrated model join once available. Readings sit at their probe
	// position plus the calibrated z offset once there's a fit, otherwise the nominal one.
	const placementOffset = $derived(calibrated?.zOffsetMM ?? model.zOffsetMM);
	const fieldChart = $derived<Series[]>([
		...(modelValid ? [faceCurve('Nominal model', 'var(--series-1)', model, runTempC)] : []),
		...(calibrated ? [faceCurve('Calibrated model', 'var(--series-3)', calibrated, runTempC)] : []),
		...(regression && modelValid ? [regressionCurve(placementOffset)] : []),
		...(rows.length && modelValid ? [measuredAtFace(placementOffset)] : [])
	]);
	const errorChart = $derived<Series[]>(
		calibrated
			? [
					{
						name: 'Nominal model',
						color: 'var(--series-1)',
						kind: 'points',
						data: rows.flatMap((_, i) =>
							nominalError[i] === null ? [] : [{ x: trueMM[i]!, y: nominalError[i]! }]
						)
					},
					{
						name: 'Calibrated model',
						color: 'var(--series-3)',
						kind: 'points',
						data: rows.flatMap((_, i) =>
							calibratedError[i] === null ? [] : [{ x: trueMM[i]!, y: calibratedError[i]! }]
						)
					},
					...(regression
						? [
								{
									name: 'Regression',
									color: 'var(--series-4)',
									kind: 'points' as const,
									data: rows.flatMap((_, i) =>
										regressionError[i] === null ? [] : [{ x: trueMM[i]!, y: regressionError[i]! }]
									)
								}
							]
						: [])
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

	<div class="columns">
		<div class="main-col">
			<section class="card">
				<h3>Field vs distance</h3>
				<Chart
					series={fieldChart}
					xLabel="Distance from magnet face (mm)"
					yLabel="B_z (mT, log)"
					xDomain={FACE_RANGE}
					logY
					formatY={formatField}
					height={380}
				/>
			</section>

			<section class="card">
				<h3>Firmware output</h3>
				<textarea bind:value={text} placeholder={PLACEHOLDER} rows="9" spellcheck="false"
				></textarea>
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
									<th>Nominal error (mm)</th>
									<th>Calibrated error (mm)</th>
									<th>Regression error (mm)</th>
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
										<td class={errorClass(nominalError[i])}>
											{nominalError[i] === null ? '–' : signedMM(nominalError[i]!)}
										</td>
										<td class={errorClass(calibratedError[i])}>
											{calibratedError[i] === null ? '–' : signedMM(calibratedError[i]!)}
										</td>
										<td class={errorClass(regressionError[i])}>
											{regressionError[i] === null ? '–' : signedMM(regressionError[i]!)}
										</td>
									</tr>
								{/each}
							</tbody>
						</table>
					</div>
				{/if}
			</section>

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
		</div>

		<aside class="side-col">
			<section class="card">
				<h3>Nominal model</h3>
				<div class="fields">
					<label
						>Diameter (mm)<input type="number" step="0.1" bind:value={model.diameterMM} /></label
					>
					<label
						>Thickness (mm)<input type="number" step="0.1" bind:value={model.thicknessMM} /></label
					>
					<label
						>Remanence (mT)<input type="number" step="1" bind:value={model.remanenceMT} /></label
					>
					<label
						>Z offset (mm)<input type="number" step="0.01" bind:value={model.zOffsetMM} /></label
					>
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
				<button class="secondary" onclick={() => (model = { ...NOMINAL_N52 })}>
					Reset to N52 nominal
				</button>
			</section>

			<section class="card">
				<h3>Error</h3>
				{#if nominalStats}
					<table class="error-stats">
						<thead>
							<tr>
								<th></th>
								<th>Nominal</th>
								<th>Calibrated</th>
								<th>Regression</th>
							</tr>
						</thead>
						<tbody>
							{#each ERROR_STATS as stat (stat.label)}
								<tr>
									<th scope="row">{stat.label}</th>
									<td class={statClass(stat, nominalStats)}>{stat.format(nominalStats)}</td>
									<td class={statClass(stat, calibratedStats)}>
										{calibratedStats ? stat.format(calibratedStats) : '–'}
									</td>
									<td class={statClass(stat, regressionStats)}>
										{regressionStats ? stat.format(regressionStats) : '–'}
									</td>
								</tr>
							{/each}
						</tbody>
					</table>
					{#if regression}
						<p class="note">
							Regression: position as a degree {regression.coefficients.length - 1} polynomial in ln(B<sub
								>z</sub
							>), least squares on position. Only valid at the run temperature and inside the
							measured range.
						</p>
					{/if}
				{:else}
					<p class="note">Enter probe readings to see the error.</p>
				{/if}
			</section>

			<section class="card">
				<div class="card-head">
					<h3>Calibrated constants</h3>
					{#if calibrated}
						<button onclick={copyCpp}>{copied ? 'Copied' : 'Copy'}</button>
					{/if}
				</div>
				{#if calibrated}
					<p class="note">Paste into <code>firmware/common/magnet.h</code>.</p>
					<pre><code>{cpp}</code></pre>
				{:else}
					<p class="note">Enter at least two probe readings to fit the remanence and z offset.</p>
				{/if}
			</section>
		</aside>
	</div>
</div>

<style>
	/* Wrap rather than cut long lines in the narrow column; Copy takes the raw text */
	pre {
		font-size: 12px;
		white-space: pre-wrap;
		overflow-wrap: anywhere;
	}
</style>
