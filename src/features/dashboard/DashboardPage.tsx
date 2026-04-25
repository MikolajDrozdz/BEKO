import { useEffect, useMemo, useState, type ReactNode } from 'react'
import { Link } from 'react-router-dom'
import {
  Activity,
  BarChart3,
  Cpu,
  Gauge,
  Hourglass,
  MessageSquare,
  Antenna,
  ScrollText,
  Radio,
  Clock,
  RefreshCw,
  Signal,
} from 'lucide-react'
import { useGatewayInfo, useRadioStatus, useSystemInfo, useSystemMetricsHistory } from '@/hooks/useSystem'
import { useNodes } from '@/hooks/useNodes'
import { useMessageHistory, useMessagePending, useMessageStats } from '@/hooks/useMessages'
import { useLogs, useLogsSummary } from '@/hooks/useLogs'
import { usePairingStatus } from '@/hooks/usePairing'
import { StatCard } from '@/components/common/StatCard'
import { SectionHeader } from '@/components/common/SectionHeader'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { StatusBadge } from '@/components/common/StatusBadge'
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { Skeleton } from '@/components/ui/skeleton'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import { formatTimestamp, formatUptime, formatFrequency, formatNodeId } from '@/lib/utils/format'
import { getMessageStatusLabel } from '@/lib/utils/protocol'
import type { LogEntry, MessageRecord, MessageStatsBucket, Node, RadioStatus, SystemInfo, SystemMetric } from '@/types/api'

type MetricSample = {
  ts: number
  cpuPercent?: number
  memoryPercent?: number
  cpuTemp?: number
  diskPercent?: number
}

type BarDatum = {
  label: string
  value: number
  color?: string
  sub?: string
}

const TIME_RANGES = [
  { value: '15m', label: '15 min', step: '5s', bucketCount: 15 },
  { value: '1h', label: '1 h', step: '10s', bucketCount: 12 },
  { value: '6h', label: '6 h', step: '1m', bucketCount: 12 },
  { value: '24h', label: '24 h', step: '5m', bucketCount: 24 },
] as const

type TimeRangeValue = typeof TIME_RANGES[number]['value']

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value))
}

function formatShortTime(ts: number): string {
  return new Date(ts).toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' })
}

function getTimeRangeOption(value: TimeRangeValue) {
  return TIME_RANGES.find((option) => option.value === value) ?? TIME_RANGES[1]
}

function durationToMs(value: string): number {
  const match = value.match(/^(\d+)([mhd])$/)
  if (!match) return 60 * 60_000
  const amount = Number(match[1])
  if (match[2] === 'm') return amount * 60_000
  if (match[2] === 'h') return amount * 60 * 60_000
  return amount * 24 * 60 * 60_000
}

function formatAxisTime(ts: number, rangeMs: number): string {
  const options: Intl.DateTimeFormatOptions = rangeMs >= 24 * 60 * 60_000
    ? { month: 'short', day: '2-digit', hour: '2-digit', minute: '2-digit' }
    : { hour: '2-digit', minute: '2-digit' }
  return new Date(ts).toLocaleString(undefined, options)
}

function formatFrequencyValue(value?: number): string {
  if (value === undefined) return '—'
  if (value > 1_000_000) return `${(value / 1_000_000).toFixed(3)} MHz`
  return formatFrequency(value)
}

function getMessageTimestamp(msg: MessageRecord): number | undefined {
  const raw = msg.timestamp ?? msg.sent_at
  if (!raw) return undefined
  const ts = new Date(raw).getTime()
  return Number.isFinite(ts) ? ts : undefined
}

function isReceivedMessage(msg: MessageRecord): boolean {
  if (msg.direction === 'received') return true
  if (msg.status === 'received' || msg.status === 'response') return true
  return msg.src_id !== undefined && msg.dst_id === undefined
}

function makeMetricSample(data?: SystemInfo): MetricSample | undefined {
  if (!data) return undefined
  const memoryPercent = data.memory_used !== undefined && data.memory_total
    ? Math.round((data.memory_used / data.memory_total) * 100)
    : undefined
  const cpuTemp = data.cpu_temp !== undefined ? Number(data.cpu_temp.toFixed(1)) : undefined
  if (memoryPercent === undefined && cpuTemp === undefined) return undefined
  return { ts: Date.now(), memoryPercent, cpuTemp }
}

