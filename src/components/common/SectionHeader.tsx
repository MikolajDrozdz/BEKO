import { type ReactNode } from 'react'
import { cn } from '@/lib/utils/cn'

interface SectionHeaderProps {
  title: string
  description?: string
  actions?: ReactNode
  className?: string
}

export function SectionHeader({ title, description, actions, className }: SectionHeaderProps) {
  return (
    <div className={cn('flex items-start justify-between gap-4', className)}>
      <div className="min-w-0">
        <h1 className="text-lg font-semibold tracking-tight text-teal-900 dark:text-teal-50">{title}</h1>
        {description && (
          <p className="mt-0.5 text-sm text-teal-500 dark:text-teal-400">{description}</p>
        )}
      </div>
      {actions && <div className="flex shrink-0 items-center gap-2">{actions}</div>}
    </div>
  )
}
