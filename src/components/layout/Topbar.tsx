import { useState, useRef, useEffect } from 'react'
import { Moon, Sun, Menu, Settings2, Wifi, LogOut, User, ShieldCheck, ChevronDown } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { useTheme } from '@/context/ThemeContext'
import { useAuth } from '@/context/AuthContext'
import { getBaseUrl, setBaseUrl } from '@/lib/api/client'
import { getAuthBaseUrl, setAuthBaseUrl } from '@/lib/api/authClient'
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from '@/components/ui/dialog'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'

interface TopbarProps {
  onToggleSidebar: () => void
}

function UserMenu() {
  const { user, logout, isAdmin } = useAuth()
  const [open, setOpen] = useState(false)
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    function handleClick(e: MouseEvent) {
      if (ref.current && !ref.current.contains(e.target as Node)) {
        setOpen(false)
      }
    }
    if (open) document.addEventListener('mousedown', handleClick)
    return () => document.removeEventListener('mousedown', handleClick)
  }, [open])

  if (!user) return null

  return (
    <div ref={ref} className="relative ml-1">
      <button
        type="button"
        onClick={() => setOpen((v) => !v)}
        className="flex items-center gap-1.5 rounded-md px-2 py-1 hover:bg-teal-100 dark:hover:bg-teal-800 transition-colors"
      >
        <div className="flex h-6 w-6 items-center justify-center rounded-full bg-teal-100 dark:bg-teal-800 border border-teal-200 dark:border-teal-700">
          {isAdmin
            ? <ShieldCheck className="h-3 w-3 text-ivory-700 dark:text-ivory-400" />
            : <User className="h-3 w-3 text-teal-600 dark:text-teal-400" />
          }
        </div>
        <span className="text-xs font-medium text-teal-700 dark:text-teal-300 max-w-[100px] truncate">
          {user.username}
        </span>
        <ChevronDown className="h-3 w-3 text-teal-400" />
      </button>

      {open && (
        <div className="absolute right-0 top-full mt-1 w-44 rounded-lg border border-teal-200 dark:border-teal-700 bg-white dark:bg-teal-900 shadow-lg z-50 py-1">
          <div className="px-3 py-2 border-b border-teal-100 dark:border-teal-800">
            <p className="text-xs font-medium text-teal-900 dark:text-teal-50">{user.username}</p>
            <p className="text-[10px] text-teal-500 dark:text-teal-400 capitalize">{user.role}</p>
          </div>
          <button
            type="button"
            onClick={() => { setOpen(false); logout() }}
            className="flex w-full items-center gap-2 px-3 py-2 text-xs text-red-600 dark:text-red-400 hover:bg-red-50 dark:hover:bg-red-950/20 transition-colors"
          >
            <LogOut className="h-3.5 w-3.5" />
            Sign out
          </button>
        </div>
      )}
    </div>
  )
}

export function Topbar({ onToggleSidebar }: TopbarProps) {
  const { theme, toggleTheme } = useTheme()
  const [settingsOpen, setSettingsOpen] = useState(false)
  const [gatewayUrlInput, setGatewayUrlInput] = useState(getBaseUrl())
  const [authUrlInput, setAuthUrlInput] = useState(getAuthBaseUrl())

  function handleSaveSettings() {
    setBaseUrl(gatewayUrlInput.trim())
    setAuthBaseUrl(authUrlInput.trim())
    setSettingsOpen(false)
    window.location.reload()
  }

  return (
    <header className="flex h-14 items-center justify-between gap-4 border-b border-teal-200 dark:border-teal-800 bg-white dark:bg-teal-900 px-4">
      <div className="flex items-center gap-2">
        <Button variant="ghost" size="icon" onClick={onToggleSidebar} title="Toggle sidebar">
          <Menu className="h-4 w-4" />
        </Button>
      </div>

      <div className="flex items-center gap-1">
        {/* Gateway URL indicator */}
        <div className="flex items-center gap-1.5 px-2 py-1 rounded-md bg-teal-50 dark:bg-teal-800 border border-teal-200 dark:border-teal-700 mr-2">
          <Wifi className="h-3 w-3 text-teal-500 dark:text-teal-400" />
          <span className="text-xs text-teal-600 dark:text-teal-300 font-mono-feature truncate max-w-[160px]">
            {getBaseUrl()}
          </span>
        </div>

        <Button variant="ghost" size="icon" onClick={() => setSettingsOpen(true)} title="Connection settings">
          <Settings2 className="h-4 w-4" />
        </Button>

        <Button variant="ghost" size="icon" onClick={toggleTheme} title="Toggle theme">
          {theme === 'dark' ? <Sun className="h-4 w-4" /> : <Moon className="h-4 w-4" />}
        </Button>

        <UserMenu />
      </div>

      {/* Settings dialog */}
      <Dialog open={settingsOpen} onOpenChange={setSettingsOpen}>
        <DialogContent className="max-w-sm">
          <DialogHeader>
            <DialogTitle>Connection Settings</DialogTitle>
          </DialogHeader>
          <div className="space-y-4">
            <div className="space-y-1.5">
              <Label htmlFor="gateway-url">Gateway API URL</Label>
              <Input
                id="gateway-url"
                value={gatewayUrlInput}
                onChange={(e) => setGatewayUrlInput(e.target.value)}
                placeholder="http://raspberrypi.local:8000"
                className="font-mono-feature text-xs"
              />
              <p className="text-[10px] text-teal-500 dark:text-teal-400">
                BEKO LoRa Gateway API endpoint.
              </p>
            </div>
            <div className="space-y-1.5">
              <Label htmlFor="auth-url">Auth Service URL</Label>
              <Input
                id="auth-url"
                value={authUrlInput}
                onChange={(e) => setAuthUrlInput(e.target.value)}
                placeholder="http://localhost:8001"
                className="font-mono-feature text-xs"
              />
              <p className="text-[10px] text-teal-500 dark:text-teal-400">
                BEKO Auth Service (login, users).
              </p>
            </div>
          </div>
          <DialogFooter>
            <Button variant="outline" size="sm" onClick={() => setSettingsOpen(false)}>Cancel</Button>
            <Button size="sm" onClick={handleSaveSettings}>Save & Reconnect</Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </header>
  )
}