function metricToSample(metric: SystemMetric): MetricSample | undefined {
  const rawTs = metric.timestamp ?? metric.time
  const ts = rawTs ? new Date(rawTs).getTime() : Date.now()
  const memoryPercent = metric.memory_percent ??
    (metric.memory_used !== undefined && metric.memory_total
      ? Math.round((metric.memory_used / metric.memory_total) * 100)
      : undefined)
  const diskPercent = metric.disk_percent ??
    (metric.disk_used !== undefined && metric.disk_total
      ? Math.round((metric.disk_used / metric.disk_total) * 100)
      : undefined)
  const sample = {
    ts: Number.isFinite(ts) ? ts : Date.now(),
    cpuPercent: metric.cpu_percent,
    memoryPercent,
    cpuTemp: metric.cpu_temp,
    diskPercent,
  }
  if (
    sample.cpuPercent === undefined &&
    sample.memoryPercent === undefined &&
    sample.cpuTemp === undefined &&
    sample.diskPercent === undefined
  ) return undefined
  return sample
}

function buildMessageBuckets(messages: MessageRecord[], rangeMs: number, bucketCount = 12) {
  const end = Date.now()
  const start = end - rangeMs
  const bucketMs = Math.ceil(rangeMs / bucketCount)
  const buckets = Array.from({ length: bucketCount }, (_, i) => {
    const bucketStart = start + i * bucketMs
    return {
      label: formatShortTime(bucketStart),
      total: 0,
      sent: 0,
      received: 0,
    }
  })

  for (const msg of messages) {
    const ts = getMessageTimestamp(msg) ?? end - 1
    if (ts < start || ts > end) continue
    const index = clamp(Math.floor((ts - start) / bucketMs), 0, bucketCount - 1)
    buckets[index].total += 1
    if (isReceivedMessage(msg)) buckets[index].received += 1
    else buckets[index].sent += 1
  }

  return buckets
}

function buildStatsBuckets(buckets?: MessageStatsBucket[], bucketCount = 12) {
  if (!buckets || buckets.length === 0) return undefined
  return buckets.slice(-bucketCount).map((bucket) => {
    const rawTs = bucket.time ?? bucket.timestamp
    const ts = rawTs ? new Date(rawTs).getTime() : undefined
    const sent = bucket.sent ?? 0
    const received = (bucket.received ?? 0) + (bucket.response ?? 0)
    const failed = bucket.failed ?? 0
    return {
      label: ts && Number.isFinite(ts) ? formatShortTime(ts) : (rawTs ?? ''),
      total: bucket.total ?? sent + received + failed,
      sent,
      received,
    }
  })
}

function countBy<T>(items: T[], getKey: (item: T) => string | undefined): BarDatum[] {
  const counts = new Map<string, number>()
  for (const item of items) {
    const key = getKey(item) || 'unknown'
    counts.set(key, (counts.get(key) ?? 0) + 1)
  }
  return [...counts.entries()]
    .sort((a, b) => b[1] - a[1])
    .slice(0, 6)
    .map(([label, value]) => ({ label, value }))
}

function ChartPanel({
  title,
  description,
  icon: Icon,
  actions,
  children,
}: {
  title: string
  description?: string
  icon: typeof Activity
  actions?: ReactNode
  children: ReactNode
}) {
  return (
    <Card className="overflow-hidden">
      <CardHeader className="pb-2">
        <div className="flex items-start justify-between gap-3">
          <div className="min-w-0">
            <CardTitle className="flex items-center gap-2">
              <Icon className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
              {title}
            </CardTitle>
            {description && <CardDescription>{description}</CardDescription>}
          </div>
          {actions && <div className="shrink-0">{actions}</div>}
        </div>
      </CardHeader>
      <CardContent>{children}</CardContent>
    </Card>
  )
}

function EmptyChart({ label = 'No data' }: { label?: string }) {
  return (
    <div className="flex h-40 items-center justify-center rounded-md border border-dashed border-teal-200 bg-teal-50/60 dark:border-teal-800 dark:bg-teal-950/30">
      <span className="text-xs text-teal-500 dark:text-teal-400">{label}</span>
    </div>
  )
}

