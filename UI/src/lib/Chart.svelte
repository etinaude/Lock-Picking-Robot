<script lang="ts" module>
	export interface Point {
		x: number;
		y: number;
	}

	export interface Series {
		name: string;
		color: string; // a CSS colour, normally one of the --series-* tokens
		kind: 'line' | 'points';
		data: Point[];
	}
</script>

<script lang="ts">
	interface Props {
		series: Series[];
		xLabel: string;
		yLabel: string;
		formatX?: (value: number) => string;
		formatY?: (value: number) => string;
		zeroLine?: boolean;
		height?: number;
		xDomain?: [number, number]; // fixed x range instead of fitting the data
		logY?: boolean; // for values spanning decades, e.g. a magnet's field
	}

	let {
		series,
		xLabel,
		yLabel,
		formatX = (value) => value.toFixed(2),
		formatY = (value) => value.toFixed(3),
		zeroLine = false,
		height = 300,
		xDomain,
		logY = false
	}: Props = $props();

	const uid = $props.id();
	const clipId = `plot-${uid}`;

	const margin = { top: 12, right: 16, bottom: 44, left: 56 };

	let width = $state(640);
	let hoverIndex = $state<number | null>(null);

	const plotWidth = $derived(Math.max(width - margin.left - margin.right, 10));
	const plotHeight = $derived(height - margin.top - margin.bottom);

	// Clean 1/2/5 steps across a padded domain
	function niceTicks(min: number, max: number, count: number) {
		if (!Number.isFinite(min) || !Number.isFinite(max)) return [0, 1];
		if (min === max) {
			min -= 1;
			max += 1;
		}
		const raw = (max - min) / count;
		const magnitude = 10 ** Math.floor(Math.log10(raw));
		const step = [1, 2, 5, 10].map((m) => m * magnitude).find((s) => s >= raw)!;
		const start = Math.floor(min / step) * step;
		const end = Math.ceil(max / step) * step;
		const ticks: number[] = [];
		for (let tick = start; tick <= end + step / 2; tick += step) ticks.push(+tick.toFixed(10));
		return ticks;
	}

	// 1/2/5 per decade, or just the decades when the range is wide
	function logTicks(min: number, max: number) {
		if (!Number.isFinite(min) || !Number.isFinite(max) || min <= 0) return [1, 10];
		const low = Math.floor(Math.log10(min));
		const high = Math.max(Math.ceil(Math.log10(max)), low + 1);
		const steps = high - low > 4 ? [1] : [1, 2, 5];
		const ticks: number[] = [];
		for (let decade = low; decade < high; decade++)
			for (const step of steps) ticks.push(+(step * 10 ** decade).toPrecision(2));
		ticks.push(10 ** high);
		return ticks;
	}

	const allPoints = $derived(series.flatMap((s) => s.data));
	const xTicks = $derived.by(() => {
		const ticks = niceTicks(
			xDomain?.[0] ?? Math.min(...allPoints.map((p) => p.x)),
			xDomain?.[1] ?? Math.max(...allPoints.map((p) => p.x)),
			Math.max(Math.floor(plotWidth / 90), 2)
		);
		// A fixed range is exact, so drop ticks the rounding pushed outside it
		return xDomain ? ticks.filter((t) => t >= xDomain[0] - 1e-9 && t <= xDomain[1] + 1e-9) : ticks;
	});
	const yTicks = $derived.by(() => {
		const ys = allPoints.map((p) => p.y);
		if (logY) {
			const positive = ys.filter((y) => y > 0);
			return logTicks(Math.min(...positive), Math.max(...positive));
		}
		if (zeroLine) ys.push(0);
		return niceTicks(Math.min(...ys), Math.max(...ys), 5);
	});

	// Ticks carry only as many decimals as their spacing needs
	const tickFormat = (ticks: number[]) => {
		const decimals = Math.max(0, -Math.floor(Math.log10(ticks[1] - ticks[0] || 1)));
		return (value: number) => value.toFixed(decimals);
	};
	const formatXTick = $derived(tickFormat(xTicks));
	const formatYTick = $derived(
		logY ? (value: number) => String(+value.toPrecision(2)) : tickFormat(yTicks)
	);

	const xMin = $derived(xDomain?.[0] ?? xTicks[0]);
	const xMax = $derived(xDomain?.[1] ?? xTicks[xTicks.length - 1]);
	const yMin = $derived(yTicks[0]);
	const yMax = $derived(yTicks[yTicks.length - 1]);

	const sx = (x: number) => ((x - xMin) / (xMax - xMin)) * plotWidth;
	const sy = (y: number) =>
		logY
			? plotHeight -
				((Math.log10(y) - Math.log10(yMin)) / (Math.log10(yMax) - Math.log10(yMin))) * plotHeight
			: plotHeight - ((y - yMin) / (yMax - yMin)) * plotHeight;
	// Log scales can't place zero or negative values
	const plottable = (data: Point[]) => (logY ? data.filter((p) => p.y > 0) : data);

	const linePath = (data: Point[]) =>
		plottable(data)
			.map((p, i) => `${i === 0 ? 'M' : 'L'}${sx(p.x).toFixed(1)},${sy(p.y).toFixed(1)}`)
			.join('');

	// The crosshair snaps to the measured x positions (the point series), or
	// along the first line when there are no points yet
	const snapXs = $derived.by(() => {
		const pointSeries = series.filter((s) => s.kind === 'points' && s.data.length);
		const source = pointSeries.length ? pointSeries : series.slice(0, 1);
		return [...new Set(source.flatMap((s) => s.data.map((p) => p.x)))].sort((a, b) => a - b);
	});

	function valueAt(s: Series, x: number): number | null {
		if (s.kind === 'points') return s.data.find((p) => Math.abs(p.x - x) < 1e-9)?.y ?? null;
		for (let i = 1; i < s.data.length; i++) {
			const a = s.data[i - 1];
			const b = s.data[i];
			if (x >= a.x && x <= b.x) return a.y + ((x - a.x) / (b.x - a.x)) * (b.y - a.y);
		}
		return null;
	}

	const hoverX = $derived(hoverIndex === null ? null : snapXs[hoverIndex]);

	function onPointerMove(event: PointerEvent) {
		if (snapXs.length === 0) return;
		const bounds = (event.currentTarget as SVGRectElement).getBoundingClientRect();
		const px = event.clientX - bounds.left;
		let best = 0;
		snapXs.forEach((x, i) => {
			if (Math.abs(sx(x) - px) < Math.abs(sx(snapXs[best]) - px)) best = i;
		});
		hoverIndex = best;
	}

	function onKeyDown(event: KeyboardEvent) {
		if (snapXs.length === 0) return;
		if (event.key === 'ArrowRight')
			hoverIndex = Math.min((hoverIndex ?? -1) + 1, snapXs.length - 1);
		else if (event.key === 'ArrowLeft') hoverIndex = Math.max((hoverIndex ?? 1) - 1, 0);
		else return;
		event.preventDefault();
	}
