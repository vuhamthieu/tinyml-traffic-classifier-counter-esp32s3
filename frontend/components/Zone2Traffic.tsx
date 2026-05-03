"use client";

import type { TelemetryRow } from "@/hooks/useTelemetry";
import { useMemo } from "react";
import {
  Area,
  AreaChart,
  Legend,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";

const timeFmt = new Intl.DateTimeFormat(undefined, {
  hour: "2-digit",
  minute: "2-digit",
  second: "2-digit",
});

function formatTime(value: unknown) {
  if (value instanceof Date) return timeFmt.format(value);
  if (typeof value === "number" || typeof value === "string") {
    const d = new Date(value);
    if (!Number.isNaN(d.getTime())) return timeFmt.format(d);
  }
  return "";
}

export default function Zone2Traffic({ data }: { data: TelemetryRow[] }) {
  const chartData = useMemo(() => {
    return [...data].sort(
      (a, b) => a.timestamp.getTime() - b.timestamp.getTime(),
    );
  }, [data]);

  return (
    <div className="h-[320px] w-full md:h-[420px]">
      <ResponsiveContainer width="100%" height="100%" minHeight={320}>
        <AreaChart data={chartData} margin={{ top: 8, right: 16, left: 0, bottom: 0 }}>
          <XAxis
            dataKey="timestamp"
            tickFormatter={formatTime}
            minTickGap={24}
            stroke="#888888" 
          />
          <YAxis allowDecimals={false} stroke="#888888" />
          
          <Tooltip
            labelFormatter={(label) => `Time: ${formatTime(label)}`}
            contentStyle={{ backgroundColor: '#171717', borderColor: '#333', color: '#fff' }}
          />
          <Legend />

          <Area
            type="monotone"
            dataKey="car"
            name="Cars"
            stroke="#0ea5e9"
            fill="#0ea5e9"
            fillOpacity={0.3}
            strokeWidth={2}
            dot={false}
            isAnimationActive={false}
          />
          <Area
            type="monotone"
            dataKey="motorcycle"
            name="Motorcycles"
            stroke="#10b981"
            fill="#10b981"
            fillOpacity={0.3}
            strokeWidth={2}
            dot={false}
            isAnimationActive={false}
          />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  );
}