function TimeRangeSelect({
  value,
  onChange,
}: {
  value: TimeRangeValue
  onChange: (value: TimeRangeValue) => void
}) {
  return (
    <Select value={value} onValueChange={(next) => onChange(next as TimeRangeValue)}>
      <SelectTrigger className="h-8 w-24 text-xs">
        <SelectValue />
      </SelectTrigger>
      <SelectContent>
        {TIME_RANGES.map((option) => (
          <SelectItem key={option.value} value={option.value}>
            {option.label}
          </SelectItem>
        ))}
      </SelectContent>
    </Select>
  )
}

function LineChart({ samples, rangeMs }: { samples: MetricSample[]; rangeMs: number }) {
  const sortedSamples = [...samples].sort((a, b) => a.ts - b.ts)
  const series = [
    {
      key: 'cpuPercent' as const,
      label: 'CPU',
      suffix: '%',
      color: '#2563eb',
      values: sortedSamples.map((s) => ({ ts: s.ts, value: s.cpuPercent })),
    },
    {
      key: 'memoryPercent' as const,
      label: 'Memory',
      suffix: '%',
      color: '#0f766e',
      values: sortedSamples.map((s) => ({ ts: s.ts, value: s.memoryPercent })),
    },
    {
      key: 'cpuTemp' as const,
      label: 'Temp',
      suffix: '°C',
      color: '#d97706',
      values: sortedSamples.map((s) => ({ ts: s.ts, value: s.cpuTemp })),
    },
    {
      key: 'diskPercent' as const,
      label: 'Disk',
      suffix: '%',
      color: '#7c3aed',
      values: sortedSamples.map((s) => ({ ts: s.ts, value: s.diskPercent })),
    },
  ]

  if (samples.length < 2 || series.every((s) => s.values.every((v) => v.value === undefined))) {
    return <EmptyChart label="Waiting for samples" />
  }

  const width = 320
  const height = 120
  const pad = 10
  const axisEnd = sortedSamples[sortedSamples.length - 1]?.ts ?? Date.now()
  const axisStart = axisEnd - rangeMs
  const axisSpan = Math.max(1, axisEnd - axisStart)
  const timeTicks = [axisStart, axisStart + axisSpan / 2, axisEnd]
  const values = series.flatMap((s) => s.values.map((v) => v.value).filter((v): v is number => v !== undefined))
  const max = Math.max(100, ...values)
  const min = 0

  function point(ts: number, value: number) {
    const progress = clamp((ts - axisStart) / axisSpan, 0, 1)
    const x = pad + progress * (width - pad * 2)
    const y = height - pad - ((value - min) / Math.max(1, max - min)) * (height - pad * 2)
    return `${x.toFixed(1)},${y.toFixed(1)}`
  }

  return (
    <div className="space-y-3">
      <div className="flex flex-wrap items-center gap-3">
        {series.map((s) => {
          const last = [...s.values].reverse().find((v) => v.value !== undefined)?.value
          return (
            <div key={s.key} className="flex items-center gap-1.5 text-xs text-teal-600 dark:text-teal-400">
              <span className="h-2 w-2 rounded-full" style={{ backgroundColor: s.color }} />
              <span>{s.label}</span>
              <span className="font-mono-feature text-teal-900 dark:text-teal-50">
                {last !== undefined ? `${last}${s.suffix}` : '—'}
              </span>
            </div>
          )
        })}
      </div>
      <svg viewBox={`0 0 ${width} ${height}`} className="h-40 w-full overflow-visible">
        {[0, 25, 50, 75, 100].map((tick) => {
          const y = height - pad - (tick / 100) * (height - pad * 2)
          return (
            <g key={tick}>
              <line x1={pad} x2={width - pad} y1={y} y2={y} className="stroke-teal-100 dark:stroke-teal-800" strokeWidth="1" />
              <text x={0} y={y + 3} className="fill-teal-400 text-[8px]">{tick}</text>
            </g>
          )
        })}
        {series.map((s) => {
          const points = s.values
            .map((v) => v.value !== undefined ? point(v.ts, v.value) : undefined)
            .filter((v): v is string => Boolean(v))
          return (
            <polyline
              key={s.key}
              fill="none"
              stroke={s.color}
              strokeLinecap="round"
              strokeLinejoin="round"
              strokeWidth="2.4"
              points={points.join(' ')}
            />
          )
        })}
      </svg>
      <div className="grid grid-cols-3 gap-2 border-t border-teal-100 pt-1 text-[10px] text-teal-400 dark:border-teal-800 dark:text-teal-600">
        {timeTicks.map((tick, index) => (
          <span key={index} className={index === 1 ? 'text-center' : index === 2 ? 'text-right' : ''}>
            {formatAxisTime(tick, rangeMs)}
          </span>
        ))}
      </div>
      <div className="flex flex-wrap items-center justify-between gap-2 text-[10px] text-teal-500 dark:text-teal-400">
        <span>{samples.length} samples</span>
        <span className="font-mono-feature">
          {formatAxisTime(axisStart, rangeMs)} - {formatAxisTime(axisEnd, rangeMs)}
        </span>
      </div>
    </div>
  )
}

