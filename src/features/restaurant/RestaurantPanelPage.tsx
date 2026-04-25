import { useEffect, useMemo, useState } from 'react'
import {
  Armchair,
  CheckCircle2,
  ChefHat,
  Clock3,
  Loader2,
  Radio,
  RefreshCw,
  Send,
  Trash2,
  UtensilsCrossed,
  XCircle,
} from 'lucide-react'
import { useMessageHistory, useSendMessage } from '@/hooks/useMessages'
import { useNodes } from '@/hooks/useNodes'
import { SectionHeader } from '@/components/common/SectionHeader'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import { textToHex } from '@/lib/utils/hex'
import { formatNodeId } from '@/lib/utils/format'
import { getMessageStatusLabel, isNodeAddress } from '@/lib/utils/protocol'
import type { MessageRecord, Node } from '@/types/api'

const ASSIGNMENTS_KEY = 'beko_restaurant_panel_assignments'
const READY_PAYLOAD = 'ready'
const READY_PAYLOAD_HEX = textToHex(READY_PAYLOAD)
const UNASSIGNED = 'unassigned'

type Place = {
  id: string
  label: string
  zone: string
  seats: number
}

type SendState = {
  phase: 'sending' | 'waiting_ack' | 'delivered' | 'failed' | 'sent'
  kind: 'assign' | 'ready'
  messageId?: string | number
  startedAt: number
  status?: string
  payloadText?: string
  error?: string
}

type Assignments = Record<string, number | undefined>
type SendStates = Record<string, SendState | undefined>

const PLACES: Place[] = [
  { id: 't01', label: 'Stolik 1', zone: 'Sala A', seats: 2 },
  { id: 't02', label: 'Stolik 2', zone: 'Sala A', seats: 2 },
  { id: 't03', label: 'Stolik 3', zone: 'Sala A', seats: 4 },
  { id: 't04', label: 'Stolik 4', zone: 'Sala A', seats: 4 },
  { id: 't05', label: 'Stolik 5', zone: 'Sala A', seats: 6 },
  { id: 't06', label: 'Stolik 6', zone: 'Sala B', seats: 2 },
  { id: 't07', label: 'Stolik 7', zone: 'Sala B', seats: 4 },
  { id: 't08', label: 'Stolik 8', zone: 'Sala B', seats: 4 },
  { id: 't09', label: 'Stolik 9', zone: 'Sala B', seats: 6 },
  { id: 't10', label: 'Stolik 10', zone: 'Sala B', seats: 8 },
  { id: 'bar01', label: 'Bar 1', zone: 'Bar', seats: 1 },
  { id: 'bar02', label: 'Bar 2', zone: 'Bar', seats: 1 },
  { id: 'bar03', label: 'Bar 3', zone: 'Bar', seats: 1 },
  { id: 'vip01', label: 'VIP 1', zone: 'VIP', seats: 4 },
  { id: 'vip02', label: 'VIP 2', zone: 'VIP', seats: 6 },
  { id: 'terrace01', label: 'Taras 1', zone: 'Taras', seats: 4 },
  { id: 'terrace02', label: 'Taras 2', zone: 'Taras', seats: 4 },
  { id: 'terrace03', label: 'Taras 3', zone: 'Taras', seats: 6 },
]

function loadAssignments(): Assignments {
  try {
    return JSON.parse(localStorage.getItem(ASSIGNMENTS_KEY) ?? '{}') as Assignments
  } catch {
    return {}
  }
}

function saveAssignments(assignments: Assignments): void {
  localStorage.setItem(ASSIGNMENTS_KEY, JSON.stringify(assignments))
}

function getNodeId(node: Node): number {
  return node.node_id ?? node.id ?? 0
}

function nodeLabel(node: Node): string {
  const id = getNodeId(node)
  return node.name ? `${node.name} (${formatNodeId(id)})` : formatNodeId(id)
}

function getStatusPhase(status?: string): SendState['phase'] {
  if (status === 'failed') return 'failed'
  if (status === 'delivered' || status === 'answered') return 'delivered'
  if (status === 'pending' || status === 'sent_waiting_response' || status === 'delivered_waiting_response') return 'waiting_ack'
  if (status === 'sent' || status === 'ok') return 'sent'
  return 'waiting_ack'
}

