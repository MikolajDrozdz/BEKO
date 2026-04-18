import { type LucideIcon } from 'lucide-react'
import { Card, CardContent } from '@/components/ui/card'
import { Skeleton } from '@/components/ui/skeleton'
import { cn } from '@/lib/utils/cn'

interface StatCardProps {
  icon: LucideIcon
  label: string
  value?: string | number
  sub?: string
  loading?: boolean
  accent?: boolean
  className?: string
  iconClassName?: string
}

export function StatCard({ icon: Icon, label, value, sub, loading, accent, className, iconClassName }: StatCardProps) {
  return (
    <Card className={cn('overflow-hidden', className)}>
      <CardContent className="p-4">
        <div className="flex items-start justify-between gap-3">
          <div className="flex-1 min-w-0">
            <p className="text-xs font-medium uppercase tracking-wide text-teal-500 dark:text-teal-400 mb-1.5">{label}</p>
            {loading ? (
              <Skeleton className="h-6 w-20" />
            ) : (
              <p className={cn('text-xl font-semibold tracking-tight truncate', accent ? 'text-ivory-700 dark:text-ivory-400' : 'text-teal-900 dark:text-teal-50')}>
                {value ?? '—'}
              </p>
            )}
            {sub && !loading && (
              <p className="text-xs text-teal-500 dark:text-teal-400 mt-0.5 truncate">{sub}</p>
            )}
          </div>
          <div className={cn('flex h-9 w-9 shrink-0 items-center justify-center rounded-md', accent ? 'bg-ivory-100 dark:bg-ivory-900/50' : 'bg-teal-100 dark:bg-teal-800', iconClassName)}>
            <Icon className={cn('h-4 w-4', accent ? 'text-ivory-700 dark:text-ivory-400' : 'text-teal-500 dark:text-teal-400')} />
          </div>
        </div>
      </CardContent>
    </Card>
  )
}
