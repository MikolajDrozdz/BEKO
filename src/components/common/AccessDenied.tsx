import { ShieldOff } from 'lucide-react'

export function AccessDenied() {
  return (
    <div className="flex flex-col items-center justify-center py-20 gap-4 text-center">
      <div className="h-14 w-14 rounded-full bg-red-50 dark:bg-red-950/30 flex items-center justify-center">
        <ShieldOff className="h-6 w-6 text-red-400 dark:text-red-500" />
      </div>
      <div>
        <p className="text-sm font-medium text-teal-900 dark:text-teal-50">Access Denied</p>
        <p className="text-xs text-teal-500 dark:text-teal-400 mt-1 max-w-xs">
          You do not have permission to access this section. Contact an administrator to request access.
        </p>
      </div>
    </div>
  )
}
