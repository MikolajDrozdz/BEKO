import { useMemo, useState, useRef, useEffect, KeyboardEvent } from 'react'
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
  Pencil,
  AlertTriangle,
  Clock,
  CheckCheck,
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
import { Input } from '@/components/ui/input'
import { Alert, AlertDescription } from '@/components/ui/alert'
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from '@/components/ui/dialog'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import { hexToText, textToHex } from '@/lib/utils/hex'
import {
  BROADCAST_ID,
  MAX_PAYLOAD_BYTES,
  getMessageStatusLabel,
  getSendTargetError,
  getUtf8ByteLength,
  isAsciiText,
  isAskActText,
  isAskActType,
  isNodeAddress,
  isSendTargetAddress,
  requiresNodeResponse,
} from '@/lib/utils/protocol'
import { formatNodeId } from '@/lib/utils/format'
import type { MessageRecord } from '@/types/api'

const schema = z.object({
  message: z.string().min(1, 'Type a message'),
})
type FormValues = z.infer<typeof schema>
type AddressBook = Record<string, string>
type ResponsePrompt = {
  key: string
  message: MessageRecord
  text: string
  expiresAt: number
}

const ALL_CHATS = 'all'
const ADDRESS_BOOK_KEY = 'beko_chat_address_book'
const RESPONSE_TIMEOUT_SECONDS = 30

// ─── Helpers ─────────────────────────────────────────────────────────────────

