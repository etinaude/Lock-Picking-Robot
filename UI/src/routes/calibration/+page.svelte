<script lang="ts">
	import { onMount } from 'svelte';
	import '#lib/calibration/calibration.css';
	import CurrentCalibration from '#lib/calibration/CurrentCalibration.svelte';
	import MagnetometerCalibration from '#lib/calibration/MagnetometerCalibration.svelte';
	import PidCalibration from '#lib/calibration/PidCalibration.svelte';
	import RepeatabilityCalibration from '#lib/calibration/RepeatabilityCalibration.svelte';
	import SerialMonitor from '#lib/serial/SerialMonitor.svelte';
	import { serial } from '#lib/serial/serial.svelte.ts';

	// Same order and letters as the calibration firmware's serial commands
	const MODES = [
		{ id: 'pid', command: 'P', label: 'PID tuning', component: PidCalibration },
		{ id: 'current', command: 'C', label: 'Motor current', component: CurrentCalibration },
		{
			id: 'magnetometer',
			command: 'M',
			label: 'Magnetometer',
			component: MagnetometerCalibration
		},
		{
			id: 'repeatability',
			command: 'R',
			label: 'Repeatability',
			component: RepeatabilityCalibration
		},
		{ id: 'serial', command: '>_', label: 'Serial monitor', component: SerialMonitor }
	];

	let modeId = $state('magnetometer');
	const mode = $derived(MODES.find((m) => m.id === modeId) ?? MODES[2]);

	// The selected mode lives in the URL hash so a refresh keeps it
	onMount(() => {
		const hash = location.hash.slice(1);
		if (MODES.some((m) => m.id === hash)) modeId = hash;
		serial.init(); // reconnects to a previously used port from any tab
	});

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
