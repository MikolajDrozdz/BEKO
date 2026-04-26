import { useState } from 'react'
import { useNavigate, useLocation } from 'react-router-dom'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { Radio, Eye, EyeOff, Wifi, Settings2 } from 'lucide-react'
import { useAuth } from '@/context/AuthContext'
import { getAuthBaseUrl, setAuthBaseUrl } from '@/lib/api/authClient'
import { getBaseUrl, setBaseUrl } from '@/lib/api/client'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
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
  const [gatewayUrlInput, setGatewayUrlInput] = useState(getBaseUrl())
  const [authUrlInput, setAuthUrlInput] = useState(getAuthBaseUrl())
  const [settingsError, setSettingsError] = useState<string | null>(null)

  const form = useForm<FormValues>({
    resolver: zodResolver(schema),
    defaultValues: { username: '', password: '' },
  })

  function normalizeHttpUrl(value: string): string {
    const trimmed = value.trim().replace(/\/$/, '')
    if (!trimmed) throw new Error('Both URLs are required')

    try {
      const parsed = new URL(trimmed)
      if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
        throw new Error('URL must start with http:// or https://')
      }
      return parsed.toString().replace(/\/$/, '')
    } catch {
      throw new Error('Enter valid http(s) URLs')
    }
  }

  function openSettings() {
    setGatewayUrlInput(getBaseUrl())
    setAuthUrlInput(getAuthBaseUrl())
    setSettingsError(null)
    setSettingsOpen(true)
  }

  function useCurrentHost() {
    if (typeof window === 'undefined') return
    const base = `${window.location.protocol}//${window.location.hostname}`
    setGatewayUrlInput(`${base}:8000`)
    setAuthUrlInput(`${base}:8001`)
    setSettingsError(null)
  }

  function saveSettings() {
    try {
      const gatewayUrl = normalizeHttpUrl(gatewayUrlInput)
      const authUrl = normalizeHttpUrl(authUrlInput)
      setBaseUrl(gatewayUrl)
      setAuthBaseUrl(authUrl)
      setGatewayUrlInput(gatewayUrl)
      setAuthUrlInput(authUrl)
      setSettingsError(null)
      setSettingsOpen(false)
    } catch (err) {
      setSettingsError(err instanceof Error ? err.message : 'Invalid connection settings')
    }
  }

  async function onSubmit(values: FormValues) {
    setLoginError(null)
    try {
      await login(values.username, values.password)
      navigate(from, { replace: true })
    } catch (err) {
      setLoginError(err instanceof Error ? err.message : 'Login failed')
    }
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
          <div className="flex min-w-0 flex-col gap-1 text-teal-400 dark:text-teal-600">
            <div className="flex items-center gap-1.5">
              <Wifi className="h-3 w-3 shrink-0" />
              <span className="text-[10px] font-mono-feature truncate max-w-[260px]">{getAuthBaseUrl()}</span>
            </div>
            <div className="flex items-center gap-1.5">
              <Wifi className="h-3 w-3" />
              <span className="text-[10px] font-mono-feature truncate max-w-[260px]">{getBaseUrl()}</span>
            </div>
          </div>
          <Button type="button" variant="ghost" size="icon" onClick={openSettings} title="Connection settings">
            <Settings2 className="h-4 w-4" />
          </Button>
        </div>

        <Dialog open={settingsOpen} onOpenChange={setSettingsOpen}>
          <DialogContent className="max-w-sm">
            <DialogHeader>
              <DialogTitle>Connection Settings</DialogTitle>
            </DialogHeader>
            <div className="space-y-4">
              <div className="space-y-1.5">
                <Label htmlFor="login-auth-url">Auth Service URL</Label>
                <Input
                  id="login-auth-url"
                  value={authUrlInput}
                  onChange={(e) => setAuthUrlInput(e.target.value)}
                  placeholder="http://raspberrypi.local:8001"
                  className="font-mono-feature text-xs"
                />
              </div>
              <div className="space-y-1.5">
                <Label htmlFor="login-gateway-url">Gateway API URL</Label>
                <Input
                  id="login-gateway-url"
                  value={gatewayUrlInput}
                  onChange={(e) => setGatewayUrlInput(e.target.value)}
                  placeholder="http://raspberrypi.local:8000"
                  className="font-mono-feature text-xs"
                />
              </div>
              {settingsError && (
                <div className="rounded-md bg-red-50 dark:bg-red-950/30 border border-red-200 dark:border-red-800 px-3 py-2">
                  <p className="text-xs text-red-600 dark:text-red-400">{settingsError}</p>
                </div>
              )}
            </div>
            <DialogFooter>
              <Button type="button" variant="outline" size="sm" onClick={useCurrentHost}>
                Use current host
              </Button>
              <Button type="button" size="sm" onClick={saveSettings}>
                Save
              </Button>
            </DialogFooter>
          </DialogContent>
        </Dialog>
      </div>
    </div>
  )
}
