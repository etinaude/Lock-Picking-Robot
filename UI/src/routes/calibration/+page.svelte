<script lang="ts">
	import { onMount, type Component } from 'svelte';
	import '#lib/calibration/calibration.css';
	import CurrentCalibration from '#lib/calibration/CurrentCalibration.svelte';
	import MagnetometerCalibration from '#lib/calibration/MagnetometerCalibration.svelte';
	import PidCalibration from '#lib/calibration/PidCalibration.svelte';
	import RepeatabilityCalibration from '#lib/calibration/RepeatabilityCalibration.svelte';
	import SerialMonitor from '#lib/serial/SerialMonitor.svelte';
	import { serial } from '#lib/serial/serial.svelte.ts';
	import { runner, type RunMode } from '#lib/calibration/runner.svelte.ts';
	import { pidCommand, validGains } from '#lib/pid.ts';

	// Same order and letters as the calibration firmware's serial commands. `run`
	// is the tab a sidebar run fills, or 'unavailable' while the firmware lacks it.
	const MODES: {
		id: string;
		command: string;
		label: string;
		component: Component;
		run?: RunMode | 'unavailable';
	}[] = [
		{
			id: 'pid',
			command: 'P',
			label: 'PID tuning',
			component: PidCalibration,
			run: 'pid'
		},
		{
			id: 'current',
			command: 'C',
			label: 'Motor current',
			component: CurrentCalibration,
			run: 'unavailable'
		},
		{
			id: 'magnetometer',
			command: 'M',
			label: 'Magnetometer',
			component: MagnetometerCalibration,
			run: 'magnetometer'
		},
		{
			id: 'repeatability',
			command: 'R',
			label: 'Repeatability',
			component: RepeatabilityCalibration,
			run: 'repeatability'
		},
		{ id: 'serial', command: '>_', label: 'Serial monitor', component: SerialMonitor }
	];
	const DEFAULT_CYCLES = 10; // the firmware's R with no count

	let cycles = $state(DEFAULT_CYCLES);

	let modeId = $state('magnetometer');
	const mode = $derived(MODES.find((m) => m.id === modeId) ?? MODES[2]);

	// The selected mode lives in the URL hash so a refresh keeps it
	onMount(() => {
		const hash = location.hash.slice(1);
		if (MODES.some((m) => m.id === hash)) modeId = hash;
		serial.init(); // reconnects to a previously used port from any tab
	});

	const runCommand = $derived(
		mode.run === 'pid'
			? pidCommand(runner.pidGains)
			: mode.run === 'repeatability' && cycles !== DEFAULT_CYCLES
				? `R${cycles}`
				: mode.command
	);
	const runDisabled = $derived(
		mode.run === 'pid'
			? !validGains(runner.pidGains)
			: mode.run === 'repeatability' && (!cycles || cycles < 1 || cycles > 50)
	);

	function startRun() {
		if (mode.run && mode.run !== 'unavailable') runner.run(mode.run, runCommand);
	}

	function select(id: string) {
		modeId = id;
		history.replaceState(history.state, '', `#${id}`);
	}
</script>

<svelte:head>
	<title>Calibration</title>
</svelte:head>

