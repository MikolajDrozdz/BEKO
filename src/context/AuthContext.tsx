import {
  createContext,
  useContext,
  useState,
  useEffect,
  useCallback,
  type ReactNode,
} from 'react'
import type { UserInfo, Capability } from '@/types/api'
import { authApi } from '@/lib/api/auth'

const TOKEN_KEY = 'beko_auth_token'
const USER_KEY = 'beko_auth_user'

interface AuthContextValue {
  user: UserInfo | null
  token: string | null
  isLoading: boolean
  login: (username: string, password: string) => Promise<void>
  logout: () => void
  hasPermission: (capability: Capability | string) => boolean
  isAdmin: boolean
}

const AuthContext = createContext<AuthContextValue | null>(null)

function isTokenExpired(token: string): boolean {
  try {
    const payload = JSON.parse(atob(token.split('.')[1].replace(/-/g, '+').replace(/_/g, '/')))
    return typeof payload.exp === 'number' && payload.exp * 1000 < Date.now()
  } catch {
    return true
  }
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<UserInfo | null>(null)
  const [token, setToken] = useState<string | null>(null)
  const [isLoading, setIsLoading] = useState(true)

  const clearAuth = useCallback(() => {
    localStorage.removeItem(TOKEN_KEY)
    localStorage.removeItem(USER_KEY)
    setToken(null)
    setUser(null)
  }, [])

  // Restore session on mount
  useEffect(() => {
    const storedToken = localStorage.getItem(TOKEN_KEY)
    const storedUser = localStorage.getItem(USER_KEY)

    if (!storedToken || isTokenExpired(storedToken)) {
      clearAuth()
      setIsLoading(false)
      return
    }

    setToken(storedToken)

    if (storedUser) {
      try {
        setUser(JSON.parse(storedUser) as UserInfo)
      } catch {
        // ignore malformed stored user
      }
    }

    // Validate token and refresh user data in background
    authApi.me().then((freshUser) => {
      setUser(freshUser)
      localStorage.setItem(USER_KEY, JSON.stringify(freshUser))
    }).catch(() => {
      clearAuth()
    }).finally(() => {
      setIsLoading(false)
    })
  }, [clearAuth])

  const login = useCallback(async (username: string, password: string) => {
    const result = await authApi.login(username, password)
    localStorage.setItem(TOKEN_KEY, result.access_token)
    localStorage.setItem(USER_KEY, JSON.stringify(result.user))
    setToken(result.access_token)
    setUser(result.user)
  }, [])

  const logout = useCallback(() => {
    clearAuth()
  }, [clearAuth])

  const hasPermission = useCallback(
    (capability: Capability | string): boolean => {
      if (!user) return false
      if (user.role === 'admin') return true
      return user.permissions.includes(capability as Capability)
    },
    [user],
  )

  const isAdmin = user?.role === 'admin'

  return (
    <AuthContext.Provider value={{ user, token, isLoading, login, logout, hasPermission, isAdmin }}>
      {children}
    </AuthContext.Provider>
  )
}

export function useAuth(): AuthContextValue {
  const ctx = useContext(AuthContext)
  if (!ctx) throw new Error('useAuth must be used within AuthProvider')
  return ctx
}
