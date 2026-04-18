import { forwardRef, type TextareaHTMLAttributes } from 'react'
import { cn } from '@/lib/utils/cn'

export interface TextareaProps extends TextareaHTMLAttributes<HTMLTextAreaElement> {
  error?: boolean
}

const Textarea = forwardRef<HTMLTextAreaElement, TextareaProps>(
  ({ className, error, ...props }, ref) => {
    return (
      <textarea
        className={cn(
          'flex min-h-[80px] w-full rounded-md border bg-white px-3 py-2 text-sm shadow-sm transition-colors',
          'placeholder:text-teal-400 dark:placeholder:text-teal-600',
          'focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ivory-600',
          'disabled:cursor-not-allowed disabled:opacity-50 resize-y',
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
Textarea.displayName = 'Textarea'

export { Textarea }
