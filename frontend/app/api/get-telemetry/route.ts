import { NextResponse } from "next/server";

export async function GET() {
  try {
    // 1. Clean the Base URL and ensure it points to the correct endpoint
    let baseUrl = process.env.NEXT_INTERNAL_API_URL || "http://192.168.1.170:8000";
    baseUrl = baseUrl.replace(/\/+$/, ""); 
    const targetUrl = `${baseUrl}/api/telemetry`;

    // 2. Read the secret key and prepare headers
    const apiKey = process.env.FASTAPI_SECRET_KEY || "";
    
    // Log a warning if running in production without keys configured
    if (!apiKey && process.env.NODE_ENV === "production") {
      console.warn("[WARN] FASTAPI_SECRET_KEY is not set in Vercel Environment Variables. FastAPI will return 403 Forbidden.");
    }

    const response = await fetch(targetUrl, {
      headers: {
        "Accept": "application/json",
        "X-API-Key": apiKey,
        "ngrok-skip-browser-warning": "true",
      },
      cache: "no-store",
    });

    // 3. Handle and pass JSON response properly
    if (!response.ok) {
      const errorText = await response.text().catch(() => "Unknown error");
      console.error(`[BFF Proxy Error]: Backend returned ${response.status}. Details: ${errorText}`);
      
      return NextResponse.json(
        { error: `Backend returned ${response.status}`, details: errorText },
        { status: response.status }
      );
    }

    const data = await response.json();
    return NextResponse.json(data);
  } catch (error) {
    console.error("[BFF Proxy Error]:", error);
    return NextResponse.json(
      { error: "Internal Server Error" },
      { status: 500 }
    );
  }
}
