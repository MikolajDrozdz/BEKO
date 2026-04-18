import type { ApiError } from '@/types/api'

const CONFIG_KEY = 'beko_api_base_url'
const DEFAULT_BASE_URL = 'http://localhost:8000'

export function getBaseUrl(): string {
  return localStorage.getItem(CONFIG_KEY) ?? DEFAULT_BASE_URL
}

export function setBaseUrl(url: string): void {
  localStorage.setItem(CONFIG_KEY, url.replace(/\/$/, ''))
}

export class GatewayApiError extends Error {
  public status: number
  public detail: string

  constructor(status: number, detail: string) {
    super(detail)
    this.name = 'GatewayApiError'
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
    // ignore parse errors
  }
  return res.statusText || `HTTP ${res.status}`
}

export async function apiFetch<T>(
  path: string,
  options?: RequestInit & { signal?: AbortSignal },
): Promise<T> {
  const base = getBaseUrl()
  const url = `${base}${path}`

  const res = await fetch(url, {
    headers: {
      'Content-Type': 'application/json',
      Accept: 'application/json',
      ...(options?.headers ?? {}),
    },
    ...options,
  })

  if (!res.ok) {
    const detail = await parseErrorDetail(res)
    throw new GatewayApiError(res.status, detail)
  }

  const text = await res.text()
  if (!text) return undefined as T
  try {
    return JSON.parse(text) as T
  } catch {
    return text as unknown as T
  }
}

export function apiGet<T>(path: string, signal?: AbortSignal): Promise<T> {
  return apiFetch<T>(path, { method: 'GET', signal })
}

export function apiPost<T>(path: string, body?: unknown, signal?: AbortSignal): Promise<T> {
  return apiFetch<T>(path, {
    method: 'POST',
    body: body !== undefined ? JSON.stringify(body) : undefined,
    signal,
  })
}

export function apiDelete<T>(path: string, signal?: AbortSignal): Promise<T> {
  return apiFetch<T>(path, { method: 'DELETE', signal })
}
