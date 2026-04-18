import { useState, useRef, useEffect, KeyboardEvent } from 'react'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { motion, AnimatePresence } from 'framer-motion'
import {
  Send,
  Radio,
  RefreshCw,
  Lock,
  ChevronDown,
  MessageSquare,
  Cpu,
} from 'lucide-react'
import { useMessageHistory, useSendMessage } from '@/hooks/useMessages'
import { useNodes } from '@/hooks/useNodes'
import { SectionHeader } from '@/components/common/SectionHeader'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { Button } from '@/components/ui/button'
import { Textarea } from '@/components/ui/textarea'
import { Badge } from '@/components/ui/badge'
import { Skeleton } from '@/components/ui/skeleton'
import { Switch } from '@/components/ui/switch'
import { Label } from '@/components/ui/label'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import { hexToText, textToHex, BROADCAST_ID } from '@/lib/utils/hex'
import { formatNodeId } from '@/lib/utils/format'
import type { MessageRecord } from '@/types/api'

const schema = z.object({
  message: z.string().min(1, 'Type a message'),
})
type FormValues = z.infer<typeof schema>

// ─── Helpers ─────────────────────────────────────────────────────────────────

function isReceived(msg: MessageRecord): boolean {
  if (msg.direction === 'received') return true
  if (msg.direction === 'sent') return false
  // fallback on status field
  if (msg.status === 'received') return true
  if (msg.status === 'sent' || msg.status === 'delivered' || msg.status === 'ok') return false
  // fallback: received = has source node but no known destination
  if (msg.src_id !== undefined && msg.dst_id === undefined) return true
  return false
}

function getMsgTimestamp(msg: MessageRecord): string | undefined {
  return msg.timestamp ?? msg.sent_at
}

function formatTime(ts?: string | number): string {
  if (!ts) return ''
  try {
    const d = typeof ts === 'number' ? new Date(ts * 1000) : new Date(ts)
    return d.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' })
  } catch {
    return ''
  }
}

function getDateKey(ts?: string): string {
  if (!ts) return 'no-date'
  try { return new Date(ts).toDateString() } catch { return 'no-date' }
}

function getDateLabel(ts?: string): string {
  if (!ts) return 'Unknown date'
  try {
    const d = new Date(ts)
    const today = new Date()
    const yesterday = new Date(today)
    yesterday.setDate(yesterday.getDate() - 1)
    if (d.toDateString() === today.toDateString()) return 'Today'
    if (d.toDateString() === yesterday.toDateString()) return 'Yesterday'
    return d.toLocaleDateString(undefined, {
      weekday: 'long', year: 'numeric', month: 'long', day: 'numeric',
    })
  } catch {
    return String(ts)
  }
}

// ─── Components ───────────────────────────────────────────────────────────────

function DateDivider({ label }: { label: string }) {
  return (
    <div className="flex items-center gap-3 py-2 sticky top-0 z-10">
      <div className="flex-1 h-px bg-teal-100 dark:bg-teal-800" />
      <span className="text-[10px] text-teal-500 dark:text-teal-400 uppercase tracking-wider font-semibold
                       bg-teal-50 dark:bg-teal-900/80 px-2 py-0.5 rounded-full border border-teal-100 dark:border-teal-800">
        {label}
      </span>
      <div className="flex-1 h-px bg-teal-100 dark:bg-teal-800" />
    </div>
  )
}

