import { type HTMLAttributes } from 'react'
import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from '@/lib/utils/cn'

const badgeVariants = cva(
  'inline-flex items-center gap-1 rounded-full px-2 py-0.5 text-xs font-medium transition-colors',
  {
    variants: {
      variant: {
        default: 'bg-teal-100 text-teal-800 dark:bg-teal-800 dark:text-teal-100',
        accent: 'bg-ivory-100 text-ivory-800 dark:bg-ivory-900 dark:text-ivory-300',
        success: 'bg-green-100 text-green-800 dark:bg-green-900/40 dark:text-green-400',
        warning: 'bg-amber-100 text-amber-800 dark:bg-amber-900/40 dark:text-amber-400',
        destructive: 'bg-red-100 text-red-800 dark:bg-red-900/40 dark:text-red-400',
        outline: 'border border-teal-300 dark:border-teal-700 text-teal-700 dark:text-teal-300',
        active: 'bg-ivory-700 text-white dark:bg-ivory-500 dark:text-teal-950',
      },
    },
    defaultVariants: {
      variant: 'default',
    },
  },
)

export interface BadgeProps
  extends HTMLAttributes<HTMLDivElement>,
    VariantProps<typeof badgeVariants> {}

function Badge({ className, variant, ...props }: BadgeProps) {
  return <div className={cn(badgeVariants({ variant }), className)} {...props} />
}

export { Badge, badgeVariants }