</script>

<div class="chart">
	{#if series.length > 1}
		<ul class="legend">
			{#each series as s (s.name)}
				<li>
					<svg width="16" height="10" aria-hidden="true">
						{#if s.kind === 'line'}
							<line x1="0" y1="5" x2="16" y2="5" stroke={s.color} stroke-width="2" />
						{:else}
							<circle cx="8" cy="5" r="4" fill={s.color} />
						{/if}
					</svg>
					{s.name}
				</li>
			{/each}
		</ul>
	{/if}

	<div class="frame" bind:clientWidth={width}>
		<!-- Focusable so the arrow keys can step the tooltip through the points -->
		<!-- svelte-ignore a11y_no_noninteractive_tabindex, a11y_no_noninteractive_element_interactions -->
		<svg
			{width}
			{height}
			role="img"
			aria-label={`${yLabel} against ${xLabel}. Use the arrow keys to step through points.`}
			tabindex="0"
			onkeydown={onKeyDown}
			onblur={() => (hoverIndex = null)}
		>
			<defs>
				<!-- Marks stay inside the plot when the x range is fixed; points get room for their radius -->
				<clipPath id={clipId}>
					<rect x="-8" y="-8" width={plotWidth + 16} height={plotHeight + 16} />
				</clipPath>
			</defs>
			<g transform={`translate(${margin.left},${margin.top})`}>
				{#each yTicks as tick (tick)}
					<line class="grid" x1="0" x2={plotWidth} y1={sy(tick)} y2={sy(tick)} />
					<text class="tick" x="-8" y={sy(tick)} text-anchor="end" dominant-baseline="middle">
						{formatYTick(tick)}
					</text>
				{/each}
				{#if zeroLine && !logY}
					<line class="baseline" x1="0" x2={plotWidth} y1={sy(0)} y2={sy(0)} />
				{/if}
				{#each xTicks as tick (tick)}
					<text class="tick" x={sx(tick)} y={plotHeight + 18} text-anchor="middle">
						{formatXTick(tick)}
					</text>
				{/each}
				<line class="baseline" x1="0" x2={plotWidth} y1={plotHeight} y2={plotHeight} />

				<text class="axis-label" x={plotWidth / 2} y={plotHeight + 38} text-anchor="middle">
					{xLabel}
				</text>
				<text
					class="axis-label"
					transform={`translate(${-margin.left + 14},${plotHeight / 2}) rotate(-90)`}
					text-anchor="middle"
				>
					{yLabel}
				</text>

				{#if hoverX !== null}
					<line class="crosshair" x1={sx(hoverX)} x2={sx(hoverX)} y1="0" y2={plotHeight} />
				{/if}

				<g clip-path={`url(#${clipId})`}>
					{#each series as s (s.name)}
						{#if s.kind === 'line'}
							<path d={linePath(s.data)} fill="none" stroke={s.color} stroke-width="2" />
						{/if}
					{/each}
				</g>
				<g clip-path={`url(#${clipId})`}>
					{#each series as s (s.name)}
						{#if s.kind === 'points'}
							{#each plottable(s.data) as p, i (i)}
								<circle
									cx={sx(p.x)}
									cy={sy(p.y)}
									r={hoverX !== null && Math.abs(p.x - hoverX) < 1e-9 ? 6 : 4}
									fill={s.color}
									stroke="var(--surface-1)"
									stroke-width="2"
								/>
							{/each}
						{/if}
					{/each}
				</g>

				<rect
					class="hit"
					width={plotWidth}
					height={plotHeight}
					role="presentation"
					onpointermove={onPointerMove}
					onpointerleave={() => (hoverIndex = null)}
				/>
			</g>
		</svg>

		{#if hoverX !== null}
			<div
				class="tooltip"
				style:left={`${margin.left + sx(hoverX)}px`}
				style:transform={sx(hoverX) > plotWidth / 2
					? 'translateX(calc(-100% - 12px))'
					: 'translateX(12px)'}
			>
				<div class="tooltip-x">{xLabel}: {formatX(hoverX)}</div>
				{#each series as s (s.name)}
					{@const value = valueAt(s, hoverX)}
					{#if value !== null}
						<div class="tooltip-row">
							<svg width="12" height="8" aria-hidden="true">
								<line x1="0" y1="4" x2="12" y2="4" stroke={s.color} stroke-width="2" />
							</svg>
							<strong>{formatY(value)}</strong>
							<span>{s.name}</span>
						</div>
					{/if}
				{/each}
			</div>
		{/if}
	</div>
</div>

<style>
	.chart {
		display: flex;
		flex-direction: column;
		gap: 8px;
	}

	.legend {
		display: flex;
		flex-wrap: wrap;
		gap: 4px 16px;
		margin: 0;
		padding: 0;
		list-style: none;
		font-size: 13px;
		color: var(--text-secondary);
	}

	.legend li {
		display: flex;
		align-items: center;
		gap: 6px;
	}

	.frame {
		position: relative;
		width: 100%;
	}

	svg {
		display: block;
		overflow: visible;
	}

	svg:focus-visible {
		outline: 2px solid var(--series-1);
		outline-offset: 4px;
		border-radius: 4px;
	}

	.grid {
		stroke: var(--gridline);
		stroke-width: 1;
	}

	.baseline {
		stroke: var(--baseline);
		stroke-width: 1;
	}

	.crosshair {
		stroke: var(--text-muted);
		stroke-width: 1;
	}

	.tick {
		fill: var(--text-muted);
		font-size: 12px;
		font-variant-numeric: tabular-nums;
	}

	.axis-label {
		fill: var(--text-secondary);
		font-size: 12px;
	}

	.hit {
		fill: transparent;
		cursor: crosshair;
	}

	.tooltip {
		position: absolute;
		top: 12px;
		pointer-events: none;
		padding: 8px 10px;
		border-radius: 6px;
		background: var(--surface-1);
		border: 1px solid var(--border);
		box-shadow: 0 4px 12px rgb(0 0 0 / 0.12);
		font-size: 12px;
		white-space: nowrap;
		color: var(--text-secondary);
	}

	.tooltip-x {
		margin-bottom: 4px;
	}

	.tooltip-row {
		display: flex;
		align-items: center;
		gap: 6px;
	}

	.tooltip-row strong {
		color: var(--text-primary);
		font-variant-numeric: tabular-nums;
	}
</style>