<div class="calibration layout">
	<nav>
		<a class="home" href="/">← Home</a>
		<h1>Calibration</h1>
		<ul>
			{#each MODES as m (m.id)}
				<li class:tool={m.id === 'serial'}>
					<button
						class="nav-item"
						aria-current={m.id === modeId ? 'page' : undefined}
						onclick={() => select(m.id)}
					>
						<span class="command">{m.command}</span>
						{m.label}
						{#if m.id === 'serial'}
							<span
								class="dot"
								class:online={serial.connected}
								title={serial.connected ? 'Connected' : 'Offline'}
							></span>
						{/if}
					</button>
				</li>
			{/each}
		</ul>

		<!-- Runs the open tab's calibration on the board and fills in its table -->
		{#if runner.running || runner.message || (serial.connected && mode.run)}
			<section class="run" aria-live="polite">
				{#if runner.running}
					<p>
						<strong>Running {runner.running.command}</strong><br />
						{runner.running.points
							? `Point ${runner.running.points}, ${runner.running.last}`
							: 'Starting...'}
					</p>
					<button class="secondary" onclick={() => runner.cancel()}>Stop listening</button>
				{:else if serial.connected && mode.run === 'unavailable'}
					<p class="hint">{mode.label} isn't in the calibration firmware yet.</p>
				{:else if serial.connected && mode.run && !serial.calibrationFirmware}
					<p class="hint">
						Waiting to see the calibration firmware's menu. The arm firmware would read a command as
						a 0 mm target, so reset the board to check.
					</p>
					<button class="secondary" onclick={() => serial.reset()}>Reset board</button>
				{:else if serial.connected && mode.run}
					{#if mode.run === 'repeatability'}
						<label class="cycles">
							Cycles
							<input type="number" min="1" max="50" bind:value={cycles} />
						</label>
					{/if}
					{#if mode.run === 'pid'}
						<p class="hint">Gains from the PID tab.</p>
					{/if}
					<button onclick={startRun} disabled={runDisabled}>
						Run {runCommand}
					</button>
				{/if}
				{#if runner.message && !runner.running}
					<p class="message" class:error={runner.message.error}>{runner.message.text}</p>
				{/if}
			</section>
		{/if}
	</nav>

	<main>
		<mode.component />
	</main>
</div>

<style>
	.layout {
		display: grid;
		grid-template-columns: 220px minmax(0, 1fr);
		min-height: 100vh;
	}

	nav {
		position: sticky;
		top: 0;
		align-self: start;
		height: 100vh;
		box-sizing: border-box;
		padding: 24px 12px;
		border-right: 1px solid var(--border);
		background: var(--surface-1);
	}

	.home {
		padding: 0 8px;
		color: var(--text-secondary);
		font-size: 14px;
	}

	h1 {
		margin: 12px 8px 16px;
		font-size: 20px;
	}

	ul {
		display: flex;
		flex-direction: column;
		gap: 2px;
		margin: 0;
		padding: 0;
		list-style: none;
	}

	.nav-item {
		display: flex;
		align-items: center;
		gap: 10px;
		width: 100%;
		padding: 8px;
		border: 0;
		border-radius: 6px;
		background: transparent;
		color: var(--text-primary);
		font-size: 14px;
		text-align: left;
	}

	/* Tools sit below the calibration modes */
	li.tool {
		margin-top: 8px;
		padding-top: 8px;
		border-top: 1px solid var(--border);
	}

	.dot {
		width: 8px;
		height: 8px;
		margin-left: auto;
		border-radius: 50%;
		background: var(--baseline);
	}

	.dot.online {
		background: #0ca30c;
	}

	.run {
		display: flex;
		flex-direction: column;
		gap: 8px;
		margin-top: 16px;
		padding: 12px 8px 0;
		border-top: 1px solid var(--border);
		font-size: 13px;
	}

	.run p {
		margin: 0;
	}

	.run .hint {
		color: var(--text-secondary);
	}

	.run .message {
		color: var(--text-secondary);
	}

	.run .message.error {
		color: var(--critical);
	}

	.cycles {
		flex-direction: row;
		align-items: center;
		gap: 8px;
	}

	.cycles input {
		width: 70px;
		padding: 4px 6px;
	}

	.nav-item:hover {
		background: var(--hover);
	}

	.nav-item[aria-current='page'] {
		background: rgba(42, 120, 214, 0.1);
		font-weight: 600;
	}

	.command {
		display: inline-grid;
		place-items: center;
		width: 22px;
		height: 22px;
		border: 1px solid var(--baseline);
		border-radius: 4px;
		color: var(--text-secondary);
		font-family: ui-monospace, 'SFMono-Regular', Menlo, monospace;
		font-size: 12px;
		font-weight: 400;
	}

	main {
		max-width: 1500px;
		box-sizing: border-box;
		padding: 24px;
	}

	/* On narrow screens the sidebar becomes a row of tabs above the content */
	@media (max-width: 720px) {
		.layout {
			grid-template-columns: minmax(0, 1fr);
		}

		nav {
			position: static;
			height: auto;
			padding: 16px;
			border-right: 0;
			border-bottom: 1px solid var(--border);
		}

		ul {
			flex-direction: row;
			flex-wrap: wrap;
		}

		.nav-item {
			width: auto;
		}

		li.tool {
			margin-top: 0;
			padding-top: 0;
			border-top: 0;
		}

		main {
			padding: 16px;
		}
	}
</style>
