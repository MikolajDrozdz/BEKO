import { apiGet, apiPost } from './client'
import type {
  SendMessageRequest,
  SendMessageResponse,
  MessageHistoryResponse,
  MessagePending,
  MessageRecord,
  MessageStats,
} from '@/types/api'

function normalizeRecord(r: Record<string, unknown>): MessageRecord {
  return {
    dst_id: (r.dst_id ?? r.destination_id ?? 0) as number,
    payload_hex: (r.payload_hex ?? r.payload ?? '') as string,
    id: (r.id ?? r.message_id) as string | number | undefined,
    src_id: r.src_id as number | undefined,
    is_ack: (r.is_ack ?? r.ack) as boolean | undefined,
    ack_required: (r.ack_required ?? r.requires_ack) as boolean | undefined,
    ack_sent: (r.ack_sent ?? r.acknowledged) as boolean | undefined,
    message_type: (r.message_type ?? r.msg_type ?? r.type ?? r.kind) as string | undefined,
    timestamp: (r.timestamp ?? r.sent_at ?? r.created_at ?? r.time) as string | undefined,
    sent_at: r.sent_at as string | undefined,
    direction: r.direction as 'sent' | 'received' | undefined,
    status: r.status as string | undefined,
    rssi: (r.rssi ?? r.signal_rssi ?? r.rx_rssi) as number | undefined,
    coded: (r.coded ?? r.is_coded) as boolean | undefined,
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

export interface MessageStatsOptions {
  range?: string
}

function buildStatsPath(options: MessageStatsOptions = {}): string {
  const params = new URLSearchParams()
  params.set('range', options.range ?? '24h')
  return `/api/messages/stats?${params.toString()}`
}

export const messagesApi = {
  send: (body: SendMessageRequest, signal?: AbortSignal) =>
    apiPost<SendMessageResponse>('/api/messages/send', body, signal),

  getHistory: async (signal?: AbortSignal): Promise<MessageRecord[]> => {
    const raw = await apiGet<MessageHistoryResponse>('/api/messages/history', signal)
    return normalizeHistory(raw)
  },

  getStats: (options: MessageStatsOptions = {}, signal?: AbortSignal) =>
    apiGet<MessageStats>(buildStatsPath(options), signal),

  getPending: (signal?: AbortSignal) =>
    apiGet<MessagePending>('/api/messages/pending', signal),
}
