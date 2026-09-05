import { rgbToHex } from './color'
import type { ObservedState, SideStatus } from './types'

export type HeadlightConnectionState =
  | 'unknown'
  | 'connecting'
  | 'ready'
  | 'reconnecting'
  | 'offline'
  | 'error'

export interface HeadlightVisualState {
  connectionState: HeadlightConnectionState
  mode: 'rgb' | 'white'
  color: string
  brightness: number
  visualOpacity: number
  glowStrength: number
  illuminated: boolean
}

const RECONNECTING_STATES = new Set(['BACKOFF', 'WAITING_FOR_ADV', 'FAST_RECOVERY', 'RECOVERING'])
const CONNECTING_STATES = new Set(['CONNECTING', 'CONNECTED', 'DISCOVERING', 'SUBSCRIBING', 'QUERYING_STATE', 'SYNC_PENDING', 'RECONCILING'])

export function connectionStateForSide(side: SideStatus): HeadlightConnectionState {
  if (side.state === 'ERROR') return 'error'
  if (RECONNECTING_STATES.has(side.state)) return 'reconnecting'
  if (side.ready) return 'ready'
  if (CONNECTING_STATES.has(side.state) || side.connected) return 'connecting'
  if (side.state === 'IDLE' || side.state === 'DISCONNECTED') return 'offline'
  return 'unknown'
}

export function mapBrightnessToVisualOpacity(brightness: number, illuminated = true) {
  if (!illuminated) return 0
  const normalized = Math.max(0, Math.min(255, brightness)) / 255
  return 0.2 + normalized * 0.8
}

export function mapObservedStateToHeadlightVisual(
  side: SideStatus,
  observed: ObservedState,
): HeadlightVisualState {
  const connectionState = connectionStateForSide(side)
  const verified = connectionState === 'ready' && observed.valid && observed.power !== false
  const mode = observed.mode === 'white' ? 'white' : 'rgb'
  const color = mode === 'white'
    ? '#F4F7FF'
    : rgbToHex({ r: observed.r ?? 0, g: observed.g ?? 0, b: observed.b ?? 0 })
  const brightness = mode === 'white'
    ? observed.white ?? observed.brightness ?? 0
    : observed.brightness ?? 0
  const visualOpacity = mapBrightnessToVisualOpacity(brightness, verified)

  return {
    connectionState,
    mode,
    color: verified ? color : '#73777F',
    brightness,
    visualOpacity,
    glowStrength: verified ? 0.18 + Math.max(0, Math.min(255, brightness)) / 255 * 0.82 : 0,
    illuminated: verified,
  }
}
