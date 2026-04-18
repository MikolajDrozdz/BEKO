import { Link } from 'react-router-dom'
import {
  Cpu,
  MessageSquare,
  Antenna,
  ScrollText,
  Radio,
  Clock,
  RefreshCw,
} from 'lucide-react'
import { useSystemInfo } from '@/hooks/useSystem'
import { useNodes } from '@/hooks/useNodes'
import { useMessageHistory } from '@/hooks/useMessages'
import { useLogs } from '@/hooks/useLogs'
import { usePairingStatus } from '@/hooks/usePairing'
import { StatCard } from '@/components/common/StatCard'
import { SectionHeader } from '@/components/common/SectionHeader'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { StatusBadge } from '@/components/common/StatusBadge'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { Skeleton } from '@/components/ui/skeleton'
import { formatTimestamp, formatUptime, formatFrequency } from '@/lib/utils/format'
import type { LogEntry } from '@/types/api'

function LogLevelBadge({ level }: { level?: string }) {
  const l = (level ?? '').toUpperCase()
  if (l === 'ERROR' || l === 'CRITICAL') return <Badge variant="destructive">{l}</Badge>
  if (l === 'WARNING') return <Badge variant="warning">{l}</Badge>
  if (l === 'INFO') return <Badge variant="success">{l}</Badge>
  return <Badge variant="default">{l || 'LOG'}</Badge>
}

export function DashboardPage() {
  const system = useSystemInfo()
  const nodes = useNodes()
  const messages = useMessageHistory()
  const logs = useLogs()
  const pairingStatus = usePairingStatus(true)

  const recentLogs = (logs.data ?? []).slice(-5).reverse()

  return (
    <div className="space-y-6">
      <SectionHeader
        title="Dashboard"
        description="Gateway overview and live status"
        actions={
          <Button
            variant="outline"
            size="sm"
            onClick={() => {
              system.refetch()
              nodes.refetch()
              messages.refetch()
              logs.refetch()
            }}
          >
            <RefreshCw className="h-3.5 w-3.5" />
            Refresh
          </Button>
        }
      />

      {/* Stats */}
      <div className="grid grid-cols-2 gap-4 lg:grid-cols-4">
        <StatCard
          icon={Cpu}
          label="Paired Nodes"
          value={nodes.data?.length ?? '—'}
          loading={nodes.isLoading}
          accent
        />
        <StatCard
          icon={MessageSquare}
          label="Messages"
          value={messages.data?.length ?? '—'}
          loading={messages.isLoading}
        />
        <StatCard
          icon={Antenna}
          label="Paired (session)"
          value={pairingStatus.data?.paired_nodes_count ?? '—'}
          sub={
            (pairingStatus.data?.paired_nodes_count ?? 0) > 0
              ? 'via PAIR_RESP'
              : undefined
          }
          loading={pairingStatus.isLoading}
        />
        <StatCard
          icon={Clock}
          label="Uptime"
          value={formatUptime(system.data?.uptime)}
          loading={system.isLoading}
        />
      </div>

      <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
        {/* Gateway Info */}
        <Card>
          <CardHeader className="pb-3">
            <div className="flex items-center justify-between">
              <CardTitle className="flex items-center gap-2">
                <Radio className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
                Gateway Info
              </CardTitle>
              {system.data && (
                <StatusBadge active={true} activeLabel="Online" />
              )}
            </div>
          </CardHeader>
          <CardContent className="space-y-2">
            {system.isLoading && (
              <div className="space-y-2">
                {[...Array(5)].map((_, i) => <Skeleton key={i} className="h-4 w-full" />)}
              </div>
            )}
            {system.error && <ErrorDisplay error={system.error} compact onRetry={system.refetch} />}
            {system.data && (
              <dl className="space-y-2">
                {[
                  ['Gateway ID', system.data.gateway_id_hex ?? system.data.gateway_id ?? system.data.id ?? '—'],
                  ['Hostname', system.data.hostname ?? '—'],
                  ['Platform', system.data.platform ?? '—'],
                  ['Version', system.data.version ?? '—'],
                  ['Frequency', formatFrequency(system.data.radio?.frequency)],
                  ['Spreading Factor', system.data.radio?.spreading_factor ?? '—'],
                  ['TX Power', system.data.radio?.tx_power !== undefined ? `${system.data.radio.tx_power} dBm` : '—'],
                ].map(([k, v]) => (
                  <div key={String(k)} className="flex items-center justify-between gap-4">
                    <dt className="text-xs text-teal-500 dark:text-teal-400 shrink-0">{k}</dt>
                    <dd className="text-xs font-medium text-teal-900 dark:text-teal-100 font-mono-feature truncate text-right">{String(v)}</dd>
                  </div>
                ))}
              </dl>
            )}
            {!system.isLoading && !system.error && !system.data && (
              <p className="text-xs text-teal-500 text-center py-4">No system data</p>
            )}
          </CardContent>
        </Card>

        {/* Recent Logs */}
        <Card>
          <CardHeader className="pb-3">
            <div className="flex items-center justify-between">
              <CardTitle className="flex items-center gap-2">
                <ScrollText className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
                Recent Logs
              </CardTitle>
              <Link to="/logs">
                <Button variant="ghost" size="sm" className="text-xs h-7">
                  View all
                </Button>
              </Link>
            </div>
          </CardHeader>
          <CardContent>
            {logs.isLoading && (
              <div className="space-y-2">
                {[...Array(4)].map((_, i) => <Skeleton key={i} className="h-8 w-full" />)}
              </div>
            )}
            {logs.error && <ErrorDisplay error={logs.error} compact onRetry={logs.refetch} />}
            {recentLogs.length > 0 && (
              <div className="space-y-1.5">
                {recentLogs.map((log: LogEntry, i: number) => (
                  <div key={i} className="flex items-start gap-2 rounded-md p-1.5 hover:bg-teal-50 dark:hover:bg-teal-800/30 transition-colors">
                    <LogLevelBadge level={log.level} />
                    <div className="min-w-0 flex-1">
                      <p className="text-xs text-teal-900 dark:text-teal-100 truncate">{log.message}</p>
                      {log.timestamp && (
                        <p className="text-[10px] text-teal-400 dark:text-teal-600">{formatTimestamp(log.timestamp)}</p>
                      )}
                    </div>
                  </div>
                ))}
              </div>
            )}
            {!logs.isLoading && !logs.error && recentLogs.length === 0 && (
              <p className="text-xs text-teal-500 text-center py-4">No logs available</p>
            )}
          </CardContent>
        </Card>
      </div>

      {/* Pairing shortcut */}
      {(pairingStatus.data?.paired_nodes_count ?? 0) > 0 && (
        <div className="rounded-lg border border-ivory-200 dark:border-ivory-800/50 bg-ivory-50 dark:bg-ivory-950/20 p-4 flex items-center justify-between gap-4 animate-slide-in">
          <div className="flex items-center gap-2.5">
            <span className="h-2.5 w-2.5 rounded-full bg-ivory-500 animate-pulse" />
            <div>
              <p className="text-sm font-medium text-ivory-800 dark:text-ivory-300">Nodes paired this session</p>
              <p className="text-xs text-ivory-600 dark:text-ivory-500">
                {pairingStatus.data?.paired_nodes_count} node(s) responded with PAIR_RESP
              </p>
            </div>
          </div>
          <Link to="/pairing">
            <Button size="sm">
              <Antenna className="h-3.5 w-3.5" />
              Go to Pairing
            </Button>
          </Link>
        </div>
      )}
    </div>
  )
}
