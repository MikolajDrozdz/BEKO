import { useState } from 'react'
import { Cpu, History, RefreshCw, Search, MessageSquare, Radio } from 'lucide-react'
import { useMessageHistory } from '@/hooks/useMessages'
import { SectionHeader } from '@/components/common/SectionHeader'
import { EmptyState } from '@/components/common/EmptyState'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { CopyButton } from '@/components/common/CopyButton'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { Input } from '@/components/ui/input'
import { Skeleton } from '@/components/ui/skeleton'
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from '@/components/ui/table'
import { formatTimestamp, formatNodeId } from '@/lib/utils/format'
import { hexToText } from '@/lib/utils/hex'
import { BROADCAST_ID, getMessageStatusLabel } from '@/lib/utils/protocol'
import type { MessageRecord } from '@/types/api'

function getStatusVariant(status?: string) {
  if (status === 'failed') return 'destructive'
  if (status === 'pending' || status?.includes('waiting_response')) return 'warning'
  if (status === 'delivered' || status === 'answered' || status === 'received' || status === 'response') return 'success'
  if (status === 'sent' || status === 'ok') return 'default'
  return 'outline'
}

function PayloadCell({ hex, status }: { hex: string; status?: string }) {
  const [showHex, setShowHex] = useState(false)
  const text = hexToText(hex)
  const response = text.trim().toUpperCase()
  const isNodeResponse = status === 'response' && ['YES', 'OK', 'NO'].includes(response)

  return (
    <div className="flex items-start gap-2 min-w-0">
      <div className="min-w-0 flex-1">
        {isNodeResponse && !showHex ? (
          <Badge variant={response === 'NO' ? 'warning' : 'success'} className="text-xs">
            Odpowiedź noda: {response}
          </Badge>
        ) : showHex ? (
          <span className="font-mono-feature text-xs text-teal-700 dark:text-teal-300 break-all">{hex}</span>
        ) : (
          <span className="text-xs text-teal-900 dark:text-teal-50">
            {text || <span className="text-teal-400 italic">binary data</span>}
          </span>
        )}
      </div>
      <div className="flex items-center gap-1 shrink-0">
        <button
          onClick={() => setShowHex(!showHex)}
          className="text-[10px] text-teal-400 hover:text-teal-600 dark:hover:text-teal-300 underline"
        >
          {showHex ? 'text' : 'hex'}
        </button>
        <CopyButton value={hex} />
      </div>
    </div>
  )
}

function AddressBadge({ msg }: { msg: MessageRecord }) {
  if ((msg.status === 'response' || msg.status === 'received') && msg.src_id !== undefined) {
    return (
      <div className="flex items-center gap-1.5">
        <Cpu className="h-3 w-3 text-teal-600 dark:text-teal-400" />
        <span className="text-xs text-teal-500 dark:text-teal-400">Od noda</span>
        <span className="font-mono-feature text-xs font-medium text-teal-900 dark:text-teal-50">
          {formatNodeId(msg.src_id)}
        </span>
      </div>
    )
  }

  if (msg.dst_id === BROADCAST_ID) {
    return (
      <div className="flex items-center gap-1.5">
        <Radio className="h-3 w-3 text-ivory-600 dark:text-ivory-400" />
        <span className="text-xs font-medium text-ivory-700 dark:text-ivory-400">
          Broadcast
        </span>
      </div>
    )
  }
  return (
    <span className="font-mono-feature text-xs text-teal-900 dark:text-teal-50">
      {formatNodeId(msg.dst_id)}
    </span>
  )
}

