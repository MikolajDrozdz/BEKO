import type { AuthToken, UserInfo } from '@/types/api'
import { authFetch, authFetchWithToken } from './authClient'

export const authApi = {
  login: (username: string, password: string): Promise<AuthToken> => {
    const body = new URLSearchParams({ username, password })
    return authFetch<AuthToken>('/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body.toString(),
    })
  },

  me: (): Promise<UserInfo> =>
    authFetchWithToken<UserInfo>('/auth/me', { method: 'GET' }),
}
