import { type LucideIcon } from 'lucide-react'
import { cn } from '@/lib/utils/cn'

interface EmptyStateProps {
  icon?: LucideIcon
  title: string
  description?: string
  action?: React.ReactNode
  className?: string
}

export function EmptyState({ icon: Icon, title, description, action, className }: EmptyStateProps) {
  return (
    <div className={cn('flex flex-col items-center justify-center gap-3 py-12 text-center', className)}>
      {Icon && (
        <div className="flex h-12 w-12 items-center justify-center rounded-full bg-teal-100 dark:bg-teal-800">
          <Icon className="h-6 w-6 text-teal-400 dark:text-teal-500" />
        </div>
      )}
      <div className="space-y-1">
        <p className="text-sm font-medium text-teal-900 dark:text-teal-50">{title}</p>
        {description && (
          <p className="text-xs text-teal-500 dark:text-teal-400 max-w-xs">{description}</p>
        )}
      </div>
      {action && <div className="mt-1">{action}</div>}
    </div>
  )
}
