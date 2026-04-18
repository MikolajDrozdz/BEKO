import { AlertCircle, RefreshCw } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { cn } from '@/lib/utils/cn'
import { GatewayApiError } from '@/lib/api/client'

interface ErrorDisplayProps {
  error: Error | unknown
  onRetry?: () => void
  className?: string
  compact?: boolean
}

function extractMessage(error: unknown): string {
  if (error instanceof GatewayApiError) return error.detail
  if (error instanceof Error) return error.message
  return 'An unexpected error occurred'
}

export function ErrorDisplay({ error, onRetry, className, compact }: ErrorDisplayProps) {
  const message = extractMessage(error)

  if (compact) {
    return (
      <div className={cn('flex items-center gap-2 text-red-600 dark:text-red-400 text-xs', className)}>
        <AlertCircle className="h-3.5 w-3.5 shrink-0" />
        <span>{message}</span>
        {onRetry && (
          <button onClick={onRetry} className="underline hover:no-underline">
            Retry
          </button>
        )}
      </div>
    )
  }

  return (
    <div
      className={cn(
        'flex flex-col items-center gap-3 rounded-lg border border-red-200 dark:border-red-800/50',
        'bg-red-50 dark:bg-red-950/20 p-6 text-center',
        className,
      )}
    >
      <AlertCircle className="h-8 w-8 text-red-500 dark:text-red-400" />
      <div className="space-y-1">
        <p className="text-sm font-medium text-red-800 dark:text-red-300">Failed to load</p>
        <p className="text-xs text-red-600 dark:text-red-400 max-w-sm">{message}</p>
      </div>
      {onRetry && (
        <Button variant="outline" size="sm" onClick={onRetry}>
          <RefreshCw className="h-3.5 w-3.5" />
          Try again
        </Button>
      )}
    </div>
  )
}
