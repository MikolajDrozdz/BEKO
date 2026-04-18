import { motion, AnimatePresence } from 'framer-motion'
import {
  Antenna,
  Play,
  RefreshCw,
  Radio,
  CheckCircle2,
} from 'lucide-react'
import { usePairingStatus, usePairingActions } from '@/hooks/usePairing'
import { SectionHeader } from '@/components/common/SectionHeader'
import { StatusBadge } from '@/components/common/StatusBadge'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { EmptyState } from '@/components/common/EmptyState'
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { Skeleton } from '@/components/ui/skeleton'
import { Separator } from '@/components/ui/separator'

function BroadcastAnimation() {
  return (
    <motion.div
      initial={{ opacity: 0, height: 0 }}
      animate={{ opacity: 1, height: 112 }}
      exit={{ opacity: 0, height: 0 }}
      transition={{ duration: 0.35, ease: 'easeInOut' }}
      className="relative flex items-center justify-center overflow-hidden"
    >
      {/* Expanding rings */}
      {[0, 1, 2].map((i) => (
        <motion.span
          key={i}
          className="absolute rounded-full border-2 border-ivory-400/50 dark:border-ivory-500/40"
          style={{ width: 44, height: 44 }}
          animate={{
            width: [44, 130],
            height: [44, 130],
            opacity: [0.75, 0],
          }}
          transition={{
            duration: 2,
            delay: i * 0.65,
            repeat: Infinity,
            ease: 'easeOut',
          }}
        />
      ))}

      {/* Centre icon */}
      <motion.div
        className="relative z-10 flex h-11 w-11 items-center justify-center rounded-full bg-ivory-100 dark:bg-ivory-900/60 ring-2 ring-ivory-300 dark:ring-ivory-700/60 shadow-sm"
        animate={{ scale: [1, 1.07, 1] }}
        transition={{ duration: 2, repeat: Infinity, ease: 'easeInOut' }}
      >
        <Antenna className="h-5 w-5 text-ivory-600 dark:text-ivory-400" />
      </motion.div>

      {/* Label */}
      <div className="absolute bottom-1 left-0 right-0 flex justify-center">
        <span className="flex items-center gap-1.5 text-[10px] text-teal-500 dark:text-teal-400 font-medium tracking-wide uppercase">
          <span className="h-1.5 w-1.5 rounded-full bg-teal-400 animate-pulse" />
          Broadcasting PAIR_REQ
        </span>
      </div>
    </motion.div>
  )
}

function PairedNodeRow({ nodeHex }: { nodeHex: string }) {
  return (
    <motion.div
      initial={{ opacity: 0, y: 10 }}
      animate={{ opacity: 1, y: 0 }}
      exit={{ opacity: 0, y: -6 }}
      transition={{ duration: 0.2 }}
      className="flex items-center gap-3 rounded-lg border border-teal-200 dark:border-teal-800 bg-white dark:bg-teal-900 p-3"
    >
      <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-md bg-ivory-100 dark:bg-ivory-900/50">
        <CheckCircle2 className="h-4 w-4 text-ivory-600 dark:text-ivory-400" />
      </div>
      <div className="flex-1 min-w-0">
        <p className="text-sm font-semibold text-teal-900 dark:text-teal-50 font-mono-feature">
          {nodeHex}
        </p>
        <p className="text-xs text-teal-500 dark:text-teal-400">PAIR_RESP received — node paired</p>
      </div>
      <Badge variant="success" className="shrink-0">Paired</Badge>
    </motion.div>
  )
}

