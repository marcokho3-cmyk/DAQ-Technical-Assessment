import net from "net";
import { WebSocket, WebSocketServer } from "ws";

type CleanSample = { battery_temperature: number; timestamp: number };

const TCP_PORT = 12000; // emulator connects here
const WS_PORT = 8080;   // frontend connects here

// Safe range
const SAFE_MIN = 35;
const SAFE_MAX = 36;

// --- WebSocket server for frontends ---
const websocketServer = new WebSocketServer({ port: WS_PORT });
websocketServer.on("listening", () =>
  console.log(`[ws] WebSocket server started on ${WS_PORT}`)
);
websocketServer.on("connection", (ws: WebSocket) => {
  console.log("[ws] Frontend websocket client connected");
  ws.on("error", console.error);
});

function broadcast(obj: any) {
  const json = JSON.stringify(obj);
  websocketServer.clients.forEach((c) => {
    if (c.readyState === WebSocket.OPEN) c.send(json);
  });
}

// --- Source (TCP) connection tracking for status/UX ---
let tcpConnections = 0;
function setSourceStatus(connected: boolean) {
  broadcast({ type: "status", sourceConnected: connected, ts: Date.now() });
}
setInterval(() => {
  // periodic heartbeat so UI can infer freshness
  broadcast({ type: "heartbeat", ts: Date.now(), sourceConnected: tcpConnections > 0 });
}, 2500);

// --- Invalid packet rate-limited logging ---
let invalidCount = 0;
let lastInvalidLog = 0;
function noteInvalid(reason: string) {
  invalidCount++;
  const now = Date.now();
  if (now - lastInvalidLog > 5000) {
    console.warn(`[filter] Dropped ${invalidCount} invalid packet(s) in last 5s. Example reason: ${reason}`);
    invalidCount = 0;
    lastInvalidLog = now;
  }
}

// --- Parse & validate a single line of JSON ---
function parseLine(line: string): CleanSample | null {
  try {
    const obj = JSON.parse(line);

    // Accept common field names; coerce strings like "37.5" to number
    const tempRaw = obj?.battery_temperature ?? obj?.temp ?? obj?.temperature ?? obj?.value;
    const tsRaw = obj?.timestamp ?? obj?.ts ?? Date.now();

    const temp = Number(tempRaw);
    const timestamp = Number(tsRaw);

    if (!Number.isFinite(temp)) return noteInvalid("battery_temperature not finite"), null;
    if (!Number.isFinite(timestamp)) return noteInvalid("timestamp not finite"), null;

    // sanity guard to drop absurd spikes caused by emulator glitches
    if (temp < -50 || temp > 150) return noteInvalid("battery_temperature implausible"), null;

    return { battery_temperature: temp, timestamp: Math.trunc(timestamp) };
  } catch {
    noteInvalid("JSON parse error");
    return null;
  }
}

// --- Out-of-range detection (≥3 events within 5s) ---
const breachTimes: number[] = [];
function handleRange(temp: number, ts: number) {
  if (temp >= SAFE_MIN && temp <= SAFE_MAX) return;
  breachTimes.push(ts);
  const cutoff = ts - 5000;
  while (breachTimes.length && breachTimes[0] < cutoff) breachTimes.shift();

  if (breachTimes.length >= 3) {
    const iso = new Date(ts).toISOString();
    const message = `Battery temperature exceeded safe range ≥3 times in 5s (latest=${temp.toFixed(3)}°C @ ${iso})`;
    console.error(`[ALERT] ${message}`);
    broadcast({ type: "alert", ts, message });
    breachTimes.length = 0; // reset after alert to avoid spamming
  }
}

// --- TCP server that receives newline-delimited JSON ---
const tcpServer = net.createServer();

tcpServer.on("connection", (socket) => {
  tcpConnections++;
  setSourceStatus(true);
  console.log("[tcp] Client connected (emulator). Active:", tcpConnections);

  let buf = ""; // per-connection buffer

  socket.on("data", (chunk) => {
    buf += chunk.toString("utf8");
    const lines = buf.split("\n");
    buf = lines.pop() ?? ""; // keep incomplete line for next chunk

    for (const raw of lines) {
      const line = raw.trim();
      if (!line) continue;
      const sample = parseLine(line);
      if (!sample) continue;

      // Range logic
      handleRange(sample.battery_temperature, sample.timestamp);

      // Forward only clean data, in a consistent shape
      broadcast({
        type: "data",
        battery_temperature: sample.battery_temperature,
        timestamp: sample.timestamp,
      });
    }
  });

  socket.on("end", () => {
    tcpConnections = Math.max(0, tcpConnections - 1);
    console.log("[tcp] Client disconnected. Active:", tcpConnections);
    if (tcpConnections === 0) setSourceStatus(false);
  });

  socket.on("error", (err) => {
    console.error("[tcp] Client error:", err.message);
  });
});

tcpServer.listen(TCP_PORT, () => {
  console.log(`[tcp] TCP server listening on ${TCP_PORT}`);
});
