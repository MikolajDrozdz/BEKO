import { apiGet, apiPost } from './client'
import type {
  SendMessageRequest,
  SendMessageResponse,
  MessageHistoryResponse,
  MessageRecord,
} from '@/types/api'

function normalizeRecord(r: Record<string, unknown>): MessageRecord {
  return {
    ...(r as MessageRecord),
    timestamp: (r.timestamp ?? r.sent_at ?? r.created_at ?? r.time) as string | undefined,
    coded: (r.coded ?? r.is_coded) as boolean | undefined,
    rssi: (r.rssi ?? r.signal_rssi ?? r.rx_rssi) as number | undefined,
  }
}

function normalizeHistory(raw: MessageHistoryResponse): MessageRecord[] {
  const list: MessageRecord[] = Array.isArray(raw)
    ? raw
    : 'messages' in raw
      ? raw.messages
      : []
  return list.map((r) => normalizeRecord(r as unknown as Record<string, unknown>))
}

export const messagesApi = {
  send: (body: SendMessageRequest, signal?: AbortSignal) =>
    apiPost<SendMessageResponse>('/api/messages/send', body, signal),

  getHistory: async (signal?: AbortSignal): Promise<MessageRecord[]> => {
    const raw = await apiGet<MessageHistoryResponse>('/api/messages/history', signal)
    return normalizeHistory(raw)
  },
}
