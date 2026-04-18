import { apiGet, apiPost } from './client'
import type { PairingStatus, PairingStartResponse } from '@/types/api'

export const pairingApi = {
  start: (targetNodeId?: number, signal?: AbortSignal) =>
    apiPost<PairingStartResponse>('/api/pairing/start', { target_node_id: targetNodeId ?? 65535 }, signal),

  getStatus: (signal?: AbortSignal) =>
    apiGet<PairingStatus>('/api/pairing/status', signal),
}
