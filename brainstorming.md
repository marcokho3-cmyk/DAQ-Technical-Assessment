# Brainstorming

This file is used to document your thoughts, approaches and research conducted across all tasks in the Technical Assessment.

## Firmware

## Spyder

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