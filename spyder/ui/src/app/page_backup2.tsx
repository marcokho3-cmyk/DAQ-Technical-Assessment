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
      setTemperature(Number(lastJsonMessage.battery_temperature));
      setLastMessageTs(lastJsonMessage.timestamp ?? now);
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
      </main>
    </div>
  )
}
