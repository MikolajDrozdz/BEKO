import { cn } from '@/lib/utils/cn'

interface LoadingSpinnerProps {
  className?: string
  size?: 'sm' | 'md' | 'lg'
}

export function LoadingSpinner({ className, size = 'md' }: LoadingSpinnerProps) {
  const sizeClass = {
    sm: 'h-4 w-4 border-[1.5px]',
    md: 'h-6 w-6 border-2',
    lg: 'h-8 w-8 border-2',
  }[size]

  return (
    <span
      className={cn(
        'inline-block animate-spin rounded-full border-teal-300 dark:border-teal-700 border-t-ivory-700 dark:border-t-ivory-400',
        sizeClass,
        className,
      )}
    />
  )
}

export function PageLoader() {
  return (
    <div className="flex h-full flex-1 items-center justify-center py-16">
      <LoadingSpinner size="lg" />
    </div>
  )
}
