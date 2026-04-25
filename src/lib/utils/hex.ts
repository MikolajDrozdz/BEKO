export function textToHex(text: string): string {
  return Array.from(new TextEncoder().encode(text))
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('')
}

export function hexToText(hex: string): string {
  try {
    const clean = hex.replace(/\s/g, '')
    const bytes = new Uint8Array(
      clean.match(/.{1,2}/g)?.map((b) => parseInt(b, 16)) ?? [],
    )
    return new TextDecoder().decode(bytes)
  } catch {
    return ''
  }
}

export function isValidHex(hex: string): boolean {
  const clean = hex.replace(/\s/g, '')
  return clean.length % 2 === 0 && /^[0-9a-fA-F]*$/.test(clean)
}

export function formatHex(hex: string): string {
  return hex.toLowerCase().replace(/\s/g, '')
}

export { BROADCAST_ID } from './protocol'
