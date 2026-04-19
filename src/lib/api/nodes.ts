import { apiDelete, apiGet, apiPost } from './client'
import type { Node, NodesResponse, NodeActionResponse } from '@/types/api'

function normalizeNodes(raw: NodesResponse): Node[] {
  if (Array.isArray(raw)) return raw
  if ('nodes' in raw) return raw.nodes
  return []
}

export const nodesApi = {
  getAll: async (signal?: AbortSignal): Promise<Node[]> => {
    const raw = await apiGet<NodesResponse>('/api/nodes/', signal)
    return normalizeNodes(raw)
  },

  delete: (nodeId: number, signal?: AbortSignal) =>
    apiDelete<NodeActionResponse>(`/api/nodes/${nodeId}`, signal),

  rotateKeys: (nodeId: number, signal?: AbortSignal) =>
    apiPost<NodeActionResponse>(`/api/system/nodes/${nodeId}/rotate_keys`, undefined, signal),
}
