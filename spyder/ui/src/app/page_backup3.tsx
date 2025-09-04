"use client"

import { useState, useEffect } from "react"
import useWebSocket, { ReadyState } from "react-use-websocket"
import { useTheme } from "next-themes"
import Image from "next/image"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Badge } from "@/components/ui/badge"
import { Thermometer } from "lucide-react"
import Numeric from "../components/custom/numeric"
import RedbackLogoDarkMode from "../../public/logo-darkmode.svg"
import RedbackLogoLightMode from "../../public/logo-lightmode.svg"
import { LineChart, Line, Tooltip, XAxis, YAxis, CartesianGrid, ReferenceArea } from "recharts"

const WS_URL = "ws://localhost:8080"

interface VehicleData {
  battery_temperature: number
  timestamp: number
}

/**
 * Page component that displays DAQ technical assessment. Contains the LiveValue component as well as page header and labels.
 * Could this be split into more components?...
 *
 * @returns {JSX.Element} The rendered page component.
 */
export default function Page(): JSX.Element {
  const { setTheme } = useTheme()
  const [temperature, setTemperature] = useState<any>(0)
  const [sourceConnected, setSourceConnected] = useState(false)
  const [lastMessageTs, setLastMessageTs] = useState<number | null>(null)
  const [series, setSeries] = useState<{ ts: number; temp: number }[]>([])
  const { lastJsonMessage, readyState }: { lastJsonMessage: any; readyState: ReadyState } = useWebSocket(
    WS_URL,
    {
      share: false,
      shouldReconnect: () => true,
    },
  )

const healthy =
  readyState === ReadyState.OPEN &&
  sourceConnected &&
  lastMessageTs !== null &&
  Date.now() - lastMessageTs < 6000;

 
  // derive stats for last 60s
  const stats = (() => {
    if (!series.length) return { min: null as number | null, avg: null as number | null, max: null as number | null };
    let mn = series[0].temp, mx = series[0].temp, sum = 0;
    for (const s of series) {
      if (s.temp < mn) mn = s.temp;
      if (s.temp > mx) mx = s.temp;
      sum += s.temp;
    }
    return { min: mn, avg: sum / series.length, max: mx };
  })();

  /**
   * Effect hook to set the theme to dark mode.
   */

    useEffect(() => {
    if (!lastJsonMessage) return;
    const now = Date.now();
 
    // backend meta messages
    if (lastJsonMessage.type === "status" || lastJsonMessage.type === "heartbeat") {
      setSourceConnected(!!lastJsonMessage.sourceConnected);
      setLastMessageTs(lastJsonMessage.ts ?? now);
      return;
    }
    // data message
    if (typeof lastJsonMessage.battery_temperature !== "undefined") {
        const tempNum = Number(lastJsonMessage.battery_temperature);
        setTemperature(tempNum);
        setLastMessageTs(lastJsonMessage.timestamp ?? now);
        setSeries(prev => {
          const next = [...prev, { ts: now, temp: tempNum }];
          return next.filter(p => p.ts >= now - 60_000);
        });
    }
  }, [lastJsonMessage]);

  useEffect(() => {
    setTheme("dark")
  }, [setTheme])

  return (
    <div className="min-h-screen bg-background flex flex-col">
      <header className="px-5 h-20 flex items-center gap-5 border-b">
        <Image
          src={RedbackLogoDarkMode}
          className="h-12 w-auto"
          alt="Redback Racing Logo"
        />
        <h1 className="text-foreground text-xl font-semibold">DAQ Technical Assessment</h1>
        <Badge variant={healthy ? "success" : "destructive"} className="ml-auto">
          {healthy ? "Connected" : "Disconnected"}
        </Badge>
      </header>
      <main className="flex-grow flex items-center justify-center p-8">
        <Card className="w-full max-w-md">
          <CardHeader>
            <CardTitle className="text-2xl font-light flex items-center gap-2">
              <Thermometer className="h-6 w-6" />
              Live Battery Temperature
            </CardTitle>
          </CardHeader>
          <CardContent className="flex items-center justify-center">
            <Numeric temp={temperature} />
          </CardContent>
        </Card>
    {/* ===== Last 60s chart ===== */}
    <div className="w-full max-w-3xl mt-10">
      <h2 className="text-lg font-semibold mb-2 text-foreground">Last 60 seconds</h2>
      <div className="overflow-x-auto rounded-2xl border border-border bg-card p-4">
        <LineChart
          width={720}
          height={260}
          data={series.map(s => ({ t: new Date(s.ts).toLocaleTimeString(), temp: Number(s.temp.toFixed(3)) }))}
        >
          <CartesianGrid strokeDasharray="3 3" />
          <XAxis dataKey="t" hide />
          <YAxis domain={[0, 120]} />
          <Tooltip />
          {/* safe band shading */}
          <ReferenceArea y1={20} y2={80} />
          <Line type="monotone" dataKey="temp" dot={false} />
        </LineChart>

        {/* ===== Stats row ===== */}
        <div className="grid grid-cols-3 gap-4 mt-4 text-center">
          <div className="rounded-lg border border-border p-3">
            <div className="text-xs text-muted-foreground">Min (60s)</div>
            <div className="text-lg font-medium">{stats.min == null ? "—" : stats.min.toFixed(3)}</div>
          </div>
          <div className="rounded-lg border border-border p-3">
            <div className="text-xs text-muted-foreground">Avg (60s)</div>
            <div className="text-lg font-medium">{stats.avg == null ? "—" : stats.avg.toFixed(3)}</div>
          </div>
          <div className="rounded-lg border border-border p-3">
            <div className="text-xs text-muted-foreground">Max (60s)</div>
            <div className="text-lg font-medium">{stats.max == null ? "—" : stats.max.toFixed(3)}</div>
          </div>
        </div>
      </div>
    </div>
   </main>
    </div>
  )
}
