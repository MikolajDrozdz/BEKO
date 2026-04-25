import { apiGet, apiPost } from './client'
import type { PairingStatus, PairingStartResponse } from '@/types/api'
import { BROADCAST_ID } from '@/lib/utils/protocol'

export const pairingApi = {
  start: (targetNodeId?: number, signal?: AbortSignal) =>
    apiPost<PairingStartResponse>('/api/pairing/start', { target_node_id: targetNodeId ?? BROADCAST_ID }, signal),

  getStatus: (signal?: AbortSignal) =>
    apiGet<PairingStatus>('/api/pairing/status', signal),
}
