import { apiGet, apiPost } from './client'
import type {
  GatewayInfo,
  RadioStatus,
  SystemInfo,
  SystemMetric,
  SystemMetricsHistoryResponse,
  SystemPairResponse,
} from '@/types/api'

function normalizeMetrics(raw: SystemMetricsHistoryResponse): SystemMetric[] {
  if (Array.isArray(raw)) return raw
  if ('metrics' in raw) return raw.metrics
  return []
}

export interface SystemMetricsHistoryOptions {
  range?: string
  step?: string
}

function buildMetricsHistoryPath(options: SystemMetricsHistoryOptions = {}): string {
  const params = new URLSearchParams()
  params.set('range', options.range ?? '1h')
  params.set('step', options.step ?? '10s')
  return `/api/system/metrics/history?${params.toString()}`
}

export const systemApi = {
  getInfo: (signal?: AbortSignal) =>
    apiGet<SystemInfo>('/api/system/', signal),

  getGatewayInfo: (signal?: AbortSignal) =>
    apiGet<GatewayInfo>('/api/system/gateway', signal),

  getMetricsHistory: async (
    options: SystemMetricsHistoryOptions = {},
    signal?: AbortSignal,
  ): Promise<SystemMetric[]> => {
    const raw = await apiGet<SystemMetricsHistoryResponse>(buildMetricsHistoryPath(options), signal)
    return normalizeMetrics(raw)
  },

  getRadioStatus: (signal?: AbortSignal) =>
    apiGet<RadioStatus>('/api/radio/status', signal),

  forcePair: (signal?: AbortSignal) =>
    apiPost<SystemPairResponse>('/api/system/pair', undefined, signal),
}