export function MessageHistoryPage() {
  const { data: messages, isLoading, error, refetch } = useMessageHistory()
  const [search, setSearch] = useState('')

  const filtered = (messages ?? [])
    .slice()
    .reverse()
    .filter((m: MessageRecord) => {
      if (!search) return true
      const q = search.toLowerCase()
      return (
        String(m.dst_id).includes(q) ||
        formatNodeId(m.dst_id).toLowerCase().includes(q) ||
        (m.src_id !== undefined && String(m.src_id).includes(q)) ||
        (m.src_id !== undefined && formatNodeId(m.src_id).toLowerCase().includes(q)) ||
        m.payload_hex.toLowerCase().includes(q) ||
        hexToText(m.payload_hex).toLowerCase().includes(q) ||
        getMessageStatusLabel(m.status).toLowerCase().includes(q)
      )
    })

  return (
    <div className="space-y-6">
      <SectionHeader
        title="Message History"
        description="All sent messages recorded by the gateway"
        actions={
          <Button variant="outline" size="sm" onClick={() => refetch()}>
            <RefreshCw className="h-3.5 w-3.5" />
            Refresh
          </Button>
        }
      />

      <Card>
        <CardHeader className="pb-3">
          <div className="flex items-center justify-between gap-3">
            <CardTitle className="flex items-center gap-2">
              <History className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
              Messages
              {messages && (
                <Badge variant="outline" className="ml-1">{messages.length}</Badge>
              )}
            </CardTitle>
            <div className="relative w-56">
              <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 h-3.5 w-3.5 text-teal-400" />
              <Input
                placeholder="Search messages..."
                value={search}
                onChange={(e) => setSearch(e.target.value)}
                className="pl-8 h-8 text-xs"
              />
            </div>
          </div>
        </CardHeader>
        <CardContent className="p-0">
          {isLoading && (
            <div className="p-5 space-y-2">
              {[...Array(4)].map((_, i) => <Skeleton key={i} className="h-12 w-full" />)}
            </div>
          )}

          {error && (
            <div className="p-5">
              <ErrorDisplay error={error} onRetry={refetch} />
            </div>
          )}

          {!isLoading && !error && (
            <>
              {filtered.length > 0 ? (
                <Table>
                  <TableHeader>
                    <TableRow>
                      <TableHead>Timestamp</TableHead>
                      <TableHead>Adres</TableHead>
                      <TableHead>Payload</TableHead>
                      <TableHead>Coded</TableHead>
                      <TableHead>RSSI</TableHead>
                      <TableHead>Status</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {filtered.map((msg: MessageRecord, idx: number) => (
                      <TableRow key={msg.id ?? idx}>
                        <TableCell>
                          <span className="text-xs text-teal-600 dark:text-teal-400 whitespace-nowrap">
                            {formatTimestamp(msg.timestamp)}
                          </span>
                        </TableCell>
                        <TableCell>
                          <AddressBadge msg={msg} />
                        </TableCell>
                        <TableCell className="max-w-[240px]">
                          <PayloadCell hex={msg.payload_hex} status={msg.status} />
                        </TableCell>
                        <TableCell>
                          {msg.coded !== undefined ? (
                            <Badge variant={msg.coded ? 'accent' : 'default'} className="text-xs">
                              {msg.coded ? 'Yes' : 'No'}
                            </Badge>
                          ) : (
                            <span className="text-xs text-teal-400">—</span>
                          )}
                        </TableCell>
                        <TableCell>
                          {msg.rssi !== undefined ? (
                            <span className="font-mono-feature text-xs">{msg.rssi} dBm</span>
                          ) : (
                            <span className="text-xs text-teal-400">—</span>
                          )}
                        </TableCell>
                        <TableCell>
                          {msg.status ? (
                            <Badge
                              variant={getStatusVariant(msg.status)}
                              className="max-w-[260px] whitespace-normal"
                            >
                              {getMessageStatusLabel(msg.status)}
                            </Badge>
                          ) : (
                            <span className="text-xs text-teal-400">—</span>
                          )}
                        </TableCell>
                      </TableRow>
                    ))}
                  </TableBody>
                </Table>
              ) : (
                <EmptyState
                  icon={MessageSquare}
                  title={search ? 'No messages match' : 'No message history'}
                  description={search ? 'Try a different search term.' : 'Messages you send will appear here.'}
                  className="py-16"
                />
              )}
            </>
          )}
        </CardContent>
      </Card>
    </div>
  )
}