export function PairingPage() {
  const actions = usePairingActions()
  const { data: status, isLoading, error, refetch } = usePairingStatus(true)

  const isPending = actions.isPolling
  const pairedNodes = status?.paired_nodes_list ?? []
  const pairedCount = status?.paired_nodes_count ?? pairedNodes.length

  return (
    <div className="space-y-6">
      <SectionHeader
        title="Pairing Center"
        description="Pair new STM32 nodes to the gateway"
        actions={
          <Button variant="outline" size="sm" onClick={() => refetch()}>
            <RefreshCw className="h-3.5 w-3.5" />
            Refresh
          </Button>
        }
      />

      {/* Pairing flow explanation */}
      <Card className="border-teal-200/80 dark:border-teal-800/80">
        <CardContent className="pt-5">
          <div className="flex flex-wrap gap-0">
            {[
              { step: '1', label: 'Send PAIR_REQ', desc: 'Gateway broadcasts encrypted pairing request' },
              { step: '2', label: 'Node listens', desc: 'STM32 node receives the PAIR_REQ' },
              { step: '3', label: 'User confirms', desc: 'User presses accept on STM32 device' },
              { step: '4', label: 'Node paired', desc: 'Gateway receives PAIR_RESP and saves node' },
            ].map((s, i, arr) => (
              <div key={s.step} className="flex items-center">
                <div className="flex flex-col items-center text-center px-4">
                  <div className="flex h-7 w-7 items-center justify-center rounded-full bg-ivory-100 dark:bg-ivory-900/50 text-xs font-semibold text-ivory-700 dark:text-ivory-400 mb-1">
                    {s.step}
                  </div>
                  <p className="text-xs font-medium text-teal-900 dark:text-teal-50">{s.label}</p>
                  <p className="text-[10px] text-teal-500 dark:text-teal-400 max-w-[80px]">{s.desc}</p>
                </div>
                {i < arr.length - 1 && (
                  <div className="h-px w-8 bg-teal-200 dark:bg-teal-700 shrink-0 mb-5" />
                )}
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <div className="grid gap-4 lg:grid-cols-[320px_1fr]">
        {/* Control Panel */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Antenna className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
              Pairing Control
            </CardTitle>
            <CardDescription>Send PAIR_REQ to nodes</CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            {/* Status */}
            <div className="flex items-center justify-between rounded-md border border-teal-100 dark:border-teal-800 bg-teal-50 dark:bg-teal-800/30 px-3 py-2.5">
              <span className="text-xs text-teal-600 dark:text-teal-400 font-medium">Request status</span>
              {isLoading ? (
                <Skeleton className="h-5 w-16" />
              ) : (
                <StatusBadge
                  active={isPending}
                  activeLabel="Waiting"
                  inactiveLabel="Idle"
                  pulse={isPending}
                />
              )}
            </div>

            {/* Broadcast animation */}
            <AnimatePresence>
              {isPending && <BroadcastAnimation />}
            </AnimatePresence>

            <Separator />

            <motion.div whileTap={{ scale: 0.97 }} transition={{ duration: 0.1 }}>
              <Button
                className="w-full"
                onClick={() => actions.start.mutate(undefined)}
                loading={actions.start.isPending}
              >
                <motion.span
                  animate={isPending ? { rotate: [0, 15, -15, 0] } : { rotate: 0 }}
                  transition={{ duration: 0.4, delay: 0.1 }}
                >
                  <Play className="h-4 w-4" />
                </motion.span>
                Send PAIR_REQ (Broadcast)
              </Button>
            </motion.div>

            {error && (
              <ErrorDisplay error={error} compact onRetry={refetch} />
            )}

            <Separator />

            <div className="space-y-1">
              <p className="text-xs font-medium text-teal-700 dark:text-teal-300">Paired nodes (session)</p>
              <motion.p
                key={pairedCount}
                initial={{ opacity: 0, y: -8 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ duration: 0.3, ease: 'easeOut' }}
                className="text-2xl font-semibold text-teal-900 dark:text-teal-50"
              >
                {isLoading ? '—' : pairedCount}
              </motion.p>
              <p className="text-[10px] text-teal-400 dark:text-teal-500">
                Nodes that responded with PAIR_RESP
              </p>
            </div>
          </CardContent>
        </Card>

        {/* Paired Nodes */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Radio className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
              Paired Nodes
            </CardTitle>
            <CardDescription>
              Nodes that have responded with PAIR_RESP and been saved to the gateway
            </CardDescription>
          </CardHeader>
          <CardContent>
            {isLoading && (
              <div className="space-y-3">
                {[...Array(2)].map((_, i) => (
                  <Skeleton key={i} className="h-16 w-full" />
                ))}
              </div>
            )}

            {!isLoading && (
              <AnimatePresence mode="popLayout">
                {pairedNodes.length > 0 ? (
                  <div className="space-y-2">
                    {pairedNodes.map((nodeHex: string) => (
                      <PairedNodeRow key={nodeHex} nodeHex={nodeHex} />
                    ))}
                  </div>
                ) : (
                  <EmptyState
                    icon={Antenna}
                    title="No paired nodes yet"
                    description={
                      isPending
                        ? 'PAIR_REQ sent — waiting for STM32 nodes to respond...'
                        : 'Send a PAIR_REQ to start the pairing process.'
                    }
                  />
                )}
              </AnimatePresence>
            )}
          </CardContent>
        </Card>
      </div>
    </div>
  )
}
