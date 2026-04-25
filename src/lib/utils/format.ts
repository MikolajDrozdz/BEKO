// ─── Date/time ────────────────────────────────────────────────────────────────

function toDate(ts: string | number): Date {
  if (typeof ts === 'number') {
    // treat as Unix epoch seconds if small, milliseconds if large
    return ts < 1e10 ? new Date(ts * 1000) : new Date(ts)
  }
  // ISO 8601 strings — ensure UTC offset is respected
  const d = new Date(ts)
  return d
}

/** Full datetime: "12 Jan 2025, 14:32:05" */
export function formatTimestamp(ts?: string | number | null): string {
  if (ts == null || ts === '') return '—'
  try {
    const d = toDate(ts)
    if (isNaN(d.getTime())) return String(ts)
    return d.toLocaleString(undefined, {
      day: '2-digit',
      month: 'short',
      year: 'numeric',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    })
  } catch {
    return String(ts)
  }
}

/** Time only: "14:32:05" */
export function formatTime(ts?: string | number | null): string {
  if (ts == null || ts === '') return ''
  try {
    const d = toDate(ts)
    if (isNaN(d.getTime())) return ''
    return d.toLocaleTimeString(undefined, {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    })
  } catch {
    return ''
  }
}

/** Date label: "Today", "Yesterday", or "Wednesday, 12 January 2025" */
export function formatDateLabel(ts?: string | number | null): string {
  if (ts == null || ts === '') return 'Unknown date'
  try {
    const d = toDate(ts)
    if (isNaN(d.getTime())) return String(ts)
    const today = new Date()
    const yesterday = new Date(today)
    yesterday.setDate(today.getDate() - 1)
    if (d.toDateString() === today.toDateString()) return 'Today'
    if (d.toDateString() === yesterday.toDateString()) return 'Yesterday'
    return d.toLocaleDateString(undefined, {
      weekday: 'long',
      year: 'numeric',
      month: 'long',
      day: 'numeric',
    })
  } catch {
    return String(ts)
  }
}

/** Returns a stable string key for grouping messages by calendar day */
export function dateDayKey(ts?: string | number | null): string {
  if (ts == null || ts === '') return 'no-date'
  try {
    const d = toDate(ts)
    if (isNaN(d.getTime())) return 'no-date'
    return d.toDateString()
  } catch {
    return 'no-date'
  }
}

// ─── Gateway-specific formatters ──────────────────────────────────────────────

export function formatNodeId(id: number): string {
  return `0x${id.toString(16).toUpperCase().padStart(4, '0')}`
}

export function formatUptime(seconds?: number): string {
  if (seconds === undefined || seconds === null) return '—'
  const d = Math.floor(seconds / 86400)
  const h = Math.floor((seconds % 86400) / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = seconds % 60
  const parts: string[] = []
  if (d > 0) parts.push(`${d}d`)
  if (h > 0) parts.push(`${h}h`)
  if (m > 0) parts.push(`${m}m`)
  parts.push(`${s}s`)
  return parts.join(' ')
}

export function formatBytes(bytes?: number): string {
  if (bytes === undefined) return '—'
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
}

export function formatFrequency(mhz?: number): string {
  if (mhz === undefined) return '—'
  return `${mhz} MHz`
}
