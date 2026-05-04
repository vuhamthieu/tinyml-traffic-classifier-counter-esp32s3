"use client";

import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import Zone1KPIs from "@/components/Zone1KPIs";
import Zone2Traffic from "@/components/Zone2Traffic";
import Zone3Diagnostics from "@/components/Zone3Diagnostics";
import { useTelemetry } from "@/hooks/useTelemetry";

export default function Home() {
  const { data, isLoading, isError, error } = useTelemetry();

  const isLive = !isError;
  const hasData = data && data.length > 0;

  return (
    <main className="mx-auto flex min-h-dvh w-full max-w-7xl flex-col gap-4 p-4 md:p-6">
      
      {/* Live System Indicator */}
      <div className="flex items-center justify-between rounded-lg border p-4 bg-muted/10">
        <h1 className="text-xl font-bold tracking-tight">AIoT Dashboard</h1>
        {hasData && (
          isLive ? (
            <div className="flex items-center gap-2 text-sm font-medium text-green-600 dark:text-green-400">
              <span className="relative flex h-3 w-3">
                <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-green-400 opacity-75"></span>
                <span className="relative inline-flex rounded-full h-3 w-3 bg-green-500"></span>
              </span>
               Live System
            </div>
          ) : (
            <div className="flex items-center gap-2 text-sm font-medium text-red-500">
              <span className="relative flex h-3 w-3">
                <span className="relative inline-flex rounded-full h-3 w-3 bg-red-500"></span>
              </span>
               Live Feed Disconnected - Showing Last Known Data
            </div>
          )
        )}
      </div>

      {/* Zone 1 (Health + KPIs) */}
      <section className="rounded-lg border p-4">
        <header className="space-y-1">
          <h2 className="text-lg font-semibold tracking-tight">
            Edge AI Vehicle Counter
          </h2>
          <p className="text-sm text-foreground/70">
            Zone 1: system health + KPI cards
          </p>
        </header>

        <div className="mt-4">
          {isLoading && !hasData ? (
            <div className="text-sm text-foreground/70">Loading telemetry…</div>
          ) : !hasData ? (
            <Alert variant="default" className="bg-muted/50 border-dashed">
              <AlertTitle>System Offline / Awaiting Edge Data</AlertTitle>
              <AlertDescription>
                The Edge AI Camera and IoT Gateway are currently disconnected from the backend. 
                Waiting for the next LoRa payload packet to arrive...
              </AlertDescription>
            </Alert>
          ) : (
            <Zone1KPIs data={data} />
          )}
        </div>
      </section>

      {/* Zone 2 (Traffic Flow Canvas) */}
      <section className="flex-1 rounded-lg border p-4">
        <header className="space-y-1">
          <h2 className="text-base font-semibold tracking-tight">Live Telemetry</h2>
          <p className="text-sm text-foreground/70">
            Zone 2: scrolling chart (MVP uses Recharts)
          </p>
        </header>

        <div className="mt-4">
          {isLoading && !hasData ? (
            <div className="text-sm text-foreground/70">Loading chart…</div>
          ) : !hasData ? (
            <div className="flex h-48 items-center justify-center rounded-md border border-dashed bg-muted/20 text-sm text-foreground/50">
              [ Live Telemetry Offline ]
            </div>
          ) : (
            <Zone2Traffic data={data} />
          )}
        </div>
      </section>

      {/* Zone 3 (Diagnostics) */}
      <section className="rounded-lg border p-4">
        <header className="space-y-1">
          <h2 className="text-base font-semibold tracking-tight">Diagnostics</h2>
          <p className="text-sm text-foreground/70">
            Zone 3: RSSI / SNR charts
          </p>
        </header>

        <div className="mt-4">
          {isLoading && !hasData ? (
            <div className="text-sm text-foreground/70">Loading diagnostics…</div>
          ) : !hasData ? (
            <div className="flex h-32 items-center justify-center rounded-md border border-dashed bg-muted/20 text-sm text-foreground/50">
              [ Diagnostics Offline ]
            </div>
          ) : (
            <Zone3Diagnostics data={data} />
          )}
        </div>
      </section>
    </main>
  );
}
