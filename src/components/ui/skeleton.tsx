import { type HTMLAttributes } from 'react'
import { cn } from '@/lib/utils/cn'

function Skeleton({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn(
        'animate-pulse rounded-md bg-teal-200 dark:bg-teal-800',
        className,
      )}
      {...props}
    />
  )
}

export { Skeleton }
