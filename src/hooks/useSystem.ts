import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { toast } from 'sonner'
import { GatewayApiError } from '@/lib/api/client'
import { systemApi, type SystemMetricsHistoryOptions } from '@/lib/api/system'
import type { GatewayInfo, RadioStatus, SystemInfo, SystemMetric } from '@/types/api'

export const SYSTEM_KEY = ['system']
export const SYSTEM_METRICS_KEY = ['system', 'metrics']
export const RADIO_STATUS_KEY = ['radio', 'status']
export const GATEWAY_INFO_KEY = ['system', 'gateway']

function isNotFound(error: unknown): boolean {
  return error instanceof GatewayApiError && error.status === 404
}

export function useSystemInfo(autoRefresh: boolean = true, intervalMs: number = 30000) {
  return useQuery<SystemInfo>({
    queryKey: SYSTEM_KEY,
    queryFn: ({ signal }) => systemApi.getInfo(signal),
    refetchInterval: autoRefresh ? intervalMs : false,
    retry: 1,
  })
}

export function useSystemMetricsHistory(
  autoRefresh: boolean = true,
  intervalMs: number = 10000,
  options: SystemMetricsHistoryOptions = {},
) {
  return useQuery<SystemMetric[]>({
    queryKey: [...SYSTEM_METRICS_KEY, options],
    queryFn: ({ signal }) => systemApi.getMetricsHistory(options, signal),
    refetchInterval: (query) => isNotFound(query.state.error) ? false : autoRefresh ? intervalMs : false,
    retry: (failureCount, error) => !isNotFound(error) && failureCount < 1,
  })
}

export function useGatewayInfo(autoRefresh: boolean = true, intervalMs: number = 5000) {
  return useQuery<GatewayInfo>({
    queryKey: GATEWAY_INFO_KEY,
    queryFn: ({ signal }) => systemApi.getGatewayInfo(signal),
    refetchInterval: (query) => isNotFound(query.state.error) ? false : autoRefresh ? intervalMs : false,
    retry: (failureCount, error) => !isNotFound(error) && failureCount < 1,
  })
}

export function useRadioStatus(autoRefresh: boolean = true, intervalMs: number = 5000) {
  return useQuery<RadioStatus>({
    queryKey: RADIO_STATUS_KEY,
    queryFn: ({ signal }) => systemApi.getRadioStatus(signal),
    refetchInterval: (query) => isNotFound(query.state.error) ? false : autoRefresh ? intervalMs : false,
    retry: (failureCount, error) => !isNotFound(error) && failureCount < 1,
  })
}

export function useForcePair() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: () => systemApi.forcePair(),
    onSuccess: () => {
      toast.success('Force pair initiated')
      queryClient.invalidateQueries({ queryKey: SYSTEM_KEY })
    },
    onError: (err: Error) => {
      toast.error(`Force pair failed: ${err.message}`)
    },
  })
}
