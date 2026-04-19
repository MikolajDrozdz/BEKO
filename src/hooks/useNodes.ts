import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { toast } from 'sonner'
import { nodesApi } from '@/lib/api/nodes'
import type { Node } from '@/types/api'

export const NODES_KEY = ['nodes']

export function useNodes() {
  return useQuery<Node[]>({
    queryKey: NODES_KEY,
    queryFn: ({ signal }) => nodesApi.getAll(signal),
    retry: 1,
  })
}

export function useDeleteNode() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (nodeId: number) => nodesApi.delete(nodeId),
    onSuccess: (_, nodeId) => {
      toast.success(`Node ${nodeId} removed`)
      queryClient.invalidateQueries({ queryKey: NODES_KEY })
    },
    onError: (err: Error) => {
      toast.error(`Delete failed: ${err.message}`)
    },
  })
}

export function useRotateKeys() {
  const queryClient = useQueryClient()
  return useMutation({
    mutationFn: (nodeId: number) => nodesApi.rotateKeys(nodeId),
    onSuccess: (_, nodeId) => {
      toast.success(`Keys rotated for node ${nodeId}`)
      queryClient.invalidateQueries({ queryKey: NODES_KEY })
    },
    onError: (err: Error) => {
      toast.error(`Key rotation failed: ${err.message}`)
    },
  })
}
