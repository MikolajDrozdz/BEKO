import { useState } from 'react'
import { Moon, Sun, Menu, Settings2, Wifi } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { useTheme } from '@/context/ThemeContext'
import { getBaseUrl, setBaseUrl } from '@/lib/api/client'
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

export function Topbar({ onToggleSidebar }: TopbarProps) {
  const { theme, toggleTheme } = useTheme()
  const [settingsOpen, setSettingsOpen] = useState(false)
  const [urlInput, setUrlInput] = useState(getBaseUrl())

  function handleSaveSettings() {
    setBaseUrl(urlInput.trim())
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
        <div className="flex items-center gap-1.5 px-2 py-1 rounded-md bg-teal-50 dark:bg-teal-800 border border-teal-200 dark:border-teal-700 mr-2">
          <Wifi className="h-3 w-3 text-teal-500 dark:text-teal-400" />
          <span className="text-xs text-teal-600 dark:text-teal-300 font-mono-feature truncate max-w-[160px]">
            {getBaseUrl()}
          </span>
        </div>

        <Button variant="ghost" size="icon" onClick={() => setSettingsOpen(true)} title="API settings">
          <Settings2 className="h-4 w-4" />
        </Button>

        <Button variant="ghost" size="icon" onClick={toggleTheme} title="Toggle theme">
          {theme === 'dark' ? <Sun className="h-4 w-4" /> : <Moon className="h-4 w-4" />}
        </Button>
      </div>

      <Dialog open={settingsOpen} onOpenChange={setSettingsOpen}>
        <DialogContent className="max-w-sm">
          <DialogHeader>
            <DialogTitle>Gateway Connection</DialogTitle>
          </DialogHeader>
          <div className="space-y-3">
            <div className="space-y-1.5">
              <Label htmlFor="api-url">API Base URL</Label>
              <Input
                id="api-url"
                value={urlInput}
                onChange={(e) => setUrlInput(e.target.value)}
                placeholder="http://raspberrypi.local:8000"
                className="font-mono-feature"
              />
              <p className="text-xs text-teal-500 dark:text-teal-400">
                Point this to your BEKO Gateway host. Changes reload the page.
              </p>
            </div>
          </div>
          <DialogFooter>
            <Button variant="outline" size="sm" onClick={() => setSettingsOpen(false)}>
              Cancel
            </Button>
            <Button size="sm" onClick={handleSaveSettings}>
              Save & Reconnect
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </header>
  )
}
