import { Box } from "@chakra-ui/react";

export type RatePoint = { tMs: number; hz: number };
export type TrendPoint = { tMs: number; value: number };

function pointValue(point: RatePoint | TrendPoint): number {
  return "value" in point ? point.value : point.hz;
}

function buildPolyline(points: Array<RatePoint | TrendPoint>): { d: string; min: number; max: number; last: number } {
  if (points.length === 0) return { d: "", min: 0, max: 0, last: 0 };
  let min = Number.POSITIVE_INFINITY;
  let max = Number.NEGATIVE_INFINITY;
  for (const p of points) {
    const value = pointValue(p);
    if (value < min) min = value;
    if (value > max) max = value;
  }
  if (!Number.isFinite(min) || !Number.isFinite(max)) {
    min = 0;
    max = 0;
  }
  if (min === max) {
    max = min + 1;
  }
  const pad = (max - min) * 0.08;
  const yMin = min - pad;
  const yMax = max + pad;
  const w = 100;
  const h = 40;
  const n = points.length;
  const stepX = n <= 1 ? 0 : w / (n - 1);
  const path: string[] = [];
  for (let i = 0; i < n; i++) {
    const x = i * stepX;
    const yNorm = (pointValue(points[i]) - yMin) / (yMax - yMin);
    const y = h - yNorm * h;
    path.push(`${x.toFixed(2)},${y.toFixed(2)}`);
  }
  return { d: path.join(" "), min, max, last: pointValue(points[n - 1]) };
}

export function RateLineChart({
  points,
  id = "rateLine",
  from = "rgba(66,153,225,0.85)",
  to = "rgba(56,178,172,0.85)",
}: {
  points: Array<RatePoint | TrendPoint>;
  id?: string;
  from?: string;
  to?: string;
}) {
  const { d } = buildPolyline(points);
  return (
    <Box
      w="100%"
      h="140px"
      borderRadius="md"
      bg="rgba(255,255,255,0.02)"
      borderWidth="1px"
      borderColor="rgba(255,255,255,0.08)"
      overflow="hidden"
    >
      <svg viewBox="0 0 100 40" width="100%" height="100%" preserveAspectRatio="none">
        <defs>
          <linearGradient id={id} x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%" stopColor={from} />
            <stop offset="100%" stopColor={to} />
          </linearGradient>
        </defs>
        <path d="M0 40 H100" stroke="rgba(255,255,255,0.06)" strokeWidth="0.5" />
        <path d="M0 20 H100" stroke="rgba(255,255,255,0.06)" strokeWidth="0.5" />
        <path d="M0 0 H100" stroke="rgba(255,255,255,0.06)" strokeWidth="0.5" />
        {d ? (
          <polyline
            fill="none"
            stroke={`url(#${id})`}
            strokeWidth="1.2"
            strokeLinejoin="round"
            strokeLinecap="round"
            points={d}
          />
        ) : null}
      </svg>
    </Box>
  );
}
