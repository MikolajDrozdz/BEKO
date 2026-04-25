import { NavLink, useLocation } from 'react-router-dom'
import {
  LayoutDashboard,
  Antenna,
  Cpu,
  MessageSquare,
  Settings,
  ScrollText,
  Radio,
  Users,
} from 'lucide-react'
import { cn } from '@/lib/utils/cn'
import { useAuth } from '@/context/AuthContext'
import type { Capability } from '@/types/api'

interface NavItem {
  to: string
  label: string
  icon: React.ElementType
  exact?: boolean
  permission?: Capability
  adminOnly?: boolean
}

const NAV_ITEMS: NavItem[] = [
  { to: '/', label: 'Dashboard', icon: LayoutDashboard, exact: true, permission: 'dashboard' },
  { to: '/pairing', label: 'Pairing', icon: Antenna, permission: 'pairing' },
  { to: '/nodes', label: 'Nodes', icon: Cpu, permission: 'nodes' },
  { to: '/messenger', label: 'Messenger', icon: MessageSquare, permission: 'messages' },
  { to: '/system', label: 'System', icon: Settings, permission: 'system' },
  { to: '/logs', label: 'Logs', icon: ScrollText, permission: 'logs' },
  { to: '/users', label: 'Users', icon: Users, adminOnly: true },
]

interface SidebarProps {
  collapsed?: boolean
}

export function Sidebar({ collapsed }: SidebarProps) {
  const location = useLocation()
  const { hasPermission, isAdmin } = useAuth()

  const visibleItems = NAV_ITEMS.filter((item) => {
    if (item.adminOnly) return isAdmin
    if (item.permission) return hasPermission(item.permission)
    return true
  })

  return (
    <aside
      className={cn(
        'flex flex-col h-full border-r border-teal-200 dark:border-teal-800',
        'bg-white dark:bg-teal-900 transition-all duration-200',
        collapsed ? 'w-14' : 'w-[var(--sidebar-width,240px)]',
      )}
    >
      {/* Logo */}
      <div className={cn('flex items-center gap-2.5 border-b border-teal-200 dark:border-teal-800 px-4 h-14', collapsed && 'justify-center px-0')}>
        <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-md bg-ivory-700 dark:bg-ivory-500">
          <Radio className="h-4 w-4 text-white dark:text-teal-950" />
        </div>
        {!collapsed && (
          <div className="min-w-0">
            <p className="text-sm font-semibold text-teal-900 dark:text-teal-50 leading-tight truncate">BEKO Pager</p>
            <p className="text-[10px] text-teal-500 dark:text-teal-400 leading-tight truncate">Gateway Panel</p>
          </div>
        )}
      </div>

      {/* Navigation */}
      <nav className="flex-1 p-2 space-y-0.5 overflow-y-auto">
        {visibleItems.map((item) => {
          const isActive = item.exact
            ? location.pathname === item.to
            : location.pathname.startsWith(item.to)

          return (
            <NavLink
              key={item.to}
              to={item.to}
              className={cn(
                'flex items-center gap-2.5 rounded-md px-2.5 py-2 text-sm font-medium transition-colors',
                collapsed && 'justify-center px-0 w-10 mx-auto',
                isActive
                  ? 'bg-ivory-100 dark:bg-ivory-900/50 text-ivory-800 dark:text-ivory-400'
                  : 'text-teal-600 dark:text-teal-400 hover:bg-teal-100 dark:hover:bg-teal-800 hover:text-teal-900 dark:hover:text-teal-50',
              )}
              title={collapsed ? item.label : undefined}
            >
              <item.icon className={cn('h-4 w-4 shrink-0', isActive && 'text-ivory-700 dark:text-ivory-400')} />
              {!collapsed && <span className="truncate">{item.label}</span>}
            </NavLink>
          )
        })}
      </nav>

      {/* Footer */}
      {!collapsed && (
        <div className="p-3 border-t border-teal-200 dark:border-teal-800">
          <p className="text-[10px] text-teal-400 dark:text-teal-600 text-center">
            BEKO Gateway v0.1
          </p>
        </div>
      )}
    </aside>
  )
}
