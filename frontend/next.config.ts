import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  /* config options here */
  devIndicators: {
    buildActivity: true,
    buildActivityPosition: 'bottom-right',
  },
  // Whitelist your local network IPs to bypass HMR blocking
  experimental: {
    allowedDevOrigins: ['192.168.1.170', 'localhost', '127.0.0.1']
  }
};

export default nextConfig;
