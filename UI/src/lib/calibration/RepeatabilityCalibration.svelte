<script lang="ts">
	import { onMount } from 'svelte';
	import Chart, { type Series } from '#lib/Chart.svelte';
	import { parseCalibration } from '#lib/magnet.ts';
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

	// One group per set point, with each row's visit number within its group
	const groups = $derived.by(() => {
		const bySet = new Map<string, number[]>();
		rows.forEach((row, i) => {
			const key = row.setMM.toFixed(3);
			bySet.set(key, [...(bySet.get(key) ?? []), i]);
		});
		return [...bySet.values()].map((indices) => {
			const setMM = rows[indices[0]].setMM;
			const probed = indices.filter((i) => trueMM[i] !== null);
			const times = indices.map((i) => rows[i].timeMs).filter((t) => t !== null);
			return {
				setMM,
				indices,
				probe: summarise(probed.map((i) => trueMM[i]!)),
				predicted: summarise(indices.map((i) => rows[i].predictedMM)),
				error: summarise(probed.map((i) => readingError[i]!)),
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

	// Probe positions once any are entered, otherwise the firmware's own readings
	const useProbe = $derived(probeCount > 0);
	const deviationChart = $derived<Series[]>(
		groups.slice(0, GROUP_COLORS.length).map((group, g) => {
			const mean = useProbe ? group.probe?.mean : group.predicted?.mean;
			return {
				name: `${group.setMM.toFixed(2)} mm`,
				color: GROUP_COLORS[g],
				kind: 'points',
				data: group.indices.flatMap((i, visit) => {
					const value = useProbe ? trueMM[i] : rows[i].predictedMM;
					return value === null || mean === undefined ? [] : [{ x: visit + 1, y: value - mean }];
				})
			};
		})
	);
	const timeChart = $derived<Series[]>(
		groups.slice(0, GROUP_COLORS.length).map((group, g) => ({
			name: `${group.setMM.toFixed(2)} mm`,
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

	<div class="split">
		<section class="card">
			<h3>Firmware output</h3>
			<textarea bind:value={text} placeholder={PLACEHOLDER} rows="9" spellcheck="false"></textarea>
			<p class="note">
				{rows.length} rows · {groups.length} set point{groups.length === 1 ? '' : 's'}
				{#if parsed.temperatureC !== null}· {parsed.temperatureC.toFixed(2)} °C{/if}
				{#if rows.length && !parsed.hasTime}
					· <span class="warn">no time-to-target column, is this an M table?</span>
				{/if}
			</p>
		</section>

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
			<p class="note">
				Probe readings are optional. Without them the spread comes from the firmware's own distance
				readings, which hides any error in the magnet model.
			</p>
		</section>
	</div>

	{#if groups.length}
		<section class="card">
			<h3>Summary</h3>
			<div class="table-wrap">
				<table>
					<thead>
						<tr>
							<th>Set (mm)</th>
							<th>Visits</th>
							<th>Probe spread (mm)</th>
							<th>Probe σ (mm)</th>
							<th>Firmware spread (mm)</th>
							<th>Firmware σ (mm)</th>
							<th>Reading error (mm)</th>
							<th>Time mean / max (ms)</th>
							<th>Timeouts</th>
						</tr>
					</thead>
					<tbody>
						{#each groups as group (group.setMM)}
							<tr>
								<td>{group.setMM.toFixed(2)}</td>
								<td>{group.indices.length}</td>
								<td>{group.probe ? mm(group.probe.range) : '–'}</td>
								<td>{group.probe ? mm(group.probe.std) : '–'}</td>
								<td>{group.predicted ? mm(group.predicted.range) : '–'}</td>
								<td>{group.predicted ? mm(group.predicted.std) : '–'}</td>
								<td>
									{group.error
										? `${signedMM(group.error.mean)} ± ${group.error.std.toFixed(3)}`
										: '–'}
								</td>
								<td>
									{group.time ? `${ms(group.time.mean)} / ${ms(group.time.max)}` : '–'}
								</td>
								<td class:warn={group.timeouts > 0}>{group.timeouts}</td>
							</tr>
						{/each}
					</tbody>
				</table>
			</div>
		</section>
	{/if}

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

	{#if rows.length}
		<section class="card">
			<h3>Position at each visit</h3>
			<p class="note">
				Distance from that set point's mean position, measured by the
				{useProbe ? 'probe' : 'firmware (enter probe readings to use the probe)'}.
			</p>
			<Chart
				series={deviationChart}
				xLabel="Visit"
				yLabel="Deviation (mm)"
				zeroLine
				formatX={visit}
				formatY={signedMM}
			/>
		</section>

		{#if parsed.hasTime}
			<section class="card">
				<h3>Time to target</h3>
				<p class="note">Timeouts are left out.</p>
				<Chart series={timeChart} xLabel="Visit" yLabel="Time (ms)" formatX={visit} formatY={ms} />
			</section>
		{/if}
	{/if}
</div>
