export const INVALID_ADDRESS = 0x0000
export const GATEWAY_ID = 0x0001
export const MIN_NODE_ID = 0x0002
export const MAX_NODE_ID = 0xfffe
export const BROADCAST_ID = 0xffff
export const MAX_PAYLOAD_BYTES = 16

export type AddressKind = 'invalid' | 'gateway' | 'node' | 'broadcast' | 'out-of-range'

export function getAddressKind(id: number): AddressKind {
  if (!Number.isInteger(id) || id < INVALID_ADDRESS || id > BROADCAST_ID) return 'out-of-range'
  if (id === INVALID_ADDRESS) return 'invalid'
  if (id === GATEWAY_ID) return 'gateway'
  if (id === BROADCAST_ID) return 'broadcast'
  return 'node'
}

export function isNodeAddress(id: number): boolean {
  return id >= MIN_NODE_ID && id <= MAX_NODE_ID
}

export function isSendTargetAddress(id: number): boolean {
  return isNodeAddress(id) || id === BROADCAST_ID
}

export function getSendTargetError(id: number): string | undefined {
  const kind = getAddressKind(id)
  if (kind === 'invalid') return 'Adres 0x0000 jest nieważny.'
  if (kind === 'gateway') return 'Adres 0x0001 należy do gatewaya i nie jest targetem zwykłej wiadomości.'
  if (kind === 'out-of-range') return 'Adres musi mieścić się w zakresie 0x0000..0xFFFF.'
  return undefined
}

export function getUtf8ByteLength(text: string): number {
  return new TextEncoder().encode(text).length
}

export function isAsciiText(text: string): boolean {
  return /^[\x00-\x7F]*$/.test(text)
}

export function hasOnlyAsciiBytes(hex: string): boolean {
  const clean = hex.replace(/\s/g, '')
  const bytes = clean.match(/.{1,2}/g) ?? []
  return bytes.every((byte) => {
    const value = parseInt(byte, 16)
    return Number.isFinite(value) && value <= 0x7f
  })
}

export function getHexByteLength(hex: string): number {
  return hex.replace(/\s/g, '').length / 2
}

export function requiresNodeResponse(text: string): boolean {
  return /[.?!]$/.test(text.trimEnd())
}

export function isAskActType(type?: string): boolean {
  const normalized = type?.trim().toUpperCase()
  return normalized === 'ASK' || normalized === 'ACT'
}

export function isAskActText(text: string): boolean {
  return /^(ASK|ACT)(\b|[:\s_-])/i.test(text.trimStart())
}

export function getMessageStatusLabel(status?: string): string {
  switch (status) {
    case 'pending':
      return 'Oczekuje'
    case 'sent':
      return 'Wysłano'
    case 'failed':
      return 'Błąd wysyłki'
    case 'delivered':
      return 'Dostarczono'
    case 'sent_waiting_response':
      return 'Wysłano, oczekuje na ACK i odpowiedź'
    case 'delivered_waiting_response':
      return 'Dostarczono, oczekuje na odpowiedź'
    case 'answered':
      return 'Odpowiedziano'
    case 'received':
      return 'Odebrano'
    case 'response':
      return 'Odpowiedź noda'
    case 'ok':
      return 'OK'
    default:
      return status ?? '—'
  }
}
