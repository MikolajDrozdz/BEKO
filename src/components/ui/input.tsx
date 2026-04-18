import { forwardRef, type InputHTMLAttributes } from 'react'
import { cn } from '@/lib/utils/cn'

export interface InputProps extends InputHTMLAttributes<HTMLInputElement> {
  error?: boolean
}

const Input = forwardRef<HTMLInputElement, InputProps>(
  ({ className, type, error, ...props }, ref) => {
    return (
      <input
        type={type}
        className={cn(
          'flex h-9 w-full rounded-md border bg-white px-3 py-1 text-sm shadow-sm transition-colors',
          'placeholder:text-teal-400 dark:placeholder:text-teal-600',
          'focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ivory-600',
          'disabled:cursor-not-allowed disabled:opacity-50',
          error
            ? 'border-red-500 dark:border-red-700 focus-visible:ring-red-500'
            : 'border-teal-300 dark:border-teal-700',
          'dark:bg-teal-900 dark:text-teal-50',
          className,
        )}
        ref={ref}
        {...props}
      />
    )
  },
)
Input.displayName = 'Input'

export { Input }
