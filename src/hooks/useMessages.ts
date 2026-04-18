import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { toast } from 'sonner'
import { messagesApi } from '@/lib/api/messages'
import type { SendMessageRequest, MessageRecord } from '@/types/api'

export const MESSAGE_HISTORY_KEY = ['messages', 'history']

export function useMessageHistory() {
  return useQuery<MessageRecord[]>({
    queryKey: MESSAGE_HISTORY_KEY,
    queryFn: ({ signal }) => messagesApi.getHistory(signal),
    retry: 1,
  })
}

export function useSendMessage() {
  const queryClient = useQueryClient()

  return useMutation({
    mutationFn: (req: SendMessageRequest) => messagesApi.send(req),
    onSuccess: () => {
      toast.success('Message sent')
      queryClient.invalidateQueries({ queryKey: MESSAGE_HISTORY_KEY })
    },
    onError: (err: Error) => {
      toast.error(`Send failed: ${err.message}`)
    },
  })
}
