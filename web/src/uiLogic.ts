import type { FavoritePreset, ObservedState, SideStatus, Snapshot } from './types'
import { rgbToHex } from './color'
import type { RGB } from './color'

export const isByte = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 255

export const isRgb = (value: unknown): value is RGB => {
  if (!value || typeof value !== 'object') return false
  const rgb = value as Partial<RGB>
  return isByte(rgb.r) && isByte(rgb.g) && isByte(rgb.b)
}

export function hexToRgb(hex: string): RGB | null {
  const match = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex)
  return match ? { r: parseInt(match[1], 16), g: parseInt(match[2], 16), b: parseInt(match[3], 16) } : null
}

export const brightnessToPercent = (value: number) => Math.round(value / 255 * 100)
export const percentToBrightness = (percent: number) => Math.round(Math.max(0, Math.min(100, percent)) / 100 * 255)

export const serializeRgbState = (color: RGB, brightness: number) =>
  JSON.stringify({ mode: 'rgb', ...color, brightness })
export const serializeWhiteState = (brightness: number) =>
  JSON.stringify({ mode: 'white', brightness })

export function parseWebSocketEvent(text: string): Snapshot | null {
  try {
    const value = JSON.parse(text) as Partial<Snapshot>
    return typeof value.type === 'string' && typeof value.firmware === 'string' &&
      typeof value.group_state === 'string' && !!value.desired && !!value.left &&
      !!value.right && !!value.observed && !!value.favorite &&
      !!value.remote ? value as Snapshot : null
  } catch { return null }
}

export const sideStatusLabel = (side: SideStatus) => {
  if (side.ready) return 'Conectado'
  if (side.state === 'BACKOFF' || side.state === 'WAITING_FOR_ADV' ||
      side.state === 'FAST_RECOVERY' || side.state === 'RECOVERING') return 'Reconectando'
  if (side.state === 'CONNECTING' || side.state === 'CONNECTED' || side.state === 'DISCOVERING' ||
      side.state === 'SUBSCRIBING' || side.state === 'QUERYING_STATE' || side.state === 'RECONCILING') return 'Conectando'
  if (side.state === 'SYNC_PENDING') return 'Verificando'
  if (side.state === 'ERROR') return 'Erro'
  return side.connected ? 'Conectado' : 'Offline'
}

export const observedLightPresentation = (observed: ObservedState) => {
  if (!observed.valid) return { label: 'Sem leitura', detail: 'Aguardando estado', color: '#2B2E33', active: false }
  if (observed.power === false) return { label: 'Desligado', detail: 'Saída sem luz', color: '#090A0C', active: false }
  if (observed.mode === 'white') {
    const level = observed.white ?? 0
    return { label: 'Branco real', detail: `CANAL W · ${brightnessToPercent(level)}%`, color: '#FFF8E7', active: true }
  }
  const rgb = { r: observed.r ?? 0, g: observed.g ?? 0, b: observed.b ?? 0 }
  return {
    label: rgbToHex(rgb),
    detail: `RGB ${rgb.r} / ${rgb.g} / ${rgb.b} · ${brightnessToPercent(observed.brightness ?? 0)}%`,
    color: rgbToHex(rgb),
    active: true,
  }
}

export const throttleDelay = (lastSent: number, now: number, interval: number, final: boolean) =>
  final || now - lastSent >= interval ? 0 : interval - (now - lastSent)

export const isFavoritePreset = (value: unknown): value is FavoritePreset => {
  if (!value || typeof value !== 'object') return false
  const preset = value as Partial<FavoritePreset>
  const brightness = preset.brightness
  return preset.mode === 'rgb' && isRgb(preset) && isByte(brightness)
}
