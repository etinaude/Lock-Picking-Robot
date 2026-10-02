<script lang="ts">
	import { onMount, untrack } from 'svelte';
	import Chart, { type Series } from '#lib/Chart.svelte';
	import {
		DEFAULT_GAINS,
		SAMPLE_MS,
		formatGain,
		parsePidRun,
		stepMetrics,
		summariseRun,
		validGains,
		type PidGains
	} from '#lib/pid.ts';
	import { errorClass, loadState, mm, saveState, signedMM } from './shared.ts';
	import { runner } from './runner.svelte.ts';

	const STORAGE_KEY = 'pid-calibration';
	const MAX_RUNS = 10; // each run is a few thousand lines of output
	const RUN_COLORS = ['var(--series-1)', 'var(--series-2)', 'var(--series-3)', 'var(--series-4)'];
	const PLACEHOLDER = `PID step test  Kp 3.0000  Ki 0.1000  Kd 1.0000  Deadband 0.050 mm  Travel 3.00-10.00 mm
Step  Target (mm)  Time (ms)  Distance (mm)  PWM    Current (mA)
1     3.00         0          6.012          0      41.2
1     3.00         20         5.987          -180   95.0
...

Temperature: 25.40 C`;

	interface StoredRun {
		id: number; // also the run's number in the UI
		time: string;
		text: string;
	}

	interface Saved {
		runs: StoredRun[];
		selectedId: number | null;
		compareIds: number[];
		gains: PidGains;
	}

	let stored = $state<StoredRun[]>([]);
	let selectedId = $state<number | null>(null);
	let compareIds = $state<number[]>([]);
	let pasted = $state('');
	let stepIndex = $state(0);
	let loaded = $state(false);
	let copied = $state(false);

	onMount(() => {
		const saved = loadState<Saved>(STORAGE_KEY);
		if (saved) {
			stored = saved.runs ?? [];
			selectedId = saved.selectedId ?? null;
			compareIds = saved.compareIds ?? [];
			runner.pidGains = { ...DEFAULT_GAINS, ...saved.gains };
		}
		loaded = true;
	});

	$effect(() => {
		const snapshot = { runs: stored, selectedId, compareIds, gains: runner.pidGains };
		if (loaded) saveState(STORAGE_KEY, snapshot);
	});

	// A run started from the sidebar becomes the newest run
	$effect(() => {
		const result = runner.results.pid;
		if (!loaded || result === null) return;
		untrack(() => addRun(result));
		runner.results.pid = null;
	});

	function addRun(text: string) {
		const id = Math.max(0, ...stored.map((run) => run.id)) + 1;
		stored = [{ id, time: new Date().toLocaleString(), text }, ...stored].slice(0, MAX_RUNS);
		selectedId = id;
		// Shown against the runs already being compared, dropping the oldest
		compareIds = [
			id,
			...compareIds.filter((other) => stored.some((run) => run.id === other))
		].slice(0, RUN_COLORS.length);
	}

	function addPasted() {
		addRun(pasted);
		pasted = '';
	}

	function deleteRun(id: number) {
		stored = stored.filter((run) => run.id !== id);
		compareIds = compareIds.filter((other) => other !== id);
		if (selectedId === id) selectedId = stored[0]?.id ?? null;
	}

	function toggleCompare(id: number) {
		compareIds = compareIds.includes(id)
			? compareIds.filter((other) => other !== id)
			: [...compareIds, id];
	}

	const runs = $derived(
		stored.map((run) => {
			const parsed = parsePidRun(run.text);
			const metrics = parsed.steps.map((step) => stepMetrics(step, parsed));
			return { ...run, parsed, metrics, summary: summariseRun(metrics) };
		})
	);
	type Run = (typeof runs)[number];

	const selected = $derived(runs.find((run) => run.id === selectedId) ?? runs[0] ?? null);
	const compared = $derived(
		compareIds.flatMap((id) => runs.filter((run) => run.id === id)).slice(0, RUN_COLORS.length)
	);
	const pastedRun = $derived(parsePidRun(pasted));

	const gainsLabel = (gains: PidGains | null) =>
		gains
			? `Kp ${formatGain(gains.kp)} · Ki ${formatGain(gains.ki)} · Kd ${formatGain(gains.kd)}`
			: 'gains unknown';
	const runLabel = (run: Run) => `Run ${run.id} (${gainsLabel(run.parsed.gains)})`;

	// The whole run on one time axis, each step following the last
	const timeline = $derived.by(() => {
		const distance: Series['data'] = [];
		const target: Series['data'] = [];
		const pwm: Series['data'] = [];
		let offsetMs = 0;
		for (const step of selected?.parsed.steps ?? []) {
			for (const sample of step.samples) {
				const x = (offsetMs + sample.timeMs) / 1000;
				distance.push({ x, y: sample.distanceMM });
				pwm.push({ x, y: sample.pwm });
			}
			const endMs = offsetMs + step.samples[step.samples.length - 1].timeMs + SAMPLE_MS;
			target.push({ x: offsetMs / 1000, y: step.targetMM }, { x: endMs / 1000, y: step.targetMM });
			offsetMs = endMs;
		}
		return {
			position: distance.length
				? ([
						{ name: 'Distance', color: 'var(--series-1)', kind: 'line', data: distance },
						{ name: 'Target', color: 'var(--series-2)', kind: 'line', data: target }
					] satisfies Series[])
				: [],
			drive: pwm.length
				? ([{ name: 'PWM', color: 'var(--series-3)', kind: 'line', data: pwm }] satisfies Series[])
				: []
		};
	});

	// Steps of the selected run, for picking which one to compare
	const stepCount = $derived(selected?.parsed.steps.length ?? 0);
	const stepNumber = $derived(Math.min(stepIndex, Math.max(stepCount - 1, 0)));

	const compareChart = $derived<Series[]>(
		compared.flatMap((run, i) => {
			const step = run.parsed.steps[stepNumber];
			if (!step) return [];
			return [
				{
					name: runLabel(run),
					color: RUN_COLORS[i],
					kind: 'line',
					data: step.samples.map((sample) => ({
						x: sample.timeMs,
						y: sample.distanceMM - step.targetMM
					}))
				}
			];
		})
	);

	const ms = (value: number | null) => (value === null ? '–' : value.toFixed(0));
	const seconds = (value: number) => `${value.toFixed(1)} s`;

	const cpp = $derived.by(() => {
		const gains = selected?.parsed.gains;
		if (!gains) return '';
		const [kp, ki, kd] = [gains.kp, gains.ki, gains.kd].map((value) => value.toFixed(4));
		return [
			`const double DEFAULT_KP = ${kp};`,
			`const double DEFAULT_KI = ${ki};`,
			`const double DEFAULT_KD = ${kd};`,
			'',
			'// or at runtime:',
			`// motor.setPIDCalibration(${kp}, ${ki}, ${kd});`
		].join('\n');
	});

	async function copyCpp() {
		try {
			await navigator.clipboard.writeText(cpp);
			copied = true;
			setTimeout(() => (copied = false), 1500);
		} catch {
			// clipboard blocked; the text is selectable in the block
		}
	}
