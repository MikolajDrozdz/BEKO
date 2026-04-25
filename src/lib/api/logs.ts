import { apiGet } from './client'
import type { LogEntry, LogsResponse, LogsSummary } from '@/types/api'

export interface LogQuery {
  limit?: number
  level?: string
  search?: string
}

function normalizeLogEntry(item: LogEntry | string): LogEntry {
  if (typeof item === 'string') return { message: item }
  return {
    ...item,
    message: item.message ?? item.event ?? '',
    timestamp: item.timestamp ?? item.created_at,
  }
}

function normalizeLogs(raw: LogsResponse): LogEntry[] {
  if (Array.isArray(raw)) {
    return raw.map((item) => normalizeLogEntry(item as LogEntry | string))
  }
  if ('logs' in raw) {
    return raw.logs.map((item) => normalizeLogEntry(item as LogEntry | string))
  }
  return []
}

function buildLogPath(options: LogQuery = {}): string {
  const params = new URLSearchParams()
  params.set('limit', String(options.limit ?? 100))
  if (options.level) params.set('level', options.level)
  if (options.search) params.set('search', options.search)
  const query = params.toString()
  return `/api/logs/${query ? `?${query}` : ''}`
}

export const logsApi = {
  getLogs: async (options: LogQuery = {}, signal?: AbortSignal): Promise<LogEntry[]> => {
    const raw = await apiGet<LogsResponse>(buildLogPath(options), signal)
    return normalizeLogs(raw)
  },

  getSummary: (signal?: AbortSignal) =>
    apiGet<LogsSummary>('/api/logs/summary', signal),
}
