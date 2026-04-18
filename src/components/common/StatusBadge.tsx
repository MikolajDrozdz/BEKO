import { Badge } from '@/components/ui/badge'
import { cn } from '@/lib/utils/cn'

interface StatusBadgeProps {
  active: boolean
  activeLabel?: string
  inactiveLabel?: string
  pulse?: boolean
  className?: string
}

export function StatusBadge({
  active,
  activeLabel = 'Active',
  inactiveLabel = 'Inactive',
  pulse = false,
  className,
}: StatusBadgeProps) {
  return (
    <Badge
      variant={active ? 'success' : 'default'}
      className={cn('gap-1.5', className)}
    >
      <span
        className={cn(
          'h-1.5 w-1.5 rounded-full',
          active ? 'bg-green-500 dark:bg-green-400' : 'bg-teal-400 dark:bg-teal-600',
          active && pulse && 'animate-pulse',
        )}
      />
      {active ? activeLabel : inactiveLabel}
    </Badge>
  )
}