function StackedMessageBars({ buckets }: { buckets: ReturnType<typeof buildMessageBuckets> }) {
  const max = Math.max(1, ...buckets.map((b) => b.total))
  const totals = buckets.reduce(
    (acc, bucket) => ({
      total: acc.total + bucket.total,
      sent: acc.sent + bucket.sent,
      received: acc.received + bucket.received,
    }),
    { total: 0, sent: 0, received: 0 },
  )

  return (
    <div className="space-y-3">
      <div className="flex flex-wrap items-center gap-3 text-xs text-teal-600 dark:text-teal-400">
        <span className="flex items-center gap-1.5"><span className="h-2 w-2 rounded-sm bg-ivory-600" />Sent <strong className="font-mono-feature text-teal-900 dark:text-teal-50">{totals.sent}</strong></span>
        <span className="flex items-center gap-1.5"><span className="h-2 w-2 rounded-sm bg-teal-500" />Received <strong className="font-mono-feature text-teal-900 dark:text-teal-50">{totals.received}</strong></span>
        <span className="font-mono-feature text-teal-900 dark:text-teal-50">Total {totals.total}</span>
      </div>
      <div className="flex h-40 items-end gap-1.5 border-b border-teal-100 pb-2 dark:border-teal-800">
        {buckets.map((bucket) => {
          const sentHeight = (bucket.sent / max) * 100
          const receivedHeight = (bucket.received / max) * 100
          return (
            <div key={bucket.label} className="flex h-full flex-1 flex-col justify-end gap-0.5">
              <div
                title={`${bucket.label}: ${bucket.total}`}
                className="w-full rounded-t-sm bg-teal-500/80"
                style={{ height: `${receivedHeight}%`, minHeight: bucket.received ? 3 : 0 }}
              />
              <div
                title={`${bucket.label}: ${bucket.total}`}
                className="w-full bg-ivory-600/90"
                style={{ height: `${sentHeight}%`, minHeight: bucket.sent ? 3 : 0 }}
              />
            </div>
          )
        })}
      </div>
      <div className="grid grid-cols-3 gap-1 text-[10px] text-teal-400 dark:text-teal-600">
        <span>{buckets[0]?.label}</span>
        <span className="text-center">{buckets[Math.floor(buckets.length / 2)]?.label}</span>
        <span className="text-right">{buckets[buckets.length - 1]?.label}</span>
      </div>
    </div>
  )
}

function HorizontalBars({ data, emptyLabel = 'No data' }: { data: BarDatum[]; emptyLabel?: string }) {
  if (data.length === 0) return <EmptyChart label={emptyLabel} />
  const max = Math.max(1, ...data.map((d) => d.value))

  return (
    <div className="space-y-2">
      {data.map((item) => (
        <div key={item.label} className="space-y-1">
          <div className="flex items-center justify-between gap-3">
            <span className="truncate text-xs font-medium text-teal-900 dark:text-teal-50">{item.label}</span>
            <span className="font-mono-feature text-xs text-teal-500 dark:text-teal-400">{item.value}</span>
          </div>
          <div className="h-2 rounded-full bg-teal-100 dark:bg-teal-800">
            <div
              className="h-full rounded-full bg-ivory-600 dark:bg-ivory-500"
              style={{ width: `${(item.value / max) * 100}%`, backgroundColor: item.color }}
            />
          </div>
          {item.sub && <p className="text-[10px] text-teal-400 dark:text-teal-600">{item.sub}</p>}
        </div>
      ))}
    </div>
  )
}

