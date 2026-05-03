"use client";

import type { TelemetryRow } from "@/hooks/useTelemetry";
import { useMemo } from "react";
import {
  Legend,
  Line,
  LineChart,
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

export default function Zone3Diagnostics({ data }: { data: TelemetryRow[] }) {
  const chartData = useMemo(() => {
    return [...data].sort(
      (a, b) => a.timestamp.getTime() - b.timestamp.getTime(),
    );
  }, [data]);

  return (
    <div className="h-[240px] w-full md:h-[300px]">
      <ResponsiveContainer width="100%" height="100%" minHeight={240}>
        <LineChart data={chartData} margin={{ top: 8, right: 16, left: 0, bottom: 0 }}>
          <XAxis dataKey="timestamp" tickFormatter={formatTime} minTickGap={24} stroke="#888888" />

          <YAxis yAxisId="left" width={44} stroke="#888888" />
          <YAxis yAxisId="right" orientation="right" width={44} stroke="#888888" />

          <Tooltip 
            labelFormatter={(label) => `Time: ${formatTime(label)}`}
            contentStyle={{ backgroundColor: '#171717', borderColor: '#333', color: '#fff' }}
          />
          <Legend />

          <Line
            yAxisId="left"
            type="monotone"
            dataKey="rssi"
            name="RSSI (dBm)"
            stroke="#f59e0b"
            strokeWidth={2}
            dot={false}
            isAnimationActive={false}
          />
          <Line
            yAxisId="right"
            type="monotone"
            dataKey="snr"
            name="SNR (dB)"
            stroke="#8b5cf6"
            strokeWidth={2}
            dot={false}
            isAnimationActive={false}
          />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}