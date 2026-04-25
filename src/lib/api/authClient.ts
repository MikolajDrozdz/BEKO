import type { ApiError } from '@/types/api'

const AUTH_URL_KEY = 'beko_auth_url'
const DEFAULT_AUTH_URL = 'http://localhost:8001'

export function getAuthBaseUrl(): string {
  return localStorage.getItem(AUTH_URL_KEY) ?? DEFAULT_AUTH_URL
}

export function setAuthBaseUrl(url: string): void {
  localStorage.setItem(AUTH_URL_KEY, url.replace(/\/$/, ''))
}

export class AuthApiError extends Error {
  public status: number
  public detail: string

  constructor(status: number, detail: string) {
    super(detail)
    this.name = 'AuthApiError'
    this.status = status
    this.detail = detail
  }
}

async function parseErrorDetail(res: Response): Promise<string> {
  try {
    const body = (await res.json()) as ApiError
    if (typeof body.detail === 'string') return body.detail
    if (Array.isArray(body.detail)) {
      return body.detail.map((e) => `${e.loc.join('.')}: ${e.msg}`).join('; ')
    }
    if (body.message) return body.message
  } catch {
    // ignore
  }
  return res.statusText || `HTTP ${res.status}`
}

export async function authFetch<T>(
  path: string,
  options?: RequestInit,
): Promise<T> {
  const base = getAuthBaseUrl()
  const url = `${base}${path}`

  const res = await fetch(url, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      Accept: 'application/json',
      ...(options?.headers ?? {}),
    },
  })

  if (!res.ok) {
    const detail = await parseErrorDetail(res)
    throw new AuthApiError(res.status, detail)
  }

  const text = await res.text()
  if (!text) return undefined as T
  try {
    return JSON.parse(text) as T
  } catch {
    return text as unknown as T
  }
}

function getStoredToken(): string | null {
  return localStorage.getItem('beko_auth_token')
}

export async function authFetchWithToken<T>(
  path: string,
  options?: RequestInit,
): Promise<T> {
  const token = getStoredToken()
  return authFetch<T>(path, {
    ...options,
    headers: {
      ...(options?.headers ?? {}),
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
  })
}
