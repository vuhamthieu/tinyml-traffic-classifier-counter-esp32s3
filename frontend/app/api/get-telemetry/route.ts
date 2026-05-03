import { NextResponse } from "next/server";

export async function GET() {
  try {
    const baseUrl = process.env.NEXT_INTERNAL_API_URL || "http://192.168.1.170:8000";
    
    const response = await fetch(`${baseUrl}/api/telemetry`, {
      headers: {
        "Accept": "application/json",
        "X-API-Key": process.env.FASTAPI_SECRET_KEY || "",
        "ngrok-skip-browser-warning": "true",
      },
      // Prevent Next.js from aggressively caching this real-time request
      cache: "no-store",
    });

    if (!response.ok) {
      return NextResponse.json(
        { error: `Backend returned ${response.status}` },
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