</script>

<div class="mode">
	<header>
		<h2>PID tuning</h2>
		<p class="note">
			Set the gains, then run <code>P</code> from the sidebar. It steps 6 → 3 → 10 → 6 → 6.5 → 6 mm
			and logs what the PID sees every {SAMPLE_MS} ms. Compare runs to pick the gains. 3 and 10 mm are
			the travel limits: the motor creeps over the last 1 mm towards one and never drives past it.
		</p>
	</header>

	<div class="columns">
		<div class="main-col">
			<section class="card">
				<h3>Response{selected ? `: ${runLabel(selected)}` : ''}</h3>
				{#if timeline.position.length}
					<p class="note">
						Distance is the rolling average the PID runs on, so the real carriage leads it slightly.
					</p>
					<Chart
						series={timeline.position}
						xLabel="Time (s)"
						yLabel="Distance (mm)"
						formatX={seconds}
						height={320}
					/>
					<h3 class="chart-title">Drive</h3>
					<p class="note">
						Signed PWM; positive drives towards larger distances. Anything driven starts at the
						firmware's minimum PWM.
					</p>
					<Chart
						series={timeline.drive}
						xLabel="Time (s)"
						yLabel="PWM"
						formatX={seconds}
						formatY={(value) => value.toFixed(0)}
						zeroLine
						height={200}
					/>
				{:else}
					<p class="note">Run <code>P</code> from the sidebar, or paste its output below.</p>
				{/if}
			</section>

			{#if selected?.metrics.length}
				<section class="card">
					<h3>Steps</h3>
					<p class="note">
						Settle is when it entered the ±{selected.parsed.deadbandMM.toFixed(2)} mm deadband for good;
						crossings are swings right through the deadband.
					</p>
					{#if selected.summary.maxPastLimitMM > 0}
						<p class="note warn">
							Went {mm(selected.summary.maxPastLimitMM)} mm past the
							{selected.parsed.travelMM[0]}–{selected.parsed.travelMM[1]} mm travel. Check the carriage
							isn't jammed, and widen <code>LIMIT_SLOW_ZONE_MM</code> or lower
							<code>LIMIT_APPROACH_PWM</code> in <code>config.h</code>.
						</p>
					{/if}
					<div class="table-wrap">
						<table>
							<thead>
								<tr>
									<th>#</th>
									<th>Move (mm)</th>
									<th>Reach (ms)</th>
									<th>Rise 10–90% (ms)</th>
									<th>Settle (ms)</th>
									<th>Overshoot (mm)</th>
									<th>Final error (mm)</th>
									<th>Crossings</th>
									<th>Peak current (mA)</th>
								</tr>
							</thead>
							<tbody>
								{#each selected.metrics as step, i (i)}
									<tr>
										<td class="muted">{i + 1}</td>
										<td>{step.fromMM.toFixed(2)} → {step.toMM.toFixed(2)}</td>
										<td>{ms(step.reachMs)}</td>
										<td>{ms(step.riseMs)}</td>
										<td class:warn={step.settleMs === null}>
											{step.settleMs === null ? 'timeout' : ms(step.settleMs)}
										</td>
										<td class={errorClass(step.overshootMM)}>{mm(step.overshootMM)}</td>
										<td class={errorClass(step.finalErrorMM)}>{signedMM(step.finalErrorMM)}</td>
										<td class:warn={step.crossings > 1}>{step.crossings}</td>
										<td>{step.peakCurrentMA.toFixed(0)}</td>
									</tr>
								{/each}
							</tbody>
						</table>
					</div>
				</section>
			{/if}

			{#if compared.length && stepCount}
				<section class="card">
					<h3>Compare runs</h3>
					<p class="note">
						Distance from the target after each step's command, for the runs ticked below.
					</p>
					<div class="steps" role="group" aria-label="Step to compare">
						{#each selected!.parsed.steps as step, i (i)}
							<button
								class="secondary"
								aria-pressed={i === stepNumber}
								onclick={() => (stepIndex = i)}
							>
								{i + 1}: → {step.targetMM.toFixed(1)} mm
							</button>
						{/each}
					</div>
					<Chart
						series={compareChart}
						xLabel="Time since the step (ms)"
						yLabel="Distance from target (mm)"
						formatX={(value) => value.toFixed(0)}
						formatY={signedMM}
						zeroLine
						height={320}
					/>
				</section>
			{/if}

			{#if runs.length}
				<section class="card">
					<div class="card-head">
						<div>
							<h3>Runs</h3>
							<p class="note">
								Newest first; the last {MAX_RUNS} are kept. Pick one to view, tick up to {RUN_COLORS.length}
								to compare.
							</p>
						</div>
						<button
							class="secondary"
							onclick={() => {
								stored = [];
								compareIds = [];
								selectedId = null;
							}}>Clear runs</button
						>
					</div>
					<div class="table-wrap">
						<table>
							<thead>
								<tr>
									<th>Compare</th>
									<th class="left">Run</th>
									<th>Kp</th>
									<th>Ki</th>
									<th>Kd</th>
									<th>Total settle (ms)</th>
									<th>Worst overshoot (mm)</th>
									<th>Worst final error (mm)</th>
									<th>Timeouts</th>
									<th>Past limit (mm)</th>
									<th></th>
								</tr>
							</thead>
							<tbody>
								{#each runs as run (run.id)}
									{@const gains = run.parsed.gains}
									<tr class:selected={run.id === selected?.id}>
										<td>
											<input
												type="checkbox"
												class="compare"
												aria-label={`Compare run ${run.id}`}
												checked={compareIds.includes(run.id)}
												disabled={!compareIds.includes(run.id) &&
													compared.length >= RUN_COLORS.length}
												onchange={() => toggleCompare(run.id)}
											/>
										</td>
										<td class="left">
											<button class="link" onclick={() => (selectedId = run.id)}
												>Run {run.id}</button
											>
											<span class="muted">{run.time}</span>
										</td>
										<td>{gains ? formatGain(gains.kp) : '–'}</td>
										<td>{gains ? formatGain(gains.ki) : '–'}</td>
										<td>{gains ? formatGain(gains.kd) : '–'}</td>
										<td>{ms(run.summary.totalSettleMs)}</td>
										<td class={errorClass(run.summary.maxOvershootMM)}>
											{mm(run.summary.maxOvershootMM)}
										</td>
										<td class={errorClass(run.summary.maxFinalErrorMM)}>
											{mm(run.summary.maxFinalErrorMM)}
										</td>
										<td class:warn={run.summary.timeouts > 0}>{run.summary.timeouts}</td>
										<td class:warn={run.summary.maxPastLimitMM > 0}>
											{mm(run.summary.maxPastLimitMM)}
										</td>
										<td>
											<div class="actions">
												<button
													class="secondary"
													disabled={!gains}
													onclick={() => gains && (runner.pidGains = { ...gains })}
													>Use gains</button
												>
												<button class="secondary" onclick={() => deleteRun(run.id)}>Delete</button>
											</div>
										</td>
									</tr>
								{/each}
							</tbody>
						</table>
					</div>
				</section>
			{/if}

			<section class="card">
				<h3>Firmware output</h3>
				<p class="note">For a run done from another serial monitor: paste its output and add it.</p>
				<textarea bind:value={pasted} placeholder={PLACEHOLDER} rows="7" spellcheck="false"
				></textarea>
				<div class="card-head">
					<p class="note">
						{#if pasted.trim()}
							{pastedRun.steps.length} steps · {gainsLabel(pastedRun.gains)}
						{:else}
							Waiting for a table
						{/if}
					</p>
					<button onclick={addPasted} disabled={!pastedRun.steps.length}>Add run</button>
				</div>
			</section>
		</div>

		<aside class="side-col">
			<section class="card">
				<h3>Gains</h3>
				<div class="fields">
					<label>Kp<input type="number" step="0.1" min="0" bind:value={runner.pidGains.kp} /></label
					>
					<label
						>Ki<input type="number" step="0.01" min="0" bind:value={runner.pidGains.ki} /></label
					>
					<label>Kd<input type="number" step="0.1" min="0" bind:value={runner.pidGains.kd} /></label
					>
				</div>
				{#if !validGains(runner.pidGains)}
					<p class="note warn">Gains must be numbers of 0 or more.</p>
				{/if}
				<p class="note">
					The run sends <code>P &lt;Kp&gt; &lt;Ki&gt; &lt;Kd&gt;</code>. The board keeps them, for M
					and R runs too, until it resets.
				</p>
				<button class="secondary" onclick={() => (runner.pidGains = { ...DEFAULT_GAINS })}>
					Reset to firmware defaults
				</button>
			</section>

			<section class="card">
				<h3>Summary</h3>
				{#if selected}
					<table class="error-stats">
						<tbody>
							<tr>
								<th scope="row">Gains</th>
								<td>{gainsLabel(selected.parsed.gains)}</td>
							</tr>
							<tr>
								<th scope="row">Total settle</th>
								<td>{ms(selected.summary.totalSettleMs)} ms</td>
							</tr>
							<tr>
								<th scope="row">Worst overshoot</th>
								<td class={errorClass(selected.summary.maxOvershootMM)}>
									{mm(selected.summary.maxOvershootMM)} mm
								</td>
							</tr>
							<tr>
								<th scope="row">Worst final error</th>
								<td class={errorClass(selected.summary.maxFinalErrorMM)}>
									{mm(selected.summary.maxFinalErrorMM)} mm
								</td>
							</tr>
							<tr>
								<th scope="row">Timeouts</th>
								<td class:warn={selected.summary.timeouts > 0}>{selected.summary.timeouts}</td>
							</tr>
							<tr>
								<th scope="row">Past travel limit</th>
								<td class:warn={selected.summary.maxPastLimitMM > 0}>
									{mm(selected.summary.maxPastLimitMM)} mm
								</td>
							</tr>
							{#if selected.parsed.temperatureC !== null}
								<tr>
									<th scope="row">Temperature</th>
									<td>{selected.parsed.temperatureC.toFixed(2)} °C</td>
								</tr>
							{/if}
						</tbody>
					</table>
				{:else}
					<p class="note">No runs yet.</p>
				{/if}
			</section>

			<section class="card">
				<div class="card-head">
					<h3>Firmware constants</h3>
					{#if cpp}
						<button onclick={copyCpp}>{copied ? 'Copied' : 'Copy'}</button>
					{/if}
				</div>
				{#if cpp}
					<p class="note">
						The viewed run's gains. Paste into <code>firmware/common/motor.h</code>.
					</p>
					<pre><code>{cpp}</code></pre>
				{:else}
					<p class="note">View a run to get its gains as constants.</p>
				{/if}
			</section>
		</aside>
	</div>
</div>

<style>
	.chart-title {
		margin-top: 20px;
	}

	.steps {
		display: flex;
		flex-wrap: wrap;
		gap: 6px;
		margin-bottom: 12px;
	}

	.steps button[aria-pressed='true'] {
		border-color: var(--series-1);
		background: rgba(42, 120, 214, 0.1);
		font-weight: 600;
	}

	th.left,
	td.left {
		text-align: left;
	}

	tr.selected {
		background: rgba(42, 120, 214, 0.08);
	}

	input.compare {
		width: auto;
	}

	button.link {
		padding: 0;
		border: 0;
		background: none;
		color: var(--series-1);
		font-weight: 600;
		text-decoration: underline;
	}

	td.left .muted {
		margin-left: 8px;
		font-size: 13px;
	}

	.actions {
		display: flex;
		gap: 6px;
		justify-content: flex-end;
	}

	textarea + .card-head {
		align-items: center;
		margin-top: 8px;
	}

	textarea + .card-head .note {
		margin: 0;
	}

	button:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	pre {
		font-size: 12px;
		white-space: pre-wrap;
		overflow-wrap: anywhere;
	}
</style>
