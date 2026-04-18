import { useQuery } from '@tanstack/react-query'
import { logsApi } from '@/lib/api/logs'
import type { LogEntry } from '@/types/api'

export const LOGS_KEY = ['logs']

export function useLogs(autoRefresh: boolean = false) {
  return useQuery<LogEntry[]>({
    queryKey: LOGS_KEY,
    queryFn: ({ signal }) => logsApi.getLogs(signal),
    refetchInterval: autoRefresh ? 5000 : false,
    retry: 1,
  })
}
