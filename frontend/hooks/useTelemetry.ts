"use client";

import { useQuery } from "@tanstack/react-query";
import { z } from "zod";

const TelemetryRowSchema = z.object({
  id: z.number(),
  node_id: z.string().min(1),
  car: z.number(),
  motorcycle: z.number(),
  rssi: z.number(),
  snr: z.number(),
  timestamp: z.coerce.date(),
});

const TelemetryResponseSchema = z.array(TelemetryRowSchema);

export type TelemetryRow = z.infer<typeof TelemetryRowSchema>;

async function fetchTelemetry(): Promise<TelemetryRow[]> {
  // Fetch from the local Next.js BFF API route proxy
  const res = await fetch("/api/get-telemetry", {
    headers: { Accept: "application/json" },
  });

  if (!res.ok) {
    throw new Error(`Telemetry fetch failed: ${res.status} ${res.statusText}`);
  }

  const json: unknown = await res.json();
  return TelemetryResponseSchema.parse(json);
}

export function useTelemetry() {
  return useQuery({
    queryKey: ["telemetry"],
    queryFn: fetchTelemetry,
    refetchInterval: () => {
      if (typeof document === "undefined") return false;
      return document.visibilityState === "visible" ? 2000 : false;
    },
    staleTime: 1500,
    refetchOnWindowFocus: true,
    refetchOnReconnect: true,
    refetchIntervalInBackground: false,
  });
}