function getProcessLabel(state?: SendState): string {
  if (!state) return 'Gotowe do wysłania'
  const target = state.kind === 'assign' ? 'przypisania' : 'READY'
  if (state.phase === 'sending') return state.kind === 'assign' ? 'Przypisywanie miejsca' : 'Wysyłanie READY'
  if (state.phase === 'waiting_ack') return `Czeka na ACK ${target}`
  if (state.phase === 'delivered') return state.kind === 'assign' ? 'Przypisanie dostarczone' : 'READY dostarczone'
  if (state.phase === 'failed') return `Błąd ${target}`
  return state.kind === 'assign' ? 'Przypisanie wysłane' : 'READY wysłane'
}

function getProcessBadge(state?: SendState) {
  if (!state) return <Badge variant="outline">Idle</Badge>
  if (state.phase === 'sending') return <Badge variant="warning"><Loader2 className="h-3 w-3 animate-spin" />Sending</Badge>
  if (state.phase === 'waiting_ack') return <Badge variant="warning"><Clock3 className="h-3 w-3" />ACK</Badge>
  if (state.phase === 'delivered') return <Badge variant="success"><CheckCircle2 className="h-3 w-3" />Delivered</Badge>
  if (state.phase === 'failed') return <Badge variant="destructive"><XCircle className="h-3 w-3" />Failed</Badge>
  return <Badge variant="default">Sent</Badge>
}

function findMatchingMessage(messages: MessageRecord[], state: SendState): MessageRecord | undefined {
  if (state.messageId !== undefined) {
    return messages.find((message) => String(message.id) === String(state.messageId))
  }

  return [...messages]
    .sort((a, b) => {
      const ta = new Date(a.timestamp ?? a.sent_at ?? 0).getTime()
      const tb = new Date(b.timestamp ?? b.sent_at ?? 0).getTime()
      return tb - ta
    })
    .find((message) => {
      const ts = new Date(message.timestamp ?? message.sent_at ?? 0).getTime()
      const expectedPayload = state.payloadText ? textToHex(state.payloadText) : READY_PAYLOAD_HEX
      return message.payload_hex?.toLowerCase() === expectedPayload &&
        ts >= state.startedAt - 2000
    })
}

function getAssignmentPayload(place: Place): string {
  return place.label
}

function PlaceCard({
  place,
  assignedNodeId,
  nodes,
  state,
  onAssign,
  onRemove,
  onReady,
  disabled,
}: {
  place: Place
  assignedNodeId?: number
  nodes: Node[]
  state?: SendState
  onAssign: (nodeId?: number) => void
  onRemove: () => void
  onReady: () => void
  disabled?: boolean
}) {
  const node = nodes.find((item) => getNodeId(item) === assignedNodeId)
  const canSend = Boolean(assignedNodeId) && !disabled && state?.phase !== 'sending' && state?.phase !== 'waiting_ack'
  const assignmentPayload = getAssignmentPayload(place)

  return (
    <Card className="overflow-hidden">
      <CardHeader className="pb-2">
        <div className="flex items-start justify-between gap-3">
          <div className="min-w-0">
            <CardTitle className="flex items-center gap-2">
              <UtensilsCrossed className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
              {place.label}
            </CardTitle>
            <div className="mt-1 flex flex-wrap items-center gap-2 text-xs text-teal-500 dark:text-teal-400">
              <span>{place.zone}</span>
              <span className="flex items-center gap-1">
                <Armchair className="h-3 w-3" />
                {place.seats}
              </span>
            </div>
          </div>
          {getProcessBadge(state)}
        </div>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2">
          <Select
            value={assignedNodeId ? String(assignedNodeId) : UNASSIGNED}
            onValueChange={(value) => onAssign(value === UNASSIGNED ? undefined : Number(value))}
          >
            <SelectTrigger className="h-8 min-w-0 flex-1 text-xs">
              <SelectValue placeholder="Przydziel noda" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value={UNASSIGNED}>Bez noda</SelectItem>
              {nodes.map((item) => {
                const id = getNodeId(item)
                return (
                  <SelectItem key={id} value={String(id)}>
                    {nodeLabel(item)}
                  </SelectItem>
                )
              })}
            </SelectContent>
          </Select>
          <Button
            type="button"
            variant="outline"
            size="icon-sm"
            title="Usuń przypisanie noda"
            disabled={!assignedNodeId || disabled}
            onClick={onRemove}
          >
            <Trash2 className="h-3.5 w-3.5" />
          </Button>
        </div>

        <div className="rounded-md border border-teal-100 bg-teal-50/60 p-2 dark:border-teal-800 dark:bg-teal-950/30">
          <div className="flex items-center justify-between gap-2">
            <span className="text-xs font-medium text-teal-900 dark:text-teal-50">
              {getProcessLabel(state)}
            </span>
            <span className="font-mono-feature text-[10px] text-teal-500 dark:text-teal-400">
              {node ? formatNodeId(getNodeId(node)) : 'no node'}
            </span>
          </div>
          <p className="mt-1 text-[10px] text-teal-500 dark:text-teal-400">
            Przypisanie wysyła: <span className="font-mono-feature">{assignmentPayload}</span>
          </p>
          <p className="mt-1 text-[10px] text-teal-500 dark:text-teal-400">
            READY {'->'} coded=true, ack_required=true
          </p>
          {state?.status && (
            <p className="mt-1 text-[10px] text-teal-500 dark:text-teal-400">
              Status: {getMessageStatusLabel(state.status)}
            </p>
          )}
          {state?.error && (
            <p className="mt-1 text-[10px] text-red-500">{state.error}</p>
          )}
        </div>

        <Button
          type="button"
          className="w-full"
          disabled={!canSend}
          loading={state?.phase === 'sending'}
          onClick={onReady}
        >
          <Send className="h-4 w-4" />
          READY
        </Button>
      </CardContent>
    </Card>
  )
}

