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

export default function Zone2Traffic({ data, range = "live" }: { data: TelemetryRow[], range?: string }) {
  const chartData = useMemo(() => {
    return [...data].sort(
      (a, b) => a.timestamp.getTime() - b.timestamp.getTime(),
    );
  }, [data]);

  const timeFmtLive = new Intl.DateTimeFormat(undefined, { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  const timeFmt24h = new Intl.DateTimeFormat(undefined, { hour: "2-digit", minute: "2-digit" });
  const timeFmtDays = new Intl.DateTimeFormat(undefined, { month: "short", day: "numeric" });

  function formatTime(value: unknown) {
    let d: Date;
    if (value instanceof Date) {
      d = value;
    } else if (typeof value === "number" || typeof value === "string") {
      d = new Date(value);
    } else {
      return "";
    }
    
    if (Number.isNaN(d.getTime())) return "";
    
    if (range === "24h") return timeFmt24h.format(d);
    if (range === "7d" || range === "30d") return timeFmtDays.format(d);
    return timeFmtLive.format(d);
  }

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
