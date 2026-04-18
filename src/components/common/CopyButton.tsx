import { useState } from 'react'
import { Copy, Check } from 'lucide-react'
import { Button, type ButtonProps } from '@/components/ui/button'
import { cn } from '@/lib/utils/cn'

interface CopyButtonProps extends Omit<ButtonProps, 'onClick'> {
  value: string
  label?: string
}

export function CopyButton({ value, label, className, size = 'icon-sm', variant = 'ghost', ...props }: CopyButtonProps) {
  const [copied, setCopied] = useState(false)

  async function handleCopy() {
    try {
      await navigator.clipboard.writeText(value)
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    } catch {
      // clipboard not available
    }
  }

  return (
    <Button
      variant={variant}
      size={size}
      onClick={handleCopy}
      title={label ?? 'Copy to clipboard'}
      className={cn('transition-colors', copied && 'text-ivory-700 dark:text-ivory-400', className)}
      {...props}
    >
      {copied ? <Check className="h-3.5 w-3.5" /> : <Copy className="h-3.5 w-3.5" />}
      {label && <span>{label}</span>}
    </Button>
  )
}