export function RestaurantPanelPage() {
  const { data: nodes, isLoading: nodesLoading, error: nodesError, refetch: refetchNodes } = useNodes(true, 5000)
  const messages = useMessageHistory(true, 1000)
  const sendMessage = useSendMessage()
  const [assignments, setAssignments] = useState<Assignments>(() => loadAssignments())
  const [sendStates, setSendStates] = useState<SendStates>({})

  const availableNodes = useMemo(
    () => (nodes ?? [])
      .filter((node) => isNodeAddress(getNodeId(node)) && (node.is_paired ?? true))
      .sort((a, b) => getNodeId(a) - getNodeId(b)),
    [nodes],
  )

  const assignedCount = PLACES.filter((place) => assignments[place.id]).length
  const freeCount = PLACES.length - assignedCount
  const waitingCount = Object.values(sendStates).filter((state) => state?.phase === 'sending' || state?.phase === 'waiting_ack').length
  const deliveredCount = Object.values(sendStates).filter((state) => state?.phase === 'delivered').length

  useEffect(() => {
    if (!messages.data) return

    setSendStates((current) => {
      let changed = false
      const next: SendStates = { ...current }
      for (const [placeId, state] of Object.entries(current)) {
        if (!state || state.phase === 'failed' || state.phase === 'delivered' || state.phase === 'sent') continue
        const msg = findMatchingMessage(messages.data, state)
        if (!msg?.status) continue
        const phase = getStatusPhase(msg.status)
        if (phase !== state.phase || msg.status !== state.status || msg.id !== state.messageId) {
          next[placeId] = {
            ...state,
            phase,
            status: msg.status,
            messageId: msg.id ?? state.messageId,
          }
          changed = true
        }
      }
      return changed ? next : current
    })
  }, [messages.data])

  function assignNode(placeId: string, nodeId?: number) {
    const place = PLACES.find((item) => item.id === placeId)
    if (!place) return

    if (nodeId === undefined) {
      removeAssignment(placeId)
      return
    }

    setAssignments((current) => {
      const next = { ...current, [placeId]: nodeId }
      saveAssignments(next)
      return next
    })

    void sendPlaceAssignment(place, nodeId)
  }

  function removeAssignment(placeId: string) {
    setAssignments((current) => {
      const next = { ...current }
      delete next[placeId]
      saveAssignments(next)
      return next
    })
    setSendStates((current) => {
      const next = { ...current }
      delete next[placeId]
      return next
    })
  }

  async function sendPlaceAssignment(place: Place, dstId: number) {
    const payloadText = getAssignmentPayload(place)
    setSendStates((current) => ({
      ...current,
      [place.id]: {
        phase: 'sending',
        kind: 'assign',
        startedAt: Date.now(),
        payloadText,
      },
    }))

    try {
      const result = await sendMessage.mutateAsync({
        dst_id: dstId,
        payload_hex: textToHex(payloadText),
        coded: true,
        ack_required: true,
      })
      setSendStates((current) => ({
        ...current,
        [place.id]: {
          phase: getStatusPhase(result.status),
          kind: 'assign',
          status: result.status,
          messageId: result.id,
          startedAt: Date.now(),
          payloadText,
        },
      }))
      messages.refetch()
    } catch (err) {
      setSendStates((current) => ({
        ...current,
        [place.id]: {
          phase: 'failed',
          kind: 'assign',
          startedAt: Date.now(),
          payloadText,
          error: err instanceof Error ? err.message : 'Assignment failed',
        },
      }))
    }
  }

  async function sendReady(place: Place) {
    const dstId = assignments[place.id]
    if (!dstId) return

    setSendStates((current) => ({
      ...current,
      [place.id]: {
        phase: 'sending',
        kind: 'ready',
        startedAt: Date.now(),
        payloadText: READY_PAYLOAD,
      },
    }))

    try {
      const result = await sendMessage.mutateAsync({
        dst_id: dstId,
        payload_hex: READY_PAYLOAD_HEX,
        coded: true,
        ack_required: true,
      })
      setSendStates((current) => ({
        ...current,
        [place.id]: {
          phase: getStatusPhase(result.status),
          kind: 'ready',
          status: result.status,
          messageId: result.id,
          startedAt: Date.now(),
          payloadText: READY_PAYLOAD,
        },
      }))
      messages.refetch()
    } catch (err) {
      setSendStates((current) => ({
        ...current,
        [place.id]: {
          phase: 'failed',
          kind: 'ready',
          startedAt: Date.now(),
          payloadText: READY_PAYLOAD,
          error: err instanceof Error ? err.message : 'Send failed',
        },
      }))
    }
  }

  return (
    <div className="space-y-6">
      <SectionHeader
        title="Restaurant Panel"
        description="Symulacja sali restauracyjnej z przypisaniem nodów i wysyłką READY"
        actions={
          <Button variant="outline" size="sm" onClick={() => {
            refetchNodes()
            messages.refetch()
          }}>
            <RefreshCw className="h-3.5 w-3.5" />
            Refresh
          </Button>
        }
      />

      <div className="grid grid-cols-2 gap-4 lg:grid-cols-5">
        <Card>
          <CardContent className="flex items-center gap-3 p-4">
            <ChefHat className="h-5 w-5 text-ivory-700 dark:text-ivory-400" />
            <div>
              <p className="text-xs text-teal-500 dark:text-teal-400">Miejsca</p>
              <p className="text-lg font-semibold text-teal-900 dark:text-teal-50">{PLACES.length}</p>
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="flex items-center gap-3 p-4">
            <Armchair className="h-5 w-5 text-teal-600 dark:text-teal-400" />
            <div>
              <p className="text-xs text-teal-500 dark:text-teal-400">Wolne</p>
              <p className="text-lg font-semibold text-teal-900 dark:text-teal-50">{freeCount}</p>
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="flex items-center gap-3 p-4">
            <Radio className="h-5 w-5 text-ivory-700 dark:text-ivory-400" />
            <div>
              <p className="text-xs text-teal-500 dark:text-teal-400">Przypisane</p>
              <p className="text-lg font-semibold text-teal-900 dark:text-teal-50">{assignedCount}</p>
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="flex items-center gap-3 p-4">
            <Clock3 className="h-5 w-5 text-amber-600 dark:text-amber-400" />
            <div>
              <p className="text-xs text-teal-500 dark:text-teal-400">W trakcie</p>
              <p className="text-lg font-semibold text-teal-900 dark:text-teal-50">{waitingCount}</p>
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="flex items-center gap-3 p-4">
            <CheckCircle2 className="h-5 w-5 text-green-600 dark:text-green-400" />
            <div>
              <p className="text-xs text-teal-500 dark:text-teal-400">Dostarczone</p>
              <p className="text-lg font-semibold text-teal-900 dark:text-teal-50">{deliveredCount}</p>
            </div>
          </CardContent>
        </Card>
      </div>

      {nodesError && <ErrorDisplay error={nodesError} onRetry={refetchNodes} />}
      {messages.error && <ErrorDisplay error={messages.error} onRetry={messages.refetch} />}

      <div className="grid grid-cols-1 gap-4 md:grid-cols-2 xl:grid-cols-3 2xl:grid-cols-4">
        {PLACES.map((place) => (
          <PlaceCard
            key={place.id}
            place={place}
            assignedNodeId={assignments[place.id]}
            nodes={availableNodes}
            state={sendStates[place.id]}
            onAssign={(nodeId) => assignNode(place.id, nodeId)}
            onRemove={() => removeAssignment(place.id)}
            onReady={() => sendReady(place)}
            disabled={nodesLoading || sendMessage.isPending}
          />
        ))}
      </div>
    </div>
  )
}
