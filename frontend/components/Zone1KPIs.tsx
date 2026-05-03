"use client";

import type { TelemetryRow } from "@/hooks/useTelemetry";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { useMemo } from "react";

function formatNumber(value: number | undefined) {
  if (value === undefined || Number.isNaN(value)) return "--";
  return new Intl.NumberFormat(undefined).format(value);
}

export default function Zone1KPIs({ data }: { data: TelemetryRow[] }) {
  const latest = useMemo(() => {
    // Data can arrive newest-first (default FastAPI behavior), so pick by timestamp
    // instead of trusting array order.
    let best: TelemetryRow | undefined;
    for (const row of data) {
      if (!best || row.timestamp.getTime() > best.timestamp.getTime()) best = row;
    }
    return best;
  }, [data]);

  const kpis = [
    { label: "Cars", value: latest ? formatNumber(latest.car) : "--" },
    { label: "Motorcycles", value: latest ? formatNumber(latest.motorcycle) : "--" },
    { label: "RSSI (dBm)", value: latest ? formatNumber(latest.rssi) : "--" },
    { label: "SNR (dB)", value: latest ? String(latest.snr.toFixed(1)) : "--" },
  ] as const;

  return (
    <div className="grid grid-cols-2 gap-3 md:grid-cols-4">
      {kpis.map((kpi) => (
        <Card key={kpi.label} className="border/70">
          <CardHeader>
            <CardTitle>{kpi.label}</CardTitle>
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-semibold tabular-nums">{kpi.value}</div>
          </CardContent>
        </Card>
      ))}
    </div>
  );
}
