# Brainstorming

This file is used to document your thoughts, approaches and research conducted across all tasks in the Technical Assessment.

## Firmware

## Spyder
Task 1 — Preventing invalid data from reaching the UI

The data-emulator occasionally emits packets that are:

- malformed JSON (e.g., truncated/concatenated }{),

- well-formed JSON but with wrong shape or types (e.g., battery_temperature as a string or null),

- numerically implausible spikes (e.g., -999 or 9999 °C).

Forwarding these straight to the browser can (a) spam the UI, (b) cause runtime errors, and (c) mislead users.

Approach implemented (in streaming-service/src/server.ts)

1. Framing & parsing:

- Buffer TCP chunks and split by newline.

- Normalize back-to-back JSON objects (}{ → }\n{) to avoid partial/concatenated frames.

- parseLine() validates the message and returns a clean { battery_temperature: number, timestamp: number } or null.

2. Validation rules (fail-closed):

- battery_temperature must be a finite number (coerce strings like "37.5" → 37.5).

- timestamp must be a finite integer (fallback to Date.now() if provided but stringy).

- Drop implausible temps outside [-50, 150] °C as clear glitches.

3. What we do with invalid data:

- Drop it before it hits the UI.

- Rate-limited logging: aggregate invalid counts and log once every ~5s, e.g.
Dropped 12 invalid packet(s) in last 5s. Example reason: JSON parse error.
This gives observability without log spam.

4. Foward only clean payloads:
- boradcast normalized events to clients:
{ "type": "data", "battery_temperature": <number>, "timestamp": <number> }


why using this design:
- Fail-closed avoids rendering misleading values.

- Normalization + validation handles both transport quirks and schema drift robustly.

- Rate-limited logs keep the console useful.

Edge cases considered

- Partial frames across TCP boundaries (solved via buffering).

- Concatenated frames without newline (solved via }\s*{ normalization).

- String numbers from the emulator (coerced safely).

- Giant spikes (dropped for safety and noise reduction).

How to test

- Run the stack; confirm UI stays stable.

- Observe [filter] summary logs when emulator emits junk.

- Confirm no invalid samples are forwarded to the browser (e.g., by inspecting WS messages in DevTools).

Task 2 — Alert when temp breaches safe range ≥3 times in 5 seconds
Requirement

Safe operating range is 20–80 °C. Each time the received battery temperature exceeds this range more than 3 times in 5 seconds, print the current timestamp and a simple error message to console.

Approach implemented

- Maintain a sliding window (queue) of timestamps for out-of-range readings.

- On each valid sample:

1. Check temp < 20 || temp > 80.

2. Push timestamp into breachTimes.

3. Drop entries older than timestamp - 5000 ms.

4. If queue length >= 3, log an [ALERT] with ISO timestamp and reset the queue to avoid repeated spam for the same burst.

- Also broadcast an alert event to the UI for visibility:
{ "type": "alert", "ts": <timestamp>, "message": "..." }

Why this design

- A sliding time window captures clusters of unsafe events without being fooled by a single outlier.

- Resetting after an alert provides simple hysteresis and avoids console spam.

- Broadcasting the alert lets the UI show a toast/banner (useful operator feedback).

How to test

- Temporarily set SAFE_MIN=35, SAFE_MAX=36 to force alerts, then restore to 20/80.

- Watch streaming-service logs for: 
[ALERT] Battery temperature exceeded safe range ≥3 times in 5s (latest=XX.XXX°C @ 2025-…Z)

- Verify the UI receives {type:"alert"} and surfaces a visible notification (if implemented).

Task 3 - Connect/Disconnect button doesn't update real data flow
Why it was occurring

- The UI’s connect/disconnect button toggled a local state or relied only on the WebSocket readyState.

- But a WebSocket can stay OPEN even when the emulator stops (no data flowing).

- There was no backend signal to the UI indicating source connectivity (emulator ↔ streaming-service) or freshness of data.

What can be done to rectify this

Backend changes (implemented):

- Emit status and heartbeat messages from the streaming-service:

-   On emulator connect/disconnect:
    { "type": "status", "sourceConnected": true|false, "ts": ... }

-   Periodically (e.g., every 2.5 seconds):
    { "type": "heartbeat", "sourceConnected": true|false, "ts": ... }

Frontend changes (recommended/justified):

- Derive real connection health as:
    healthy =
    (websocket.readyState === OPEN)
    AND (lastMessageTs is recent, e.g. < 6 s)
    AND (sourceConnected === true from status/heartbeat)

- Drive the button label/colour from healthy.

- This couples the UI state to actual transport + source availability + freshness, fixing the stale button problem.

Why this design

- Separates transport (WS open) from data plane (source producing data).

- Heartbeats give a simple, robust signal even if no normal data messages arrive for a while.

- The 6-second freshness window is a practical balance between noise and responsiveness (tunable).

How to test

- With UI open, stop the emulator (docker stop spyder-data-emulator-1).

- Within ~6 s, the button should flip to Disconnected.

- Start the emulator again; the button should return to Connected.

Notes on constraints

- Per the brief, data-emulator was not modified. All behavior changes were confined to streaming-service and optionally the UI.

- We chose to drop invalid data rather than forward placeholders; this prevents misleading UI states. This is justified because operational safety favors fail-closed semantics for sensor inputs.

## Cloud
Alternatives Considered:

- API Gateway (WebSockets/HTTP)
Simpler than IoT Core, but lacks per-device identity and is less efficient for IoT telemetry.

- Kafka / MSK
Great for high-scale streaming, but overkill and too costly for one weather station feed.

- DynamoDB instead of Timestream
Flexible, but lacks native time-series queries and retention features.

- Lambda transforms instead of Firehose
Useful for enrichment, but adds latency and cold start issues. Firehose is cheaper and simpler.

Storage Choice (S3 + Timestream):

- S3 for raw, immutable logs (cheap, durable, lifecycle to Glacier).

- Timestream for fast time-series queries (rolling averages, correlation with car data).
This combination balances cost and analytical power.

Security Notes:

- Weather station authenticates via X.509 certificate in IoT Core.

- TLS 1.2+ encryption for data in transit; S3 SSE-KMS and Timestream encryption at rest.

- IAM policies are least-privilege.

- Plan for certificate rotation as part of device lifecycle.

Failure Modes & Mitigations

- Network outage: station caches data locally, retries with QoS1.

- Clock drift: NTP sync on device; backup server-side ingest timestamp.

- Schema evolution: versioned MQTT topics (e.g., v1/metrics) and JSON validation in gateway.

Use of Generative AI:

I used generative AI (ChatGPT) to:

- Brainstorm architecture options and trade-offs.

- Draft initial wording for sections of the PDF.

- Suggest Terraform snippets for S3, Timestream, Firehose, and IoT rules.

I reviewed and edited all content to ensure correctness and alignment with the task requirements.