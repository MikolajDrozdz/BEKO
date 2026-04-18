import { Radio, Server, RefreshCw, Zap, Cpu } from 'lucide-react'
import { useSystemInfo, useForcePair } from '@/hooks/useSystem'
import { SectionHeader } from '@/components/common/SectionHeader'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { ConfirmDialog } from '@/components/common/ConfirmDialog'
import { StatusBadge } from '@/components/common/StatusBadge'
import { CopyButton } from '@/components/common/CopyButton'
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Skeleton } from '@/components/ui/skeleton'
import { formatUptime, formatBytes, formatFrequency, formatNodeId } from '@/lib/utils/format'

function InfoRow({ label, value, mono }: { label: string; value?: string | number; mono?: boolean }) {
  const display = value !== undefined && value !== null && value !== '' ? String(value) : '—'
  return (
    <div className="flex items-center justify-between gap-6 py-2 border-b border-teal-100 dark:border-teal-800/60 last:border-0">
      <dt className="text-xs text-teal-500 dark:text-teal-400 shrink-0 w-36">{label}</dt>
      <dd className={`text-xs font-medium text-teal-900 dark:text-teal-50 truncate text-right flex items-center gap-1.5 ${mono ? 'font-mono-feature' : ''}`}>
        {display}
        {display !== '—' && mono && <CopyButton value={display} />}
      </dd>
    </div>
  )
}

function InfoSection({ title, icon: Icon, children, loading }: {
  title: string
  icon: typeof Radio
  children: React.ReactNode
  loading?: boolean
}) {
  return (
    <Card>
      <CardHeader className="pb-3">
        <CardTitle className="flex items-center gap-2 text-sm">
          <Icon className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
          {title}
        </CardTitle>
      </CardHeader>
      <CardContent>
        {loading ? (
          <div className="space-y-2">
            {[...Array(5)].map((_, i) => <Skeleton key={i} className="h-7 w-full" />)}
          </div>
        ) : (
          <dl>{children}</dl>
        )}
      </CardContent>
    </Card>
  )
}

export function SystemPage() {
  const { data, isLoading, error, refetch } = useSystemInfo()
  const forcePair = useForcePair()

  const gatewayId = data?.gateway_id ?? data?.id

  return (
    <div className="space-y-6">
      <SectionHeader
        title="System Panel"
        description="Gateway configuration, radio parameters, and maintenance actions"
        actions={
          <Button variant="outline" size="sm" onClick={() => refetch()}>
            <RefreshCw className="h-3.5 w-3.5" />
            Refresh
          </Button>
        }
      />

      {error && <ErrorDisplay error={error} onRetry={refetch} />}

      <div className="grid gap-4 lg:grid-cols-2">
        {/* Gateway Identity */}
        <InfoSection title="Gateway Identity" icon={Server} loading={isLoading}>
          {data && (
            <>
              <InfoRow label="Gateway ID" value={data.gateway_id_hex ?? (gatewayId !== undefined ? formatNodeId(gatewayId) : undefined)} mono />
              <InfoRow label="Protocol Version" value={data.protocol_version} />
              <InfoRow label="Status" value={data.status} />
              <InfoRow label="Hostname" value={data.hostname} />
              <InfoRow label="Platform" value={data.platform} />
              <InfoRow label="Firmware / Version" value={data.version ?? data.firmware} />
              <InfoRow label="IP Address" value={data.ip} mono />
              <div className="py-2 flex items-center justify-between">
                <dt className="text-xs text-teal-500 dark:text-teal-400 shrink-0 w-36">Online</dt>
                <StatusBadge active={true} activeLabel="Online" />
              </div>
            </>
          )}
        </InfoSection>

        {/* Radio Parameters */}
        <InfoSection title="Radio Parameters" icon={Radio} loading={isLoading}>
          {data && (
            <>
              <InfoRow label="Frequency" value={formatFrequency(data.radio?.frequency)} />
              <InfoRow label="Bandwidth" value={data.radio?.bandwidth !== undefined ? `${data.radio.bandwidth} kHz` : undefined} />
              <InfoRow label="Spreading Factor" value={data.radio?.spreading_factor} />
              <InfoRow label="Coding Rate" value={data.radio?.coding_rate} />
              <InfoRow label="TX Power" value={data.radio?.tx_power !== undefined ? `${data.radio.tx_power} dBm` : undefined} />
              <InfoRow label="Sync Word" value={data.radio?.sync_word !== undefined ? `0x${Number(data.radio.sync_word).toString(16).toUpperCase()}` : undefined} mono />
            </>
          )}
        </InfoSection>

        {/* Hardware */}
        <InfoSection title="System Resources" icon={Cpu} loading={isLoading}>
          {data && (
            <>
              <InfoRow label="Uptime" value={formatUptime(data.uptime)} />
              <InfoRow label="CPU Temperature" value={data.cpu_temp !== undefined ? `${data.cpu_temp.toFixed(1)} °C` : undefined} />
              <InfoRow label="Memory Used" value={formatBytes(data.memory_used)} />
              <InfoRow label="Memory Total" value={formatBytes(data.memory_total)} />
              {data.memory_used !== undefined && data.memory_total !== undefined && (
                <div className="py-2">
                  <div className="flex items-center justify-between mb-1.5">
                    <span className="text-xs text-teal-500 dark:text-teal-400">Memory Usage</span>
                    <span className="text-xs font-medium text-teal-900 dark:text-teal-50">
                      {Math.round((data.memory_used / data.memory_total) * 100)}%
                    </span>
                  </div>
                  <div className="h-1.5 w-full rounded-full bg-teal-100 dark:bg-teal-800">
                    <div
                      className="h-full rounded-full bg-ivory-600 dark:bg-ivory-500 transition-all"
                      style={{ width: `${Math.min(100, Math.round((data.memory_used / data.memory_total) * 100))}%` }}
                    />
                  </div>
                </div>
              )}
            </>
          )}
        </InfoSection>

        {/* Admin Actions */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2 text-sm">
              <Zap className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
              Administrative Actions
            </CardTitle>
            <CardDescription>Maintenance operations — use with care</CardDescription>
          </CardHeader>
          <CardContent className="space-y-3">
            <div className="rounded-lg border border-teal-100 dark:border-teal-800 p-3 space-y-1.5">
              <p className="text-sm font-medium text-teal-900 dark:text-teal-50">Force Pair</p>
              <p className="text-xs text-teal-500 dark:text-teal-400">
                Manually trigger a pairing operation at the system level. Use when standard pairing flow does not succeed.
              </p>
              <div className="pt-1">
                <ConfirmDialog
                  trigger={
                    <Button variant="warning" size="sm">
                      Force Pair
                    </Button>
                  }
                  title="Force System Pair"
                  description="This triggers a forced pairing operation at the system level. Are you sure you want to proceed?"
                  confirmLabel="Force Pair"
                  variant="warning"
                  onConfirm={() => forcePair.mutate()}
                  loading={forcePair.isPending}
                />
              </div>
            </div>

            <p className="text-xs text-teal-400 dark:text-teal-600">
              Per-node actions (sync counter, rotate keys) are available in the Nodes view.
            </p>
          </CardContent>
        </Card>
      </div>
    </div>
  )
}