function MessageBubble({ msg }: { msg: MessageRecord }) {
  const received = isReceived(msg)
  const text = hexToText(msg.payload_hex)
  const ts = getMsgTimestamp(msg)
  const timeStr = formatTime(ts)

  const nodeId = received
    ? (msg.src_id !== undefined ? formatNodeId(msg.src_id) : 'Unknown node')
    : (msg.dst_id === BROADCAST_ID ? 'BROADCAST' : formatNodeId(msg.dst_id))

  return (
    <motion.div
      initial={{ opacity: 0, y: 8 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.16 }}
      className={`flex items-end gap-2 ${received ? 'justify-start' : 'justify-end'}`}
    >
      {/* Avatar — left for received */}
      {received && (
        <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-full bg-teal-100 dark:bg-teal-800 border border-teal-200 dark:border-teal-700 mb-0.5">
          <Cpu className="h-3.5 w-3.5 text-teal-600 dark:text-teal-400" />
        </div>
      )}

      <div className={`max-w-[68%] flex flex-col gap-1 ${received ? 'items-start' : 'items-end'}`}>

        {/* Node label */}
        <div className={`flex items-center gap-1.5 px-1 ${received ? '' : 'flex-row-reverse'}`}>
          <span className={`text-[11px] font-semibold font-mono-feature ${
            received
              ? 'text-teal-600 dark:text-teal-400'
              : 'text-ivory-600 dark:text-ivory-400'
          }`}>
            {received ? `← ${nodeId}` : `→ ${nodeId}`}
          </span>
          {msg.coded && (
            <Lock className="h-2.5 w-2.5 text-ivory-500 dark:text-ivory-400" />
          )}
        </div>

        {/* Bubble */}
        <div className={`relative rounded-2xl px-4 py-2.5 text-sm leading-relaxed break-words shadow-sm ${
          received
            ? 'rounded-tl-none bg-white dark:bg-teal-800 border border-teal-200 dark:border-teal-700 text-teal-900 dark:text-teal-50'
            : 'rounded-tr-none bg-ivory-600 dark:bg-ivory-700 text-white'
        }`}>
          {text || (
            <span className={`font-mono-feature text-[11px] ${received ? 'opacity-60' : 'opacity-75'}`}>
              {msg.payload_hex}
            </span>
          )}
        </div>

        {/* Time + meta */}
        <div className={`flex items-center gap-2 px-1 ${received ? '' : 'flex-row-reverse'}`}>
          {timeStr && (
            <span className="text-[10px] text-teal-400 dark:text-teal-600 tabular-nums">
              {timeStr}
            </span>
          )}
          {msg.rssi !== undefined && (
            <span className="text-[10px] text-teal-400 dark:text-teal-600 font-mono-feature">
              {msg.rssi} dBm
            </span>
          )}
          {msg.status && (
            <Badge
              variant={msg.status === 'sent' || msg.status === 'ok' ? 'success' : 'default'}
              className="text-[9px] py-0 h-3.5"
            >
              {msg.status}
            </Badge>
          )}
        </div>
      </div>

      {/* Avatar — right for sent */}
      {!received && (
        <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-full bg-ivory-100 dark:bg-ivory-900/50 border border-ivory-200 dark:border-ivory-800/50 mb-0.5">
          <Radio className="h-3.5 w-3.5 text-ivory-600 dark:text-ivory-400" />
        </div>
      )}
    </motion.div>
  )
}

// ─── Main page ────────────────────────────────────────────────────────────────

type DayGroup = { dateKey: string; label: string; messages: MessageRecord[] }

function groupByDate(messages: MessageRecord[]): DayGroup[] {
  const groups: DayGroup[] = []
  for (const msg of messages) {
    const ts = getMsgTimestamp(msg)
    const key = getDateKey(ts)
    const last = groups[groups.length - 1]
    if (last && last.dateKey === key) {
      last.messages.push(msg)
    } else {
      groups.push({ dateKey: key, label: getDateLabel(ts), messages: [msg] })
    }
  }
  return groups
}