function RssiBars({ nodes }: { nodes: Node[] }) {
  const rows = nodes
    .filter((node) => node.rssi !== undefined)
    .sort((a, b) => (b.rssi ?? -999) - (a.rssi ?? -999))
    .slice(0, 8)
    .map((node) => {
      const id = node.node_id ?? node.id ?? 0
      const rssi = node.rssi ?? -120
      const percent = clamp(((rssi + 120) / 90) * 100, 4, 100)
      const color = rssi > -70 ? '#16a34a' : rssi > -95 ? '#d97706' : '#dc2626'
      return {
        label: node.name ? `${node.name} (${formatNodeId(id)})` : formatNodeId(id),
        value: Math.round(percent),
        color,
        sub: `${rssi} dBm${node.snr !== undefined ? `, SNR ${node.snr}` : ''}${node.battery_percent !== undefined ? `, battery ${node.battery_percent}%` : ''}`,
      }
    })

  return <HorizontalBars data={rows} emptyLabel="No RSSI samples" />
}

function RadioBars({ radio }: { radio?: RadioStatus }) {
  const counters = radio?.counters
  const rows = [
    { label: 'RX frames', value: counters?.rx ?? radio?.rx_count ?? 0, color: '#0f766e' },
    { label: 'TX frames', value: counters?.tx ?? radio?.tx_count ?? 0, color: '#d97706' },
    { label: 'TX failures', value: counters?.tx_failed ?? radio?.tx_fail_count ?? 0, color: '#dc2626' },
    { label: 'CRC errors', value: counters?.crc_errors ?? radio?.crc_error_count ?? 0, color: '#7c3aed' },
  ].filter((row) => row.value > 0)

  return (
    <div className="space-y-3">
      <div className="flex flex-wrap items-center gap-2">
        <StatusBadge active={!!radio?.ready} activeLabel="Radio ready" inactiveLabel="Radio offline" />
        {radio?.driver && <Badge variant="outline">{radio.driver}</Badge>}
        {radio?.runtime_label && <Badge variant="outline">{radio.runtime_label}</Badge>}
        {radio?.last_error && <Badge variant="destructive">{radio.last_error}</Badge>}
      </div>
      <HorizontalBars data={rows} emptyLabel="No radio counters" />
      <div className="grid grid-cols-2 gap-2 text-xs text-teal-600 dark:text-teal-400">
        <span>Last RSSI: <strong className="font-mono-feature text-teal-900 dark:text-teal-50">{radio?.last_rssi ?? '—'} dBm</strong></span>
        <span>Last SNR: <strong className="font-mono-feature text-teal-900 dark:text-teal-50">{radio?.last_snr ?? '—'}</strong></span>
      </div>
    </div>
  )
}

function LogLevelBadge({ level }: { level?: string }) {
  const l = (level ?? '').toUpperCase()
  if (l === 'ERROR' || l === 'CRITICAL') return <Badge variant="destructive">{l}</Badge>
  if (l === 'WARNING') return <Badge variant="warning">{l}</Badge>
  if (l === 'INFO') return <Badge variant="success">{l}</Badge>
  return <Badge variant="default">{l || 'LOG'}</Badge>
}

