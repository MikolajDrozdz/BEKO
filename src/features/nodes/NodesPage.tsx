import { useState } from 'react'
import { Cpu, Trash2, RefreshCw, Search, RotateCcw, ArrowUpDown } from 'lucide-react'
import { useNodes, useDeleteNode, useSyncCounter, useRotateKeys } from '@/hooks/useNodes'
import { SectionHeader } from '@/components/common/SectionHeader'
import { EmptyState } from '@/components/common/EmptyState'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { CopyButton } from '@/components/common/CopyButton'
import { ConfirmDialog } from '@/components/common/ConfirmDialog'
import { StatusBadge } from '@/components/common/StatusBadge'
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
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from '@/components/ui/tooltip'
import { formatTimestamp, formatNodeId } from '@/lib/utils/format'
import type { Node } from '@/types/api'

function NodeRow({ node, onDelete, onSync, onRotate, deleting, syncing, rotating }: {
  node: Node
  onDelete: () => void
  onSync: () => void
  onRotate: () => void
  deleting: boolean
  syncing: boolean
  rotating: boolean
}) {
  const id = node.node_id ?? node.id ?? 0

  return (
    <TableRow>
      <TableCell>
        <div className="flex items-center gap-2">
          <span className="font-mono-feature text-xs text-teal-900 dark:text-teal-50">{formatNodeId(id)}</span>
          <CopyButton value={String(id)} />
        </div>
        <span className="text-[10px] text-teal-400 dark:text-teal-600">{id}</span>
      </TableCell>
      <TableCell>
        {node.name ? (
          <span className="text-sm text-teal-900 dark:text-teal-50">{node.name}</span>
        ) : (
          <span className="text-xs text-teal-400 dark:text-teal-600">—</span>
        )}
      </TableCell>
      <TableCell>
        {node.coding_enabled !== undefined ? (
          <StatusBadge
            active={node.coding_enabled}
            activeLabel="Enabled"
            inactiveLabel="Disabled"
          />
        ) : (
          <span className="text-xs text-teal-400">—</span>
        )}
      </TableCell>
      <TableCell>
        {node.counter !== undefined ? (
          <span className="font-mono-feature text-xs">{node.counter}</span>
        ) : (
          <span className="text-xs text-teal-400">—</span>
        )}
      </TableCell>
      <TableCell>
        {node.rssi !== undefined ? (
          <span className="font-mono-feature text-xs">{node.rssi} dBm</span>
        ) : (
          <span className="text-xs text-teal-400">—</span>
        )}
      </TableCell>
      <TableCell>
        <span className="text-xs text-teal-600 dark:text-teal-400">
          {formatTimestamp(node.last_seen)}
        </span>
      </TableCell>
      <TableCell>
        <div className="flex items-center gap-1">
          <TooltipProvider>
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  variant="ghost"
                  size="icon-sm"
                  loading={syncing}
                  onClick={onSync}
                  title="Sync counter"
                >
                  <ArrowUpDown className="h-3.5 w-3.5" />
                </Button>
              </TooltipTrigger>
              <TooltipContent>Sync counter</TooltipContent>
            </Tooltip>
          </TooltipProvider>

          <TooltipProvider>
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  variant="ghost"
                  size="icon-sm"
                  loading={rotating}
                  onClick={onRotate}
                  title="Rotate keys"
                >
                  <RotateCcw className="h-3.5 w-3.5" />
                </Button>
              </TooltipTrigger>
              <TooltipContent>Rotate encryption keys</TooltipContent>
            </Tooltip>
          </TooltipProvider>

          <ConfirmDialog
            trigger={
              <Button variant="ghost" size="icon-sm" className="text-red-500 hover:text-red-600 dark:text-red-400">
                <Trash2 className="h-3.5 w-3.5" />
              </Button>
            }
            title="Remove Node"
            description={`Remove node ${formatNodeId(id)} from the gateway? This cannot be undone.`}
            confirmLabel="Remove"
            variant="destructive"
            onConfirm={onDelete}
            loading={deleting}
          />
        </div>
      </TableCell>
    </TableRow>
  )
}

export function NodesPage() {
  const { data: nodes, isLoading, error, refetch } = useNodes()
  const deleteNode = useDeleteNode()
  const syncCounter = useSyncCounter()
  const rotateKeys = useRotateKeys()
  const [search, setSearch] = useState('')

  const filtered = (nodes ?? []).filter((n) => {
    if (!search) return true
    const q = search.toLowerCase()
    const id = String(n.node_id ?? n.id ?? '')
    return (
      id.includes(q) ||
      formatNodeId(Number(id)).toLowerCase().includes(q) ||
      (n.name ?? '').toLowerCase().includes(q)
    )
  })

  return (
    <div className="space-y-6">
      <SectionHeader
        title="Nodes"
        description="Manage paired STM32 pager devices"
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
              <Cpu className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
              Paired Devices
              {nodes && (
                <Badge variant="outline" className="ml-1">{nodes.length}</Badge>
              )}
            </CardTitle>
            <div className="relative w-56">
              <Search className="absolute left-2.5 top-1/2 -translate-y-1/2 h-3.5 w-3.5 text-teal-400" />
              <Input
                placeholder="Search nodes..."
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
              {[...Array(3)].map((_, i) => <Skeleton key={i} className="h-12 w-full" />)}
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
                      <TableHead>Node ID</TableHead>
                      <TableHead>Name</TableHead>
                      <TableHead>Coding</TableHead>
                      <TableHead>Counter</TableHead>
                      <TableHead>RSSI</TableHead>
                      <TableHead>Last Seen</TableHead>
                      <TableHead className="w-[100px]">Actions</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {filtered.map((node: Node) => {
                      const id = node.node_id ?? node.id ?? 0
                      return (
                        <NodeRow
                          key={id}
                          node={node}
                          onDelete={() => deleteNode.mutate(id)}
                          onSync={() => syncCounter.mutate(id)}
                          onRotate={() => rotateKeys.mutate(id)}
                          deleting={deleteNode.isPending && deleteNode.variables === id}
                          syncing={syncCounter.isPending && syncCounter.variables === id}
                          rotating={rotateKeys.isPending && rotateKeys.variables === id}
                        />
                      )
                    })}
                  </TableBody>
                </Table>
              ) : (
                <EmptyState
                  icon={Cpu}
                  title={search ? 'No nodes match your search' : 'No paired nodes'}
                  description={search ? 'Try a different search term.' : 'Pair your first STM32 node via the Pairing Center.'}
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
