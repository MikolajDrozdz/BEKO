import { apiGet, apiPost } from './client'
import type { SystemInfo, SystemPairResponse } from '@/types/api'

export const systemApi = {
  getInfo: (signal?: AbortSignal) =>
    apiGet<SystemInfo>('/api/system/', signal),

  forcePair: (signal?: AbortSignal) =>
    apiPost<SystemPairResponse>('/api/system/pair', undefined, signal),
}