export function DashboardPage() {
  const [systemRange, setSystemRange] = useState<TimeRangeValue>('1h')
  const [messageRange, setMessageRange] = useState<TimeRangeValue>('24h')
  const systemRangeOption = getTimeRangeOption(systemRange)
  const messageRangeOption = getTimeRangeOption(messageRange)
  const systemRangeMs = durationToMs(systemRangeOption.value)
  const messageRangeMs = durationToMs(messageRangeOption.value)

  const system = useSystemInfo(true, 5000)
  const gatewayInfo = useGatewayInfo(true, 5000)
  const metricsHistory = useSystemMetricsHistory(true, 10000, {
    range: systemRangeOption.value,
    step: systemRangeOption.step,
  })
  const radioStatus = useRadioStatus(true, 5000)
  const nodes = useNodes(true, 5000)
  const messages = useMessageHistory(true, 5000)
  const messageStats = useMessageStats(true, 5000, { range: messageRangeOption.value })
  const messagePending = useMessagePending(true, 3000)
  const logs = useLogs(true)
  const logsSummary = useLogsSummary(true, 5000)
  const pairingStatus = usePairingStatus(true)
  const [metricSamples, setMetricSamples] = useState<MetricSample[]>([])

  const recentLogs = (logs.data ?? []).slice(-5).reverse()
  const gateway = gatewayInfo.data ?? system.data?.gateway
  const gatewayStatus = gateway?.online ?? system.data?.online ?? system.data?.radio_ready ?? Boolean(system.data)
  const gatewayRows = [
    ['Gateway ID', gateway?.gateway_id_hex ?? system.data?.gateway_id_hex ?? gateway?.gateway_id ?? system.data?.gateway_id ?? system.data?.id ?? '—'],
    ['Hostname', gateway?.hostname ?? system.data?.hostname ?? '—'],
    ['Platform', gateway?.platform ?? system.data?.platform ?? '—'],
    ['Version', gateway?.version ?? system.data?.version ?? system.data?.firmware ?? '—'],
    ['Frequency', formatFrequencyValue(gateway?.frequency_hz ?? system.data?.frequency_hz ?? system.data?.radio?.frequency_hz ?? system.data?.radio?.frequency)],
    ['Spreading Factor', gateway?.spreading_factor ?? system.data?.spreading_factor ?? system.data?.radio?.spreading_factor ?? '—'],
    ['TX Power', (gateway?.tx_power ?? system.data?.tx_power ?? system.data?.radio?.tx_power) !== undefined ? `${gateway?.tx_power ?? system.data?.tx_power ?? system.data?.radio?.tx_power} dBm` : '—'],
  ]
  const apiMetricSamples = useMemo(
    () => (metricsHistory.data ?? [])
      .map(metricToSample)
      .filter((sample): sample is MetricSample => Boolean(sample)),
    [metricsHistory.data],
  )
  const chartMetricSamples = apiMetricSamples.length > 0 ? apiMetricSamples : metricSamples
  const messageBuckets = useMemo(
    () => buildStatsBuckets(messageStats.data?.by_bucket, messageRangeOption.bucketCount) ??
      buildMessageBuckets(messages.data ?? [], messageRangeMs, messageRangeOption.bucketCount),
    [messageRangeMs, messageRangeOption.bucketCount, messageStats.data?.by_bucket, messages.data],
  )
  const messageStatusBars = useMemo(
    () => {
      const byStatus = messageStats.data?.by_status
      if (byStatus) {
        return Object.entries(byStatus)
          .sort((a, b) => b[1] - a[1])
          .slice(0, 6)
          .map(([label, value]) => ({ label: getMessageStatusLabel(label), value }))
      }
      return countBy(messages.data ?? [], (message) => getMessageStatusLabel(message.status))
    },
    [messageStats.data?.by_status, messages.data],
  )
  const logLevelBars = useMemo(() => {
    const colors: Record<string, string> = {
      CRITICAL: '#dc2626',
      ERROR: '#dc2626',
      WARNING: '#d97706',
      INFO: '#16a34a',
      DEBUG: '#64748b',
      LOG: '#0f766e',
    }
    const summaryCounts = logsSummary.data?.by_level ?? logsSummary.data?.counts
    if (summaryCounts && Object.keys(summaryCounts).length > 0) {
      return Object.entries(summaryCounts)
        .sort((a, b) => b[1] - a[1])
        .slice(0, 6)
        .map(([label, value]) => ({
          label: label.toUpperCase(),
          value,
          color: colors[label.toUpperCase()] ?? '#0f766e',
        }))
    }
    return countBy(logs.data ?? [], (log) => (log.level ?? 'LOG').toUpperCase())
      .map((row) => ({ ...row, color: colors[row.label] ?? '#0f766e' }))
  }, [logs.data, logsSummary.data])
  const onlineNodes = (nodes.data ?? []).filter((node) => node.online).length
  const pendingAckCount = messagePending.data?.pending_ack?.length ?? 0
  const pendingResponseCount = messagePending.data?.pending_response?.length ?? 0

  useEffect(() => {
    const sample = makeMetricSample(system.data)
    if (!sample) return

    setMetricSamples((current) => {
      const last = current[current.length - 1]
      if (last && sample.ts - last.ts < 4000) return current
      return [...current, sample].slice(-36)
    })
  }, [system.data])

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
              gatewayInfo.refetch()
              metricsHistory.refetch()
              radioStatus.refetch()
              nodes.refetch()
              messages.refetch()
              messageStats.refetch()
              messagePending.refetch()
              logs.refetch()
              logsSummary.refetch()
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
          sub={nodes.data ? `${onlineNodes} online` : undefined}
          loading={nodes.isLoading}
          accent
        />
        <StatCard
          icon={MessageSquare}
          label="Messages"
          value={messageStats.data?.total ?? messages.data?.length ?? '—'}
          sub={messageStats.data?.failed !== undefined ? `${messageStats.data.failed} failed` : undefined}
          loading={messages.isLoading || messageStats.isLoading}
        />
        <StatCard
          icon={Hourglass}
          label="Pending"
          value={pendingAckCount + pendingResponseCount}
          sub={`${pendingAckCount} ACK, ${pendingResponseCount} response`}
          loading={messagePending.isLoading}
        />
        <StatCard
          icon={Clock}
          label="Uptime"
          value={formatUptime(system.data?.uptime)}
          loading={system.isLoading}
        />
      </div>

      {/* Grafana-style panels */}
      <div className="grid grid-cols-1 gap-4 xl:grid-cols-3">
        <ChartPanel
          title="System Telemetry"
          description={`CPU, memory, disk and temperature over ${systemRangeOption.label}`}
          icon={Activity}
          actions={<TimeRangeSelect value={systemRange} onChange={setSystemRange} />}
        >
          <LineChart samples={chartMetricSamples} rangeMs={systemRangeMs} />
        </ChartPanel>

        <ChartPanel
          title="Message Traffic"
          description={`Sent and received frames over ${messageRangeOption.label}`}
          icon={BarChart3}
          actions={<TimeRangeSelect value={messageRange} onChange={setMessageRange} />}
        >
          <StackedMessageBars buckets={messageBuckets} />
        </ChartPanel>

        <ChartPanel
          title="Message Status"
          description="Current history distribution"
          icon={MessageSquare}
        >
          <HorizontalBars data={messageStatusBars} emptyLabel="No messages" />
        </ChartPanel>
      </div>

      <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
        <ChartPanel
          title="Log Levels"
          description="Recent backend log mix"
          icon={Gauge}
        >
          <HorizontalBars data={logLevelBars} emptyLabel="No logs" />
        </ChartPanel>

        <ChartPanel
          title="Radio Counters"
          description="SX1276/RFM95 runtime status"
          icon={Radio}
        >
          <RadioBars radio={radioStatus.data} />
        </ChartPanel>

        <ChartPanel
          title="Node RSSI"
          description="Signal, SNR and battery by paired node"
          icon={Signal}
        >
          <RssiBars nodes={nodes.data ?? []} />
        </ChartPanel>
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
              {(gateway || system.data) && (
                <StatusBadge active={gatewayStatus} activeLabel="Online" inactiveLabel="Offline" />
              )}
            </div>
          </CardHeader>
          <CardContent className="space-y-2">
            {(gatewayInfo.isLoading || system.isLoading) && !gateway && !system.data && (
              <div className="space-y-2">
                {[...Array(5)].map((_, i) => <Skeleton key={i} className="h-4 w-full" />)}
              </div>
            )}
            {(gatewayInfo.error || system.error) && !gateway && !system.data && (
              <ErrorDisplay error={gatewayInfo.error ?? system.error} compact onRetry={() => {
                gatewayInfo.refetch()
                system.refetch()
              }} />
            )}
            {(gateway || system.data) && (
              <dl className="space-y-2">
                {gatewayRows.map(([k, v]) => (
                  <div key={String(k)} className="flex items-center justify-between gap-4">
                    <dt className="text-xs text-teal-500 dark:text-teal-400 shrink-0">{k}</dt>
                    <dd className="text-xs font-medium text-teal-900 dark:text-teal-100 font-mono-feature truncate text-right">{String(v)}</dd>
                  </div>
                ))}
              </dl>
            )}
            {!gatewayInfo.isLoading && !system.isLoading && !gatewayInfo.error && !system.error && !gateway && !system.data && (
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