function isReceived(msg: MessageRecord): boolean {
  if (msg.direction === 'received') return true
  if (msg.direction === 'sent') return false
  // fallback on status field
  if (msg.status === 'received' || msg.status === 'response') return true
  if (
    msg.status === 'pending' ||
    msg.status === 'sent' ||
    msg.status === 'failed' ||
    msg.status === 'delivered' ||
    msg.status === 'sent_waiting_response' ||
    msg.status === 'delivered_waiting_response' ||
    msg.status === 'answered' ||
    msg.status === 'ok'
  ) return false
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

function loadAddressBook(): AddressBook {
  try {
    return JSON.parse(localStorage.getItem(ADDRESS_BOOK_KEY) ?? '{}') as AddressBook
  } catch {
    return {}
  }
}

function saveAddressBook(book: AddressBook): void {
  localStorage.setItem(ADDRESS_BOOK_KEY, JSON.stringify(book))
}

function getAddressLabel(id: number, book: AddressBook): string {
  if (id === BROADCAST_ID) return 'Broadcast'
  return book[String(id)]?.trim() || formatNodeId(id)
}

function getMessagePeerId(msg: MessageRecord): number {
  if (isReceived(msg)) return msg.src_id ?? msg.dst_id
  return msg.dst_id
}

function getStatusVariant(status?: string) {
  if (status === 'failed') return 'destructive'
  if (status === 'pending' || status?.includes('waiting_response')) return 'warning'
  if (status === 'delivered' || status === 'answered' || status === 'received' || status === 'response') return 'success'
  if (status === 'sent' || status === 'ok') return 'default'
  return 'outline'
}

function getMessageKey(msg: MessageRecord): string {
  return String(msg.id ?? `${msg.src_id ?? 'src'}-${msg.dst_id}-${getMsgTimestamp(msg) ?? 'time'}-${msg.payload_hex}`)
}

function isResponsePromptMessage(msg: MessageRecord): boolean {
  if (!isReceived(msg)) return false
  if (msg.status === 'response') return false
  if (!isNodeAddress(msg.src_id ?? 0)) return false

  const text = hexToText(msg.payload_hex)
  return isAskActType(msg.message_type) || isAskActText(text) || requiresNodeResponse(text)
}

function isAckConfirmedReceived(msg: MessageRecord): boolean {
  if (!isReceived(msg)) return false
  if (msg.is_ack || msg.status === 'response') return false
  if (!isNodeAddress(msg.src_id ?? 0)) return false
  return msg.ack_sent ?? msg.ack_required ?? true
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

function MessageBubble({ msg, addressBook }: { msg: MessageRecord; addressBook: AddressBook }) {
  const received = isReceived(msg)
  const text = hexToText(msg.payload_hex)
  const ts = getMsgTimestamp(msg)
  const timeStr = formatTime(ts)
  const peerId = getMessagePeerId(msg)

  const nodeId = peerId !== undefined ? getAddressLabel(peerId, addressBook) : 'Unknown node'
  const nodeAddress = peerId !== undefined ? formatNodeId(peerId) : undefined

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
          {nodeAddress && nodeAddress !== nodeId && (
            <span className="font-mono-feature text-[10px] text-teal-400 dark:text-teal-600">
              {nodeAddress}
            </span>
          )}
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
              variant={getStatusVariant(msg.status)}
              className="h-auto max-w-[220px] whitespace-normal py-0 text-[9px]"
            >
              {getMessageStatusLabel(msg.status)}
            </Badge>
          )}
          {isAckConfirmedReceived(msg) && (
            <span
              className="flex items-center gap-1 text-[10px] font-medium text-ivory-600 dark:text-ivory-400"
              title="ACK wysłany przez gateway"
            >
              <CheckCheck className="h-3 w-3" />
              Potwierdzona
            </span>
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
  const { data: messages, isLoading, error, refetch } = useMessageHistory(true, 1000)
  const { data: nodes } = useNodes()
  const sendMessage = useSendMessage()

  const [recipient, setRecipient] = useState(String(BROADCAST_ID))
  const [chatFilter, setChatFilter] = useState(ALL_CHATS)
  const [addressBook, setAddressBook] = useState<AddressBook>(() => loadAddressBook())
  const [nameDialogOpen, setNameDialogOpen] = useState(false)
  const [nameInput, setNameInput] = useState('')
  const [responsePrompt, setResponsePrompt] = useState<ResponsePrompt | null>(null)
  const [dismissedResponsePrompts, setDismissedResponsePrompts] = useState<Set<string>>(() => new Set())
  const [historyInitialized, setHistoryInitialized] = useState(false)
  const [customResponse, setCustomResponse] = useState('')
  const [responseError, setResponseError] = useState('')
  const [now, setNow] = useState(() => Date.now())
  const [coded, setCoded] = useState(false)
  const [ackRequired, setAckRequired] = useState(true)
  const [showScrollBtn, setShowScrollBtn] = useState(false)

  const bottomRef = useRef<HTMLDivElement>(null)
  const scrollRef = useRef<HTMLDivElement>(null)

  const form = useForm<FormValues>({
    resolver: zodResolver(schema),
    defaultValues: { message: '' },
  })

  const watchMessage = form.watch('message') ?? ''
  const recipientId = Number(recipient)
  const destinationError = getSendTargetError(recipientId)
  const payloadByteLength = getUtf8ByteLength(watchMessage)
  const payloadLengthError = payloadByteLength > MAX_PAYLOAD_BYTES
    ? `Payload ma ${payloadByteLength} B, limit ramki to ${MAX_PAYLOAD_BYTES} B.`
    : undefined
  const asciiError = !isAsciiText(watchMessage) ? 'Wiadomość może zawierać tylko znaki ASCII.' : undefined
  const responseRequired = requiresNodeResponse(watchMessage)
  const responseSecondsLeft = responsePrompt
    ? Math.max(0, Math.ceil((responsePrompt.expiresAt - now) / 1000))
    : 0
  const isBroadcast = recipient === String(BROADCAST_ID)
  const effectiveAckRequired = !isBroadcast && ackRequired

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages?.length, chatFilter])

  useEffect(() => {
    const interval = window.setInterval(() => setNow(Date.now()), 1000)
    return () => window.clearInterval(interval)
  }, [])

  useEffect(() => {
    if (!responsePrompt || responsePrompt.expiresAt > now) return
    setDismissedResponsePrompts((current) => new Set(current).add(responsePrompt.key))
    setResponsePrompt(null)
    setCustomResponse('')
    setResponseError('')
  }, [now, responsePrompt])

  useEffect(() => {
    if (!messages || historyInitialized) return

    setDismissedResponsePrompts((current) => {
      const next = new Set(current)
      for (const msg of messages) {
        if (isResponsePromptMessage(msg)) next.add(getMessageKey(msg))
      }
      return next
    })
    setHistoryInitialized(true)
  }, [historyInitialized, messages])

  useEffect(() => {
    if (!historyInitialized) return
    if (responsePrompt && responsePrompt.expiresAt > Date.now()) return

    const promptMessage = [...(messages ?? [])]
      .sort((a, b) => {
        const ta = getMsgTimestamp(a) ?? ''
        const tb = getMsgTimestamp(b) ?? ''
        return ta > tb ? -1 : ta < tb ? 1 : 0
      })
      .find((msg) => {
        const key = getMessageKey(msg)
        return !dismissedResponsePrompts.has(key) && isResponsePromptMessage(msg)
      })

    if (!promptMessage) return

    setResponsePrompt({
      key: getMessageKey(promptMessage),
      message: promptMessage,
      text: hexToText(promptMessage.payload_hex) || promptMessage.payload_hex,
      expiresAt: Date.now() + RESPONSE_TIMEOUT_SECONDS * 1000,
    })
    setCustomResponse('')
    setResponseError('')
  }, [dismissedResponsePrompts, historyInitialized, messages, responsePrompt])

  useEffect(() => {
    const nodeNames = (nodes ?? []).reduce<AddressBook>((acc, node) => {
      const id = node.node_id ?? node.id
      if (id !== undefined && isNodeAddress(id) && node.name && !acc[String(id)]) {
        acc[String(id)] = node.name
      }
      return acc
    }, {})

    if (Object.keys(nodeNames).length === 0) return

    setAddressBook((current) => {
      const next = { ...nodeNames, ...current }
      if (JSON.stringify(next) === JSON.stringify(current)) return current
      saveAddressBook(next)
      return next
    })
  }, [nodes])

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
    if (destinationError) {
      form.setError('message', { message: destinationError })
      return
    }
    if (payloadLengthError) {
      form.setError('message', { message: payloadLengthError })
      return
    }
    if (asciiError) {
      form.setError('message', { message: asciiError })
      return
    }
    sendMessage.mutate({
      dst_id: recipientId,
      payload_hex: textToHex(values.message),
      coded,
      ack_required: effectiveAckRequired,
    })
    form.reset()
  }

  function handleKeyDown(e: KeyboardEvent<HTMLTextAreaElement>) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      form.handleSubmit(onSubmit)()
    }
  }

  function openNameDialog(id: number) {
    setNameInput(addressBook[String(id)] ?? '')
    setNameDialogOpen(true)
  }

  function handleSaveName() {
    const id = Number(recipient)
    const next = { ...addressBook }
    const trimmed = nameInput.trim()
    if (trimmed) {
      next[String(id)] = trimmed
    } else {
      delete next[String(id)]
    }
    setAddressBook(next)
    saveAddressBook(next)
    setNameDialogOpen(false)
  }

  function closeResponsePrompt() {
    if (responsePrompt) {
      setDismissedResponsePrompts((current) => new Set(current).add(responsePrompt.key))
    }
    setResponsePrompt(null)
    setCustomResponse('')
    setResponseError('')
  }

  function sendResponse(text: string) {
    if (!responsePrompt) return

    const responseText = text.trim()
    if (!responseText) {
      setResponseError('Wpisz odpowiedź.')
      return
    }

    if (!isAsciiText(responseText)) {
      setResponseError('Odpowiedź może zawierać tylko znaki ASCII.')
      return
    }

    const byteLength = getUtf8ByteLength(responseText)
    if (byteLength > MAX_PAYLOAD_BYTES) {
      setResponseError(`Odpowiedź ma ${byteLength} B, limit ramki to ${MAX_PAYLOAD_BYTES} B.`)
      return
    }

    const dstId = responsePrompt.message.src_id
    if (dstId === undefined || !isNodeAddress(dstId)) {
      setResponseError('Nie można ustalić poprawnego adresu nadawcy.')
      return
    }

    sendMessage.mutate({
      dst_id: dstId,
      payload_hex: textToHex(responseText),
      coded: false,
    })
    closeResponsePrompt()
  }

  const addressOptions = useMemo(() => {
    const ids = new Set<number>([BROADCAST_ID])
    for (const node of nodes ?? []) {
      const id = node.node_id ?? node.id
      if (id !== undefined && isNodeAddress(id)) ids.add(id)
    }
    for (const msg of messages ?? []) {
      const peerId = getMessagePeerId(msg)
      if (isSendTargetAddress(peerId)) ids.add(peerId)
    }
    return [...ids].sort((a, b) => a - b)
  }, [messages, nodes])

  const sorted = [...(messages ?? [])].sort((a, b) => {
    const ta = getMsgTimestamp(a) ?? ''
    const tb = getMsgTimestamp(b) ?? ''
    return ta < tb ? -1 : ta > tb ? 1 : 0
  })

  const filteredMessages = sorted.filter((msg) => {
    if (chatFilter === ALL_CHATS) return true
    const id = Number(chatFilter)
    if (id === BROADCAST_ID) return msg.dst_id === BROADCAST_ID
    return msg.src_id === id || msg.dst_id === id
  })

  const groups = groupByDate(filteredMessages)
  const activeFilterLabel = chatFilter === ALL_CHATS
    ? 'All chats'
    : getAddressLabel(Number(chatFilter), addressBook)

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
        <div className="flex flex-col gap-2 px-4 py-2 border-b border-teal-100 dark:border-teal-800 bg-white/80 dark:bg-teal-900/80 shrink-0 sm:flex-row sm:items-center sm:justify-between">
          <div className="flex flex-wrap items-center gap-3">
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
          <div className="flex items-center gap-2">
            <Select value={chatFilter} onValueChange={setChatFilter}>
              <SelectTrigger className="h-7 w-52 text-xs border-teal-200 dark:border-teal-700">
                <SelectValue placeholder="Filter chat..." />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value={ALL_CHATS}>All chats</SelectItem>
                {addressOptions.map((id) => (
                  <SelectItem key={id} value={String(id)}>
                    <span>{getAddressLabel(id, addressBook)}</span>
                    {id !== BROADCAST_ID && (
                      <span className="ml-1.5 font-mono-feature text-teal-500">
                        {formatNodeId(id)}
                      </span>
                    )}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
            <span className="text-[10px] text-teal-400 dark:text-teal-600 tabular-nums">
              {filteredMessages.length}/{messages?.length ?? 0}
            </span>
          </div>
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

          {!isLoading && !error && filteredMessages.length === 0 && (
            <div className="flex flex-col items-center justify-center h-full gap-3 text-center py-16">
              <div className="h-14 w-14 rounded-full bg-teal-100 dark:bg-teal-800 flex items-center justify-center">
                <MessageSquare className="h-6 w-6 text-teal-400 dark:text-teal-500" />
              </div>
              <p className="text-sm font-medium text-teal-600 dark:text-teal-400">
                {sorted.length === 0 ? 'No messages yet' : `No messages for ${activeFilterLabel}`}
              </p>
              <p className="text-xs text-teal-400 dark:text-teal-600 max-w-xs">
                {sorted.length === 0
                  ? 'Send a message below — received messages from nodes will also appear here'
                  : 'Pick another address filter to see the rest of the conversation'}
              </p>
            </div>
          )}

          {!isLoading && groups.map(({ dateKey, label, messages: dayMsgs }) => (
            <div key={dateKey} className="space-y-3">
              <DateDivider label={label} />
              <AnimatePresence initial={false}>
                {dayMsgs.map((msg, idx) => (
                  <MessageBubble
                    key={msg.id ?? `${dateKey}-${idx}`}
                    msg={msg}
                    addressBook={addressBook}
                  />
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
                    <span>{getAddressLabel(BROADCAST_ID, addressBook)}</span>
                  </div>
                </SelectItem>
                {nodes?.filter((node) => isNodeAddress(node.node_id ?? node.id ?? 0)).map((node) => {
                  const id = node.node_id ?? node.id ?? 0
                  return (
                    <SelectItem key={id} value={String(id)}>
                      <span>{getAddressLabel(id, addressBook)}</span>
                      <span className="ml-1.5 font-mono-feature text-teal-500">{formatNodeId(id)}</span>
                    </SelectItem>
                  )
                })}
              </SelectContent>
            </Select>
            <Button
              type="button"
              variant="outline"
              size="icon-sm"
              title="Rename selected address"
              onClick={() => openNameDialog(Number(recipient))}
              disabled={!recipient || isBroadcast}
            >
              <Pencil className="h-3.5 w-3.5" />
            </Button>
            {isBroadcast && (
              <Badge variant="warning" className="text-[10px]">
                Broadcast
              </Badge>
            )}
          </div>

          <div className="flex flex-wrap items-center justify-between gap-2 text-xs">
            <div className="flex flex-wrap items-center gap-2">
              {responseRequired && (
                <Badge variant="warning">Wymaga odpowiedzi YES / OK / NO</Badge>
              )}
              {destinationError && (
                <span className="text-red-500 dark:text-red-400">{destinationError}</span>
              )}
              {payloadLengthError && (
                <span className="text-red-500 dark:text-red-400">{payloadLengthError}</span>
              )}
              {asciiError && (
                <span className="text-red-500 dark:text-red-400">{asciiError}</span>
              )}
            </div>
            <span className={payloadLengthError ? 'text-red-500 dark:text-red-400' : 'text-teal-500 dark:text-teal-400'}>
              {payloadByteLength}/{MAX_PAYLOAD_BYTES} B
            </span>
          </div>

          {/* Bottom row: textarea + coded + send */}
          <form onSubmit={form.handleSubmit(onSubmit)} className="flex items-end gap-2">
            <div className="flex-1 min-w-0">
              <Textarea
                placeholder={`Message to ${getAddressLabel(Number(recipient), addressBook)}... (Enter to send, Shift+Enter for newline)`}
                rows={1}
                className="resize-none text-sm min-h-[40px] max-h-28 overflow-y-auto"
                onKeyDown={handleKeyDown}
                {...form.register('message')}
              />
            </div>
            <div className="flex flex-col items-center gap-1 shrink-0 pb-0.5">
              <Label htmlFor="ack-msg" className="text-[10px] text-teal-500 dark:text-teal-400 cursor-pointer select-none">
                ACK
              </Label>
              <Switch
                id="ack-msg"
                checked={effectiveAckRequired}
                onCheckedChange={setAckRequired}
                disabled={isBroadcast}
                className="scale-90"
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
              disabled={!recipient || !!destinationError || !!payloadLengthError || !!asciiError}
              className="shrink-0 h-10"
            >
              <Send className="h-4 w-4" />
              Send
            </Button>
          </form>
          {form.formState.errors.message && (
            <p className="text-xs text-red-500 mt-1">{form.formState.errors.message.message}</p>
          )}
          {responseRequired && (
            <Alert variant="warning" className="py-2">
              <AlertTriangle className="h-4 w-4" />
              <AlertDescription>
                Backend oznaczy tę wiadomość jako wymagającą odpowiedzi YES / OK / NO.
              </AlertDescription>
            </Alert>
          )}
        </div>
      </div>

      <Dialog open={nameDialogOpen} onOpenChange={setNameDialogOpen}>
        <DialogContent className="max-w-sm">
          <DialogHeader>
            <DialogTitle>Address name</DialogTitle>
          </DialogHeader>
          <div className="space-y-2">
            <Label htmlFor="address-name">
              {formatNodeId(Number(recipient))}
            </Label>
            <Input
              id="address-name"
              value={nameInput}
              onChange={(e) => setNameInput(e.target.value)}
              placeholder="e.g. Reception, Line 1, Broadcast"
            />
            <p className="text-xs text-teal-500 dark:text-teal-400">
              Empty name restores the raw address label.
            </p>
          </div>
          <DialogFooter>
            <Button variant="outline" size="sm" onClick={() => setNameDialogOpen(false)}>
              Cancel
            </Button>
            <Button size="sm" onClick={handleSaveName}>
              Save
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      <Dialog
        open={!!responsePrompt}
        onOpenChange={(open) => {
          if (!open) closeResponsePrompt()
        }}
      >
        <DialogContent className="max-w-md">
          <DialogHeader>
            <DialogTitle>Wymagana odpowiedź</DialogTitle>
          </DialogHeader>

          {responsePrompt && (
            <div className="space-y-4">
              <div className="flex items-center justify-between gap-3 rounded-md border border-amber-200 bg-amber-50 px-3 py-2 dark:border-amber-800 dark:bg-amber-950/30">
                <div className="min-w-0">
                  <p className="text-xs text-amber-700 dark:text-amber-400">Nadawca</p>
                  <p className="truncate text-sm font-semibold text-teal-900 dark:text-teal-50">
                    {getAddressLabel(responsePrompt.message.src_id ?? 0, addressBook)}
                    <span className="ml-1.5 font-mono-feature text-xs font-normal text-teal-500">
                      {formatNodeId(responsePrompt.message.src_id ?? 0)}
                    </span>
                  </p>
                </div>
                <Badge variant="warning" className="shrink-0">
                  <Clock className="h-3 w-3" />
                  {responseSecondsLeft}s
                </Badge>
              </div>

              <div className="rounded-md border border-teal-100 bg-white p-3 dark:border-teal-800 dark:bg-teal-900">
                <p className="text-xs font-medium uppercase tracking-wide text-teal-500 dark:text-teal-400">
                  Wiadomość
                </p>
                <p className="mt-1 break-words text-sm text-teal-900 dark:text-teal-50">
                  {responsePrompt.text}
                </p>
                {isAskActType(responsePrompt.message.message_type) && (
                  <Badge variant="accent" className="mt-2">
                    {responsePrompt.message.message_type?.toUpperCase()}
                  </Badge>
                )}
              </div>

              <div className="grid grid-cols-3 gap-2">
                {['YES', 'OK', 'NO'].map((answer) => (
                  <Button
                    key={answer}
                    type="button"
                    variant={answer === 'NO' ? 'warning' : 'default'}
                    onClick={() => sendResponse(answer)}
                    disabled={sendMessage.isPending || responseSecondsLeft <= 0}
                  >
                    {answer}
                  </Button>
                ))}
              </div>

              <div className="space-y-2">
                <Label htmlFor="custom-response">Własna odpowiedź</Label>
                <div className="flex gap-2">
                  <Input
                    id="custom-response"
                    value={customResponse}
                    onChange={(e) => {
                      setCustomResponse(e.target.value)
                      setResponseError('')
                    }}
                    placeholder="Do 16 B UTF-8"
                    error={!!responseError}
                    onKeyDown={(e) => {
                      if (e.key === 'Enter') {
                        e.preventDefault()
                        sendResponse(customResponse)
                      }
                    }}
                  />
                  <Button
                    type="button"
                    onClick={() => sendResponse(customResponse)}
                    disabled={sendMessage.isPending || responseSecondsLeft <= 0 || !isAsciiText(customResponse)}
                  >
                    Wyślij
                  </Button>
                </div>
                <div className="flex items-center justify-between gap-2">
                  {responseError ? (
                    <p className="text-xs text-red-500">{responseError}</p>
                  ) : !isAsciiText(customResponse) ? (
                    <p className="text-xs text-red-500">Tylko ASCII.</p>
                  ) : (
                    <p className="text-xs text-teal-500 dark:text-teal-400">
                      Odpowiedź zostanie wysłana zwykłym POST /api/messages/send.
                    </p>
                  )}
                  <span className={getUtf8ByteLength(customResponse) > MAX_PAYLOAD_BYTES ? 'text-xs text-red-500' : 'text-xs text-teal-500 dark:text-teal-400'}>
                    {getUtf8ByteLength(customResponse)}/{MAX_PAYLOAD_BYTES} B
                  </span>
                </div>
              </div>

              <DialogFooter>
                <Button variant="outline" size="sm" onClick={closeResponsePrompt}>
                  Pomiń
                </Button>
              </DialogFooter>
            </div>
          )}
        </DialogContent>
      </Dialog>
    </div>
  )
}
