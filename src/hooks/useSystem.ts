import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { toast } from 'sonner'
import { systemApi } from '@/lib/api/system'
import type { SystemInfo } from '@/types/api'

export const SYSTEM_KEY = ['system']

export function useSystemInfo() {
  return useQuery<SystemInfo>({
    queryKey: SYSTEM_KEY,
    queryFn: ({ signal }) => systemApi.getInfo(signal),
    refetchInterval: 30000,
    retry: 1,
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
