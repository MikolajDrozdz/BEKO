import { useQuery } from '@tanstack/react-query'
import { logsApi, type LogQuery } from '@/lib/api/logs'
import type { LogEntry, LogsSummary } from '@/types/api'

export const LOGS_KEY = ['logs']
export const LOGS_SUMMARY_KEY = ['logs', 'summary']

export function useLogs(autoRefresh: boolean = false, options: LogQuery = {}) {
  return useQuery<LogEntry[]>({
    queryKey: [...LOGS_KEY, options],
    queryFn: ({ signal }) => logsApi.getLogs(options, signal),
    refetchInterval: autoRefresh ? 5000 : false,
    retry: 1,
  })
}

export function useLogsSummary(autoRefresh: boolean = true, intervalMs: number = 5000) {
  return useQuery<LogsSummary>({
    queryKey: LOGS_SUMMARY_KEY,
    queryFn: ({ signal }) => logsApi.getSummary(signal),
    refetchInterval: autoRefresh ? intervalMs : false,
    retry: 1,
  })
}
