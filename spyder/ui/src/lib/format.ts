export function fmt3(x: number | null | undefined) {
  if (x == null || !Number.isFinite(x)) return "—";
  return Number(x).toFixed(3);
}

export function tempTone(temp: number | null | undefined) {
  if (temp == null || !Number.isFinite(temp)) return "text-zinc-500";
  if (temp < 20 || temp > 80) return "text-temp-unsafe";
  if ((temp >= 20 && temp < 25) || (temp > 75 && temp <= 80)) return "text-temp-warn";
  return "text-temp-safe";
}
