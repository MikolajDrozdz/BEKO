import type { UserInfo, UserCreate, UserUpdate } from '@/types/api'
import { authFetchWithToken } from './authClient'

export const usersApi = {
  list: (): Promise<UserInfo[]> =>
    authFetchWithToken<UserInfo[]>('/users/', { method: 'GET' }),

  get: (id: number): Promise<UserInfo> =>
    authFetchWithToken<UserInfo>(`/users/${id}`, { method: 'GET' }),

  create: (data: UserCreate): Promise<UserInfo> =>
    authFetchWithToken<UserInfo>('/users/', {
      method: 'POST',
      body: JSON.stringify(data),
    }),

  update: (id: number, data: UserUpdate): Promise<UserInfo> =>
    authFetchWithToken<UserInfo>(`/users/${id}`, {
      method: 'PUT',
      body: JSON.stringify(data),
    }),

  delete: (id: number): Promise<void> =>
    authFetchWithToken<void>(`/users/${id}`, { method: 'DELETE' }),
}
