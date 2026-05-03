"use client";

import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { ReactNode, useState } from "react";

export default function Providers({ children }: { children: ReactNode }) {
  // Create the client once per browser session.
  // (React Query really doesn’t like getting a new client every render.)
  const [queryClient] = useState(
    () =>
      new QueryClient({
        defaultOptions: {
          queries: {
            // This dashboard is basically “always on”.
            refetchOnWindowFocus: true,
            refetchOnReconnect: true,
            // Keep retries conservative so we surface errors quickly.
            retry: 1,
          },
        },
      }),
  );

  return <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>;
}
