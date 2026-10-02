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
		--series-1: #2a78d6;
		--series-2: #eb6834;
		--series-3: #1baf7a;
		--critical: #d03b3b;
	}

	@media (prefers-color-scheme: dark) {
		:global(:root:where(:not([data-theme='light']))) {
			color-scheme: dark;
			--page: #0d0d0d;
			--surface-1: #1a1a19;
			--text-primary: #ffffff;
			--text-secondary: #c3c2b7;
			--text-muted: #898781;
			--gridline: #2c2c2a;
			--baseline: #383835;
			--border: rgba(255, 255, 255, 0.1);
			--series-1: #3987e5;
			--series-2: #d95926;
			--series-3: #199e70;
			--critical: #e66767;
		}
	}

	:global(:root[data-theme='dark']) {
		color-scheme: dark;
		--page: #0d0d0d;
		--surface-1: #1a1a19;
		--text-primary: #ffffff;
		--text-secondary: #c3c2b7;
		--text-muted: #898781;
		--gridline: #2c2c2a;
		--baseline: #383835;
		--border: rgba(255, 255, 255, 0.1);
		--series-1: #3987e5;
		--series-2: #d95926;
		--series-3: #199e70;
		--critical: #e66767;
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
