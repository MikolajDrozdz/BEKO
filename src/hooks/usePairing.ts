import { useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { toast } from 'sonner'
import { pairingApi } from '@/lib/api/pairing'
import type { PairingStatus } from '@/types/api'

export const PAIRING_STATUS_KEY = ['pairing', 'status']

export function usePairingStatus(enabled: boolean) {
  return useQuery<PairingStatus>({
    queryKey: PAIRING_STATUS_KEY,
    queryFn: ({ signal }) => pairingApi.getStatus(signal),
    enabled,
    refetchInterval: 2000,
    retry: false,
  })
}

export function usePairingActions() {
  const queryClient = useQueryClient()
  const [isPolling, setIsPolling] = useState(false)

  const startMutation = useMutation({
    mutationFn: (targetNodeId?: number) => pairingApi.start(targetNodeId),
    onSuccess: () => {
      toast.success('PAIR_REQ sent — waiting for node response')
      setIsPolling(true)
      queryClient.invalidateQueries({ queryKey: PAIRING_STATUS_KEY })
    },
    onError: (err: Error) => {
      toast.error(`Failed to start pairing: ${err.message}`)
    },
  })

  return {
    isPolling,
    setIsPolling,
    start: startMutation,
  }
}
