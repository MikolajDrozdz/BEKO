import { apiGet } from './client'
import type { LogEntry, LogsResponse } from '@/types/api'

function normalizeLogs(raw: LogsResponse): LogEntry[] {
  if (Array.isArray(raw)) {
    return raw.map((item) => {
      if (typeof item === 'string') return { message: item }
      return item as LogEntry
    })
  }
  if ('logs' in raw) {
    return raw.logs.map((item) => {
      if (typeof item === 'string') return { message: item }
      return item as LogEntry
    })
  }
  return []
}

export const logsApi = {
  getLogs: async (signal?: AbortSignal): Promise<LogEntry[]> => {
    const raw = await apiGet<LogsResponse>('/api/logs', signal)
    return normalizeLogs(raw)
  },
}
