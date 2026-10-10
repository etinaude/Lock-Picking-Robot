<svelte:head>
	<title>Lock Picking Robot</title>
</svelte:head>

<main>
	<h1>Lock Picking Robot</h1>
	<p class="soon">Coming soon</p>
	<p class="note">
		The control panel for picking locks is on its way. In the meantime, the bench tools are ready.
	</p>
	<a class="calibration" href="/calibration">Open calibration →</a>
</main>

<style>
	main {
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		gap: 12px;
		box-sizing: border-box;
		min-height: 100vh;
		max-width: 640px;
		margin: 0 auto;
		padding: 24px 16px;
		text-align: center;
	}

	h1 {
		margin: 0;
		font-size: 32px;
	}

	.soon {
		margin: 0;
		color: var(--series-1);
		font-size: 14px;
		font-weight: 600;
		letter-spacing: 0.12em;
		text-transform: uppercase;
	}

	.note {
		margin: 8px 0 0;
		color: var(--text-secondary);
		line-height: 1.5;
	}

	.calibration {
		margin-top: 8px;
		padding: 8px 14px;
		border: 1px solid var(--baseline);
		border-radius: 6px;
		color: var(--text-primary);
		font-size: 14px;
		text-decoration: none;
	}

	.calibration:hover {
		background: var(--hover);
	}
</style>
