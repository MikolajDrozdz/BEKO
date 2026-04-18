import { forwardRef, type ButtonHTMLAttributes } from 'react'
import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from '@/lib/utils/cn'

const buttonVariants = cva(
  'inline-flex items-center justify-center gap-2 rounded-md text-sm font-medium transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ivory-600 focus-visible:ring-offset-2 disabled:pointer-events-none disabled:opacity-50 select-none',
  {
    variants: {
      variant: {
        default:
          'bg-ivory-700 text-white hover:bg-ivory-800 dark:bg-ivory-500 dark:hover:bg-ivory-600 dark:text-teal-950',
        secondary:
          'bg-teal-100 text-teal-900 hover:bg-teal-200 dark:bg-teal-800 dark:text-teal-50 dark:hover:bg-teal-700',
        outline:
          'border border-teal-300 bg-transparent hover:bg-teal-100 dark:border-teal-700 dark:hover:bg-teal-800 text-teal-900 dark:text-teal-50',
        ghost:
          'bg-transparent hover:bg-teal-100 dark:hover:bg-teal-800 text-teal-900 dark:text-teal-50',
        destructive:
          'bg-red-600 text-white hover:bg-red-700 dark:bg-red-700 dark:hover:bg-red-600',
        warning:
          'bg-amber-600 text-white hover:bg-amber-700',
        link:
          'text-ivory-700 dark:text-ivory-400 underline-offset-4 hover:underline p-0 h-auto',
      },
      size: {
        default: 'h-9 px-4 py-2',
        sm: 'h-7 px-3 text-xs',
        lg: 'h-11 px-6',
        icon: 'h-9 w-9 p-0',
        'icon-sm': 'h-7 w-7 p-0',
      },
    },
    defaultVariants: {
      variant: 'default',
      size: 'default',
    },
  },
)

export interface ButtonProps
  extends ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {
  loading?: boolean
}

const Button = forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, loading, children, disabled, ...props }, ref) => {
    return (
      <button
        className={cn(buttonVariants({ variant, size, className }))}
        ref={ref}
        disabled={disabled || loading}
        {...props}
      >
        {loading && (
          <span className="h-3.5 w-3.5 animate-spin rounded-full border-2 border-current border-t-transparent" />
        )}
        {children}
      </button>
    )
  },
)
Button.displayName = 'Button'

export { Button, buttonVariants }
