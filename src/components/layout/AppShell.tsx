import { useState } from 'react'
import { Outlet } from 'react-router-dom'
import { Sidebar } from './Sidebar'
import { Topbar } from './Topbar'
import { Toaster } from 'sonner'

export function AppShell() {
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false)

  return (
    <div className="flex h-screen overflow-hidden bg-teal-50 dark:bg-teal-950">
      <Sidebar collapsed={sidebarCollapsed} />
      <div className="flex flex-1 flex-col overflow-hidden min-w-0">
        <Topbar
          onToggleSidebar={() => setSidebarCollapsed((v) => !v)}
        />
        <main className="flex-1 overflow-y-auto">
          <div className="p-6 max-w-screen-xl mx-auto animate-fade-in">
            <Outlet />
          </div>
        </main>
      </div>
      <Toaster
        position="bottom-right"
        offset={12}
        toastOptions={{
          classNames: {
            toast: 'border border-teal-200 dark:border-teal-800 bg-white dark:bg-teal-900 text-teal-900 dark:text-teal-50 shadow-lg rounded-lg text-sm',
            success: 'border-green-300 dark:border-green-800',
            error: 'border-red-300 dark:border-red-800',
            warning: 'border-amber-300 dark:border-amber-800',
          },
        }}
      />
    </div>
  )
}