export function MessengerPage() {
  const { data: messages, isLoading, error, refetch } = useMessageHistory()
  const { data: nodes } = useNodes()
  const sendMessage = useSendMessage()

  const [recipient, setRecipient] = useState(String(BROADCAST_ID))
  const [coded, setCoded] = useState(false)
  const [showScrollBtn, setShowScrollBtn] = useState(false)

  const bottomRef = useRef<HTMLDivElement>(null)
  const scrollRef = useRef<HTMLDivElement>(null)

  const form = useForm<FormValues>({
    resolver: zodResolver(schema),
    defaultValues: { message: '' },
  })

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages?.length])

  function scrollToBottom(behavior: ScrollBehavior = 'smooth') {
    bottomRef.current?.scrollIntoView({ behavior })
  }

  function handleScroll() {
    const el = scrollRef.current
    if (!el) return
    setShowScrollBtn(el.scrollHeight - el.scrollTop - el.clientHeight > 120)
  }

  function onSubmit(values: FormValues) {
    if (!recipient) return
    sendMessage.mutate({
      dst_id: Number(recipient),
      payload_hex: textToHex(values.message),
      coded,
    })
    form.reset()
  }

  function handleKeyDown(e: KeyboardEvent<HTMLTextAreaElement>) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      form.handleSubmit(onSubmit)()
    }
  }

  const isBroadcast = recipient === String(BROADCAST_ID)

  const sorted = [...(messages ?? [])].sort((a, b) => {
    const ta = getMsgTimestamp(a) ?? ''
    const tb = getMsgTimestamp(b) ?? ''
    return ta < tb ? -1 : ta > tb ? 1 : 0
  })

  const groups = groupByDate(sorted)

  return (
    <div className="flex flex-col h-[calc(100vh-11rem)]">
      <SectionHeader
        title="Messenger"
        description="Send and receive messages via the LoRa gateway"
        actions={
          <Button variant="outline" size="sm" onClick={() => refetch()}>
            <RefreshCw className="h-3.5 w-3.5" />
            Refresh
          </Button>
        }
      />

      {/* Chat card */}
      <div className="relative flex flex-col flex-1 mt-4 rounded-xl border border-teal-200 dark:border-teal-800 bg-teal-50/40 dark:bg-teal-950/40 overflow-hidden shadow-sm">

        {/* Legend / stats bar */}
        <div className="flex items-center justify-between px-4 py-1.5 border-b border-teal-100 dark:border-teal-800 bg-white/80 dark:bg-teal-900/80 shrink-0">
          <div className="flex items-center gap-3">
            <div className="flex items-center gap-1.5">
              <div className="h-2.5 w-2.5 rounded-full bg-teal-200 dark:bg-teal-700 border border-teal-300 dark:border-teal-600" />
              <span className="text-[10px] text-teal-500 dark:text-teal-400 flex items-center gap-1">
                <Cpu className="h-2.5 w-2.5" /> Received
              </span>
            </div>
            <div className="flex items-center gap-1.5">
              <div className="h-2.5 w-2.5 rounded-full bg-ivory-400 dark:bg-ivory-600" />
              <span className="text-[10px] text-teal-500 dark:text-teal-400 flex items-center gap-1">
                <Radio className="h-2.5 w-2.5" /> Sent
              </span>
            </div>
          </div>
          {messages && (
            <span className="text-[10px] text-teal-400 dark:text-teal-600 tabular-nums">
              {messages.length} messages
            </span>
          )}
        </div>

        {/* Messages */}
        <div
          ref={scrollRef}
          onScroll={handleScroll}
          className="flex-1 overflow-y-auto px-4 py-4 space-y-1"
        >
          {isLoading && (
            <div className="space-y-4 pt-2">
              {[72, 48, 96, 56].map((w, i) => (
                <div key={i} className={`flex ${i % 2 === 0 ? 'justify-end' : 'justify-start'}`}>
                  <Skeleton className={`h-12 rounded-2xl w-${w === 48 ? '36' : w === 56 ? '40' : w === 72 ? '48' : '56'}`} />
                </div>
              ))}
            </div>
          )}

          {error && <ErrorDisplay error={error} onRetry={refetch} />}

          {!isLoading && !error && sorted.length === 0 && (
            <div className="flex flex-col items-center justify-center h-full gap-3 text-center py-16">
              <div className="h-14 w-14 rounded-full bg-teal-100 dark:bg-teal-800 flex items-center justify-center">
                <MessageSquare className="h-6 w-6 text-teal-400 dark:text-teal-500" />
              </div>
              <p className="text-sm font-medium text-teal-600 dark:text-teal-400">No messages yet</p>
              <p className="text-xs text-teal-400 dark:text-teal-600 max-w-xs">
                Send a message below — received messages from nodes will also appear here
              </p>
            </div>
          )}

          {!isLoading && groups.map(({ dateKey, label, messages: dayMsgs }) => (
            <div key={dateKey} className="space-y-3">
              <DateDivider label={label} />
              <AnimatePresence initial={false}>
                {dayMsgs.map((msg, idx) => (
                  <MessageBubble key={msg.id ?? `${dateKey}-${idx}`} msg={msg} />
                ))}
              </AnimatePresence>
            </div>
          ))}

          <div ref={bottomRef} />
        </div>

        {/* Scroll-to-bottom */}
        <AnimatePresence>
          {showScrollBtn && (
            <motion.button
              initial={{ opacity: 0, scale: 0.8 }}
              animate={{ opacity: 1, scale: 1 }}
              exit={{ opacity: 0, scale: 0.8 }}
              onClick={() => scrollToBottom()}
              className="absolute bottom-20 right-5 z-20 flex h-8 w-8 items-center justify-center rounded-full bg-ivory-600 dark:bg-ivory-700 text-white shadow-lg hover:bg-ivory-700 dark:hover:bg-ivory-600 transition-colors"
            >
              <ChevronDown className="h-4 w-4" />
            </motion.button>
          )}
        </AnimatePresence>

        {/* Compose bar */}
        <div className="shrink-0 border-t border-teal-100 dark:border-teal-800 bg-white dark:bg-teal-900 px-4 py-3 space-y-2">
          {/* Top row: recipient + options */}
          <div className="flex items-center gap-2">
            <Select value={recipient} onValueChange={setRecipient}>
              <SelectTrigger className="h-8 text-xs w-48 border-teal-200 dark:border-teal-700">
                <SelectValue placeholder="Select recipient..." />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value={String(BROADCAST_ID)}>
                  <div className="flex items-center gap-2">
                    <Radio className="h-3.5 w-3.5" />
                    <span>Broadcast (all nodes)</span>
                  </div>
                </SelectItem>
                {nodes?.map((node) => {
                  const id = node.node_id ?? node.id ?? 0
                  return (
                    <SelectItem key={id} value={String(id)}>
                      <span className="font-mono-feature">{formatNodeId(id)}</span>
                      {node.name && <span className="ml-1.5 text-teal-500">— {node.name}</span>}
                    </SelectItem>
                  )
                })}
              </SelectContent>
            </Select>
            {isBroadcast && (
              <Badge variant="warning" className="text-[10px]">Broadcast</Badge>
            )}
          </div>

          {/* Bottom row: textarea + coded + send */}
          <form onSubmit={form.handleSubmit(onSubmit)} className="flex items-end gap-2">
            <div className="flex-1 min-w-0">
              <Textarea
                placeholder={`Message to ${isBroadcast ? 'all nodes' : formatNodeId(Number(recipient))}… (Enter to send, Shift+Enter for newline)`}
                rows={1}
                className="resize-none text-sm min-h-[40px] max-h-28 overflow-y-auto"
                onKeyDown={handleKeyDown}
                {...form.register('message')}
              />
            </div>
            <div className="flex flex-col items-center gap-1 shrink-0 pb-0.5">
              <Label htmlFor="coded-msg" className="text-[10px] text-teal-500 dark:text-teal-400 cursor-pointer select-none">
                Coded
              </Label>
              <Switch id="coded-msg" checked={coded} onCheckedChange={setCoded} className="scale-90" />
            </div>
            <Button
              type="submit"
              loading={sendMessage.isPending}
              disabled={!recipient}
              className="shrink-0 h-10"
            >
              <Send className="h-4 w-4" />
              Send
            </Button>
          </form>
          {form.formState.errors.message && (
            <p className="text-xs text-red-500 mt-1">{form.formState.errors.message.message}</p>
          )}
        </div>
      </div>
    </div>
  )
}
