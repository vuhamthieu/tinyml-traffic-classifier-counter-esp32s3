"use client";

import { useState, useEffect } from "react";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import Zone1KPIs from "@/components/Zone1KPIs";
import Zone2Traffic from "@/components/Zone2Traffic";
import Zone3Diagnostics from "@/components/Zone3Diagnostics";
import { useTelemetry } from "@/hooks/useTelemetry";
import { Activity, Clock, Download, Cpu, Signal, Server } from "lucide-react";

export default function Home() {
  const [range, setRange] = useState("live");
  const { data, isLoading, isError, error, dataUpdatedAt } = useTelemetry(range);
  const [secondsAgo, setSecondsAgo] = useState(0);

  const isLive = !isError;
  const hasData = data && data.length > 0;
  const latestData = hasData ? data[data.length - 1] : null;

  // Heartbeat Indicator
  useEffect(() => {
    if (!dataUpdatedAt) return;
    const interval = setInterval(() => {
      setSecondsAgo(Math.floor((Date.now() - dataUpdatedAt) / 1000));
    }, 1000);
    setSecondsAgo(Math.floor((Date.now() - dataUpdatedAt) / 1000));
    return () => clearInterval(interval);
  }, [dataUpdatedAt]);

  // CSV Export logic
  const handleExportCSV = () => {
    if (!data || data.length === 0) return;
    
    // Get headers
    const headers = Object.keys(data[0]).join(",");
    // Get rows
    const rows = data.map(row => {
      return Object.values(row).map(value => 
        typeof value === 'object' && value !== null ? value.toISOString() : value
      ).join(",");
    });
    
    const csvContent = "data:text/csv;charset=utf-8," + [headers, ...rows].join("\n");
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement("a");
    link.setAttribute("href", encodedUri);
    link.setAttribute("download", `telemetry_export_${range}_${new Date().getTime()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  return (
    <main className="mx-auto flex min-h-dvh w-full max-w-7xl flex-col gap-4 p-4 md:p-6">
      {/* Top Header & Controls */}
      <div className="flex flex-col md:flex-row items-start md:items-center justify-between rounded-lg border p-4 bg-muted/10 gap-4">
        <div>
          <h1 className="text-xl font-bold tracking-tight">AIoT Telemetry Analytics</h1>
          {dataUpdatedAt && hasData && (
            <div className="flex items-center gap-2 mt-1 text-sm text-muted-foreground">
              <Clock className="w-4 h-4" />
              <span>Last updated: {secondsAgo} seconds ago</span>
            </div>
          )}
        </div>

        <div className="flex flex-wrap items-center gap-2">
          {/* Time Range Filter */}
          <div className="flex p-1 bg-muted rounded-md shrink-0">
            {["live", "24h", "7d", "30d"].map((r) => (
              <button
                key={r}
                onClick={() => setRange(r)}
                className={`px-3 py-1.5 text-sm font-medium rounded-sm transition-colors ${
                  range === r 
                    ? "bg-background text-foreground shadow-sm" 
                    : "text-muted-foreground hover:bg-muted-foreground/10"
                }`}
              >
                {r === "live" ? "Live (Now)" : r === "24h" ? "Last 24h" : r === "7d" ? "Last 7d" : "Last 30d"}
              </button>
            ))}
          </div>

          <button 
            onClick={handleExportCSV}
            disabled={!hasData}
            className="flex items-center gap-2 px-3 py-1.5 text-sm font-medium bg-primary text-primary-foreground rounded-md hover:bg-primary/90 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
          >
            <Download className="w-4 h-4" />
            <span className="hidden sm:inline">Export CSV</span>
          </button>
        </div>
      </div>

      {/* System Health / Diagnostics Panel */}
      {hasData && (
        <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
          <div className="rounded-lg border p-4 bg-card flex flex-col gap-1">
            <div className="flex items-center gap-2 text-sm font-medium text-muted-foreground">
              <Activity className="w-4 h-4" /> Edge Status
            </div>
            <div className={`text-lg font-semibold flex items-center gap-2 ${isLive ? 'text-green-500' : 'text-red-500'}`}>
              <span className="relative flex h-3 w-3">
                {isLive && <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-green-400 opacity-75"></span>}
                <span className={`relative inline-flex rounded-full h-3 w-3 ${isLive ? 'bg-green-500' : 'bg-red-500'}`}></span>
              </span>
              {isLive ? 'Connected' : 'Offline'}
            </div>
          </div>
          
          <div className="rounded-lg border p-4 bg-card flex flex-col gap-1">
            <div className="flex items-center gap-2 text-sm font-medium text-muted-foreground">
              <Cpu className="w-4 h-4" /> YOLO FPS
            </div>
            <div className="text-lg font-semibold">
              {latestData?.fps != null ? `${latestData.fps} fps` : 'N/A'}
            </div>
          </div>

          <div className="rounded-lg border p-4 bg-card flex flex-col gap-1">
            <div className="flex items-center gap-2 text-sm font-medium text-muted-foreground">
              <Server className="w-4 h-4" /> Node Heap
            </div>
            <div className="text-lg font-semibold">
              {latestData?.heap != null ? `${(latestData.heap / 1024).toFixed(1)} KB` : 'N/A'}
            </div>
          </div>

          <div className="rounded-lg border p-4 bg-card flex flex-col gap-1">
            <div className="flex items-center gap-2 text-sm font-medium text-muted-foreground">
              <Signal className="w-4 h-4" /> Signal (LoRa)
            </div>
            <div className="text-lg font-semibold flex items-baseline gap-2">
              <span>{latestData?.rssi} dBm</span>
              <span className="text-xs text-muted-foreground font-normal">SNR: {latestData?.snr}</span>
            </div>
          </div>
        </div>
      )}

      {/* Zone 1 (Health + KPIs) */}
      <section className="rounded-lg border p-4">
        <header className="space-y-1">
          <h2 className="text-lg font-semibold tracking-tight">
            Traffic Metrics
          </h2>
          <p className="text-sm text-foreground/70">
            Overview of detected vehicle counts
          </p>
        </header>

        <div className="mt-4">
          {isLoading && !hasData ? (
            <div className="text-sm text-foreground/70">Loading telemetry…</div>
          ) : !hasData ? (
            <Alert variant="default" className="bg-muted/50 border-dashed">
              <AlertTitle>System Offline / Awaiting Edge Data</AlertTitle>
              <AlertDescription>
                The Edge AI Camera and IoT Gateway are currently disconnected from the backend. Waiting for the next LoRa payload packet to arrive...
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
          <h2 className="text-base font-semibold tracking-tight">{range === 'live' ? 'Live Telemetry' : 'Historical Data'}</h2>
          <p className="text-sm text-foreground/70">
            Traffic flow over selected period
          </p>
        </header>

        <div className="mt-4">
          {isLoading && !hasData ? (
            <div className="text-sm text-foreground/70">Loading chart…</div>
          ) : !hasData ? (
            <div className="flex h-48 items-center justify-center rounded-md border border-dashed bg-muted/20 text-sm text-foreground/50">
              [ Data Offline ]
            </div>
          ) : (
            <Zone2Traffic data={data} range={range} />
          )}
        </div>
      </section>

      {/* Zone 3 (Diagnostics) */}
      <section className="rounded-lg border p-4">
        <header className="space-y-1">
          <h2 className="text-base font-semibold tracking-tight">Signal History</h2>
          <p className="text-sm text-foreground/70">
            RSSI & SNR variations
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
