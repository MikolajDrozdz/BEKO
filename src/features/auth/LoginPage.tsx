import { useState } from 'react'
import { useNavigate, useLocation } from 'react-router-dom'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { Radio, Eye, EyeOff, Settings2, Wifi } from 'lucide-react'
import { useAuth } from '@/context/AuthContext'
import { getAuthBaseUrl, setAuthBaseUrl } from '@/lib/api/authClient'
import { getBaseUrl, setBaseUrl } from '@/lib/api/client'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from '@/components/ui/dialog'

const schema = z.object({
  username: z.string().min(1, 'Username is required'),
  password: z.string().min(1, 'Password is required'),
})
type FormValues = z.infer<typeof schema>

export function LoginPage() {
  const { login } = useAuth()
  const navigate = useNavigate()
  const location = useLocation()
  const from = (location.state as { from?: { pathname: string } })?.from?.pathname ?? '/'

  const [showPassword, setShowPassword] = useState(false)
  const [loginError, setLoginError] = useState<string | null>(null)
  const [settingsOpen, setSettingsOpen] = useState(false)
  const [authUrlInput, setAuthUrlInput] = useState(getAuthBaseUrl())
  const [gatewayUrlInput, setGatewayUrlInput] = useState(getBaseUrl())

  const form = useForm<FormValues>({
    resolver: zodResolver(schema),
    defaultValues: { username: '', password: '' },
  })

  async function onSubmit(values: FormValues) {
    setLoginError(null)
    try {
      await login(values.username, values.password)
      navigate(from, { replace: true })
    } catch (err) {
      setLoginError(err instanceof Error ? err.message : 'Login failed')
    }
  }

  function handleSaveSettings() {
    setAuthBaseUrl(authUrlInput.trim())
    setBaseUrl(gatewayUrlInput.trim())
    setSettingsOpen(false)
  }

  return (
    <div className="min-h-screen flex items-center justify-center bg-teal-50 dark:bg-teal-950 p-4">
      <div className="w-full max-w-sm space-y-6">
        {/* Logo */}
        <div className="flex flex-col items-center gap-3">
          <div className="flex h-12 w-12 items-center justify-center rounded-xl bg-ivory-700 dark:bg-ivory-500 shadow-md">
            <Radio className="h-6 w-6 text-white dark:text-teal-950" />
          </div>
          <div className="text-center">
            <h1 className="text-xl font-semibold text-teal-900 dark:text-teal-50">BEKO Pager</h1>
            <p className="text-xs text-teal-500 dark:text-teal-400">Gateway Panel</p>
          </div>
        </div>

        {/* Login card */}
        <div className="rounded-xl border border-teal-200 dark:border-teal-800 bg-white dark:bg-teal-900 shadow-sm p-6 space-y-4">
          <h2 className="text-sm font-semibold text-teal-900 dark:text-teal-50">Sign in to continue</h2>

          <form onSubmit={form.handleSubmit(onSubmit)} className="space-y-3">
            <div className="space-y-1.5">
              <Label htmlFor="username">Username</Label>
              <Input
                id="username"
                autoComplete="username"
                autoFocus
                placeholder="admin"
                className="h-9"
                {...form.register('username')}
              />
              {form.formState.errors.username && (
                <p className="text-xs text-red-500">{form.formState.errors.username.message}</p>
              )}
            </div>

            <div className="space-y-1.5">
              <Label htmlFor="password">Password</Label>
              <div className="relative">
                <Input
                  id="password"
                  type={showPassword ? 'text' : 'password'}
                  autoComplete="current-password"
                  placeholder="••••••••"
                  className="h-9 pr-9"
                  {...form.register('password')}
                />
                <button
                  type="button"
                  onClick={() => setShowPassword((v) => !v)}
                  className="absolute right-2.5 top-1/2 -translate-y-1/2 text-teal-400 hover:text-teal-600 dark:hover:text-teal-300 transition-colors"
                  tabIndex={-1}
                >
                  {showPassword ? <EyeOff className="h-3.5 w-3.5" /> : <Eye className="h-3.5 w-3.5" />}
                </button>
              </div>
              {form.formState.errors.password && (
                <p className="text-xs text-red-500">{form.formState.errors.password.message}</p>
              )}
            </div>

            {loginError && (
              <div className="rounded-md bg-red-50 dark:bg-red-950/30 border border-red-200 dark:border-red-800 px-3 py-2">
                <p className="text-xs text-red-600 dark:text-red-400">{loginError}</p>
              </div>
            )}

            <Button
              type="submit"
              className="w-full"
              loading={form.formState.isSubmitting}
            >
              Sign in
            </Button>
          </form>
        </div>

        {/* Connection info */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-1.5 text-teal-400 dark:text-teal-600">
            <Wifi className="h-3 w-3" />
            <span className="text-[10px] font-mono-feature truncate max-w-[200px]">{getAuthBaseUrl()}</span>
          </div>
          <button
            type="button"
            onClick={() => setSettingsOpen(true)}
            className="flex items-center gap-1 text-[10px] text-teal-400 dark:text-teal-600 hover:text-teal-600 dark:hover:text-teal-400 transition-colors"
          >
            <Settings2 className="h-3 w-3" />
            Configure
          </button>
        </div>
      </div>

      {/* Settings dialog */}
      <Dialog open={settingsOpen} onOpenChange={setSettingsOpen}>
        <DialogContent className="max-w-sm">
          <DialogHeader>
            <DialogTitle>Connection Settings</DialogTitle>
          </DialogHeader>
          <div className="space-y-4">
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
                BEKO Auth Service (handles login and users)
              </p>
            </div>
            <div className="space-y-1.5">
              <Label htmlFor="gateway-url">Gateway API URL</Label>
              <Input
                id="gateway-url"
                value={gatewayUrlInput}
                onChange={(e) => setGatewayUrlInput(e.target.value)}
                placeholder="http://localhost:8000"
                className="font-mono-feature text-xs"
              />
              <p className="text-[10px] text-teal-500 dark:text-teal-400">
                BEKO LoRa Gateway API
              </p>
            </div>
          </div>
          <DialogFooter>
            <Button variant="outline" size="sm" onClick={() => setSettingsOpen(false)}>Cancel</Button>
            <Button size="sm" onClick={handleSaveSettings}>Save</Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  )
}
