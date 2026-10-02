<script lang="ts">
	import { onMount } from 'svelte';
	import Chart, { type Series } from '#lib/Chart.svelte';
	import { parseCalibration } from '#lib/magnet.ts';
	import {
		ERROR_STATS,
		loadState,
		mm,
		moveBetweenProbes,
		parseProbe,
		pasteColumn,
		saveState,
		signedMM,
		summarise,
		type Stats
	} from './shared.ts';
	import { runner } from './runner.svelte.ts';

	const STORAGE_KEY = 'repeatability-calibration';
	const PLACEHOLDER = `Set Distance (mm)  Magno reading(mT)  Predicted distance (mm)  time to target (ms)
3.00               12.7880            3.001                    842
10.00              1.8660             9.998                    1210
...

Temperature: 25.40 C`;
	const GROUP_COLORS = ['var(--series-1)', 'var(--series-2)', 'var(--series-3)'];

	interface Saved {
		text: string;
		probes: string[];
		probeZeroInput: number | null;
	}

	let text = $state('');
	let probes = $state<string[]>([]);
	let probeZeroInput = $state<number | null>(null);
	let loaded = $state(false);

	onMount(() => {
		const saved = loadState<Saved>(STORAGE_KEY);
		if (saved) {
			text = saved.text ?? '';
			probes = saved.probes ?? [];
			probeZeroInput = saved.probeZeroInput ?? null;
		}
		loaded = true;
	});

	$effect(() => {
		const snapshot = { text, probes, probeZeroInput };
		if (loaded) saveState(STORAGE_KEY, snapshot);
	});

	// A run started from the sidebar replaces the table, and the old probe readings with it
	$effect(() => {
		const result = runner.results.repeatability;
		if (!loaded || result === null) return;
		text = result;
		probes = [];
		runner.results.repeatability = null;
	});

	const parsed = $derived(parseCalibration(text));
	const rows = $derived(parsed.rows);
	const probeZero = $derived(probeZeroInput ?? rows[0]?.setMM ?? 0);

	const trueMM = $derived(
		rows.map((_, i) => {
			const reading = parseProbe(probes[i]);
			return reading === null ? null : probeZero + reading;
		})
	);
	const probeCount = $derived(trueMM.filter((v) => v !== null).length);
	const readingError = $derived(
		rows.map((row, i) => (trueMM[i] === null ? null : row.predictedMM - trueMM[i]!))
	);

	// Positions come from the probe once any are entered, otherwise from the
	// firmware's own readings (which can't show an error in the magnet model)
	const useProbe = $derived(probeCount > 0);

	// One group per set point, with each row's visit number within its group
	const groups = $derived.by(() => {
		const bySet = new Map<string, number[]>();
		rows.forEach((row, i) => {
			const key = row.setMM.toFixed(3);
			bySet.set(key, [...(bySet.get(key) ?? []), i]);
		});
		return [...bySet.values()].map((indices) => {
			const positions = useProbe
				? indices.flatMap((i) => (trueMM[i] === null ? [] : [trueMM[i]!]))
				: indices.map((i) => rows[i].predictedMM);
			const times = indices.flatMap((i) => (rows[i].timeMs === null ? [] : [rows[i].timeMs!]));
			return {
				label: `${rows[indices[0]].setMM.toFixed(2)} mm`,
				indices,
				position: summarise(positions),
				error: summarise(
					indices.flatMap((i) => (readingError[i] === null ? [] : [readingError[i]!]))
				),
				time: summarise(times),
				timeouts: indices.length - times.length
			};
		});
	});
	const visitNumber = $derived.by(() => {
		const visits: number[] = [];
		for (const group of groups) group.indices.forEach((i, visit) => (visits[i] = visit + 1));
		return visits;
	});

	// Error table columns: each set point, then all readings together
	const errorColumns = $derived([
		...groups.map((group) => ({ label: group.label, stats: group.error })),
		...(groups.length > 1
			? [{ label: 'All', stats: summarise(readingError.filter((v) => v !== null)) }]
			: [])
	]);
	const anyError = $derived(errorColumns.some((column) => column.stats));

	type Group = (typeof groups)[number];
	const stat = (stats: Stats | null, format: (stats: Stats) => string) =>
		stats ? format(stats) : '–';
	const REPEATABILITY_STATS: { label: string; format: (group: Group) => string }[] = [
		{ label: 'Visits', format: (group) => String(group.indices.length) },
		{ label: 'Spread (mm)', format: (group) => stat(group.position, (s) => s.range.toFixed(4)) },
		{ label: 'SD (mm)', format: (group) => stat(group.position, (s) => s.sampleStd.toFixed(4)) },
		{
			label: 'Variance (mm²)',
			format: (group) => stat(group.position, (s) => s.sampleVariance.toFixed(6))
		},
		{ label: 'Time mean (ms)', format: (group) => stat(group.time, (s) => s.mean.toFixed(0)) },
		{ label: 'Time max (ms)', format: (group) => stat(group.time, (s) => s.max.toFixed(0)) },
		{ label: 'Timeouts', format: (group) => String(group.timeouts) }
	];

	const deviationChart = $derived<Series[]>(
		groups.slice(0, GROUP_COLORS.length).map((group, g) => ({
			name: group.label,
			color: GROUP_COLORS[g],
			kind: 'points',
			data: group.indices.flatMap((i, visit) => {
				const value = useProbe ? trueMM[i] : rows[i].predictedMM;
				return value === null || !group.position
					? []
					: [{ x: visit + 1, y: value - group.position.mean }];
			})
		}))
	);
	const timeChart = $derived<Series[]>(
		groups.slice(0, GROUP_COLORS.length).map((group, g) => ({
			name: group.label,
			color: GROUP_COLORS[g],
			kind: 'points',
			data: group.indices.flatMap((i, visit) =>
				rows[i].timeMs === null ? [] : [{ x: visit + 1, y: rows[i].timeMs! }]
			)
		}))
	);

	function onProbePaste(event: ClipboardEvent, index: number) {
		const next = pasteColumn(event, probes, index, rows.length);
		if (next) probes = next;
	}

	const visit = (value: number) => value.toFixed(0);
	const ms = (value: number) => value.toFixed(0);
