"use client";

import { useQuery } from "@tanstack/react-query";
import { z } from "zod";
import { useEffect, useState } from "react";

const TelemetryRowSchema = z.object({
  id: z.number(),
  node_id: z.string().min(1),
  car: z.number(),
  motorcycle: z.number(),
  rssi: z.number(),
  snr: z.number(),
  timestamp: z.coerce.date(),
  fps: z.number().nullable().optional(),
  heap: z.number().nullable().optional(),
});

const TelemetryResponseSchema = z.array(TelemetryRowSchema);

export type TelemetryRow = z.infer<typeof TelemetryRowSchema>;

const CACHE_KEY = "lastKnownTelemetry";

// Retrieve and properly typecast cached telemetry parsing stringified dates into JS Date objects
function getCachedTelemetry(): TelemetryRow[] | undefined {
  if (typeof window === "undefined") return undefined;
  try {
    const raw = localStorage.getItem(CACHE_KEY);
    if (raw) {
      return TelemetryResponseSchema.parse(JSON.parse(raw));
    }
  } catch (error) {
    console.error("Failed to parse cached telemetry:", error);
  }
  return undefined;
}

export function useTelemetry(range: string = "live") {
  const [cachedData, setCachedData] = useState<TelemetryRow[] | undefined>(undefined);

  // 5. Initial Mount: Load from localStorage instantly
  useEffect(() => {
    const data = getCachedTelemetry();
    if (data) {
      setCachedData(data);
    }
  }, []);

  const query = useQuery({
    queryKey: ["telemetry", range],
    // 1. Fail-Fast: Disable automatic retries to quickly show Offline state
    retry: 0,
    queryFn: async ({ signal }) => {
      // Fail-fast timeout configuration
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 3000);
      signal?.addEventListener("abort", () => controller.abort());

      try {
        const res = await fetch(`/api/get-telemetry?range=${range}`, {
          headers: { Accept: "application/json" },
          signal: controller.signal,
        });

        if (!res.ok) {
          throw new Error(`Telemetry fetch failed: ${res.status} ${res.statusText}`);
        }

        const json: unknown = await res.json();
        const parsed = TelemetryResponseSchema.parse(json);

        // 2. Safe LocalStorage Persistence
        if (typeof window !== "undefined") {
          localStorage.setItem(CACHE_KEY, JSON.stringify(parsed));
          setCachedData(parsed); // Sync our local state with the latest successfully fetched data
        }

        return parsed;
      } finally {
        clearTimeout(timeoutId);
      }
    },
    refetchInterval: () => {
      if (typeof document === "undefined") return false;
      return document.visibilityState === "visible" ? (range === 'live' ? 2000 : 30000) : false;
    },
    staleTime: 1500,
    refetchOnWindowFocus: true,
    refetchOnReconnect: true,
    refetchIntervalInBackground: false,
  });

  // 3. Offline Fallback Logic: Always fallback to localStorage state if the network goes down
  const activeData = query.data ?? cachedData;

  return {
    data: activeData,
    isLoading: query.isLoading && !activeData,
    isError: query.isError,
    error: query.error,
    dataUpdatedAt: query.dataUpdatedAt,
  };
}
