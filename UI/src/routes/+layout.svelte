<script lang="ts">
	import favicon from '#lib/assets/favicon.svg';

	let { children } = $props();
</script>

<svelte:head>
	<link rel="icon" href={favicon} />
</svelte:head>

{@render children()}

<style>
	:global(:root) {
		color-scheme: light;
		--page: #f9f9f7;
		--surface-1: #fcfcfb;
		--text-primary: #0b0b0b;
		--text-secondary: #52514e;
		--text-muted: #898781;
		--gridline: #e1e0d9;
		--baseline: #c3c2b7;
		--border: rgba(11, 11, 11, 0.1);
		--hover: rgba(11, 11, 11, 0.05);
		--series-1: #2a78d6;
		--series-2: #eb6834;
		--series-3: #1baf7a;
		--series-4: #8b5cd6;
		--critical: #ff6c6c;
		--caution: #ffa938;
	}

	:global(body) {
		margin: 0;
		background: var(--page);
		color: var(--text-primary);
		font-family:
			system-ui,
			-apple-system,
			'Segoe UI',
			sans-serif;
	}
</style>