</script>

<div class="mode">
	<header>
		<h2>Repeatability</h2>
		<p class="note">
			Run <code>R</code> (or <code>R5</code> for 5 cycles) on the calibration firmware, paste its table,
			then type the probe reading at each stop.
		</p>
	</header>

	<div class="columns">
		<div class="main-col">
			<section class="card">
				<h3>Position at each visit</h3>
				{#if rows.length}
					<p class="note">
						Distance from that set point's mean position, measured by the
						{useProbe ? 'probe' : 'firmware until probe readings are entered'}.
					</p>
					<Chart
						series={deviationChart}
						xLabel="Visit"
						yLabel="Deviation (mm)"
						zeroLine
						formatX={visit}
						formatY={signedMM}
						height={340}
					/>
				{:else}
					<p class="note">Paste the firmware output to plot each visit.</p>
				{/if}
			</section>

			<section class="card">
				<h3>Firmware output</h3>
				<textarea bind:value={text} placeholder={PLACEHOLDER} rows="9" spellcheck="false"
				></textarea>
				<p class="note">
					{rows.length} rows · {groups.length} set point{groups.length === 1 ? '' : 's'}
					{#if parsed.temperatureC !== null}· {parsed.temperatureC.toFixed(2)} °C{/if}
					{#if rows.length && !parsed.hasTime}
						· <span class="warn">no time-to-target column, is this an M table?</span>
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
								Paste the firmware output above to get a row for each stop.
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
									<th>Visit</th>
									<th class="probe-col">Probe (mm)</th>
									<th>True (mm)</th>
									<th>Firmware predicted (mm)</th>
									<th>Reading error (mm)</th>
									<th>B<sub>z</sub> (mT)</th>
									<th>Time to target (ms)</th>
								</tr>
							</thead>
							<tbody>
								{#each rows as row, i (i)}
									<tr>
										<td class="muted">{i + 1}</td>
										<td>{row.setMM.toFixed(2)}</td>
										<td class="muted">{visitNumber[i]}</td>
										<td class="probe-col">
											<input
												class="probe"
												type="text"
												inputmode="decimal"
												placeholder="–"
												aria-label={`Probe reading at stop ${i + 1}, ${row.setMM} mm`}
												bind:value={probes[i]}
												onpaste={(event) => onProbePaste(event, i)}
												onkeydown={moveBetweenProbes}
											/>
										</td>
										<td>{trueMM[i] === null ? '–' : mm(trueMM[i]!)}</td>
										<td>{mm(row.predictedMM)}</td>
										<td>{readingError[i] === null ? '–' : signedMM(readingError[i]!)}</td>
										<td>{row.bzMT.toFixed(4)}</td>
										<td class:warn={row.timeMs === null}>{row.timeMs ?? 'timeout'}</td>
									</tr>
								{/each}
							</tbody>
						</table>
					</div>
				{/if}
			</section>

			{#if rows.length && parsed.hasTime}
				<section class="card">
					<h3>Time to target</h3>
					<p class="note">Timeouts are left out.</p>
					<Chart
						series={timeChart}
						xLabel="Visit"
						yLabel="Time (ms)"
						formatX={visit}
						formatY={ms}
					/>
				</section>
			{/if}
		</div>

		<aside class="side-col">
			<section class="card">
				<h3>Probe</h3>
				<div class="fields">
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
			</section>

			<section class="card">
				<h3>Error</h3>
				{#if anyError}
					<p class="note">Firmware reading minus probe position, in mm.</p>
					<table class="error-stats">
						<thead>
							<tr>
								<th></th>
								{#each errorColumns as column (column.label)}
									<th>{column.label}</th>
								{/each}
							</tr>
						</thead>
						<tbody>
							{#each ERROR_STATS as row (row.label)}
								<tr>
									<th scope="row">{row.label}</th>
									{#each errorColumns as column (column.label)}
										<td>{stat(column.stats, row.format)}</td>
									{/each}
								</tr>
							{/each}
						</tbody>
					</table>
				{:else}
					<p class="note">Enter probe readings to see the error.</p>
				{/if}
			</section>

			<section class="card">
				<h3>Repeatability</h3>
				{#if groups.length}
					<p class="note">
						Where the carriage stopped on each visit, from the {useProbe ? 'probe' : 'firmware'}.
					</p>
					<table class="error-stats">
						<thead>
							<tr>
								<th></th>
								{#each groups as group (group.label)}
									<th>{group.label}</th>
								{/each}
							</tr>
						</thead>
						<tbody>
							{#each REPEATABILITY_STATS as row (row.label)}
								<tr>
									<th scope="row">{row.label}</th>
									{#each groups as group (group.label)}
										<td class:warn={row.label === 'Timeouts' && group.timeouts > 0}>
											{row.format(group)}
										</td>
									{/each}
								</tr>
							{/each}
						</tbody>
					</table>
				{:else}
					<p class="note">Paste the firmware output to see the spread at each set point.</p>
				{/if}
			</section>
		</aside>
	</div>
</div>
