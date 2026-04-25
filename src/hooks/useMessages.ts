import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { toast } from 'sonner'
import { GatewayApiError } from '@/lib/api/client'
import { messagesApi, type MessageStatsOptions } from '@/lib/api/messages'
import type { MessagePending, MessageRecord, MessageStats, SendMessageRequest } from '@/types/api'

export const MESSAGE_HISTORY_KEY = ['messages', 'history']
export const MESSAGE_STATS_KEY = ['messages', 'stats']
export const MESSAGE_PENDING_KEY = ['messages', 'pending']

function isNotFound(error: unknown): boolean {
  return error instanceof GatewayApiError && error.status === 404
}

export function useMessageHistory(autoRefresh: boolean = false, intervalMs: number = 2000) {
  return useQuery<MessageRecord[]>({
    queryKey: MESSAGE_HISTORY_KEY,
    queryFn: ({ signal }) => messagesApi.getHistory(signal),
    refetchInterval: autoRefresh ? intervalMs : false,
    refetchIntervalInBackground: autoRefresh,
    retry: 1,
  })
}

export function useMessageStats(
  autoRefresh: boolean = true,
  intervalMs: number = 5000,
  options: MessageStatsOptions = {},
) {
  return useQuery<MessageStats>({
    queryKey: [...MESSAGE_STATS_KEY, options],
    queryFn: ({ signal }) => messagesApi.getStats(options, signal),
    refetchInterval: (query) => isNotFound(query.state.error) ? false : autoRefresh ? intervalMs : false,
    retry: (failureCount, error) => !isNotFound(error) && failureCount < 1,
  })
}

export function useMessagePending(autoRefresh: boolean = true, intervalMs: number = 3000) {
  return useQuery<MessagePending>({
    queryKey: MESSAGE_PENDING_KEY,
    queryFn: ({ signal }) => messagesApi.getPending(signal),
    refetchInterval: (query) => isNotFound(query.state.error) ? false : autoRefresh ? intervalMs : false,
    retry: (failureCount, error) => !isNotFound(error) && failureCount < 1,
  })
}

export function useSendMessage() {
  const queryClient = useQueryClient()

  return useMutation({
    mutationFn: (req: SendMessageRequest) => messagesApi.send(req),
    onSuccess: () => {
      toast.success('Message sent')
      queryClient.invalidateQueries({ queryKey: MESSAGE_HISTORY_KEY })
      queryClient.invalidateQueries({ queryKey: MESSAGE_STATS_KEY })
      queryClient.invalidateQueries({ queryKey: MESSAGE_PENDING_KEY })
    },
    onError: (err: Error) => {
      toast.error(`Send failed: ${err.message}`)
    },
  })
}
