import type { AcceptedResponse, FavoritePreset, PoliceSpeed, RemoteButton4Config, RemoteState, Snapshot } from './types'
import { serializeRgbState, serializeWhiteState } from './uiLogic'

export interface StatusResponse {
  firmware: string
  uptime_ms: number
  wifi_clients: number
  group: { state: string }
  left: Snapshot['left']
  right: Snapshot['right']
  observed: Snapshot['observed']
  remote: RemoteState
  system_health: Snapshot['system_health']
}

interface StateResponse {
  desired: Snapshot['desired']
  observed: Snapshot['observed']
}

interface PresetsResponse {
  favorite: FavoritePreset
  white_available: boolean
}

let devGeneration = 42
const devSide: Snapshot['left'] = { connected: true, ready: true, state: 'READY', rssi: -78,
  reconnect_count: 2, verified_generation: 42, forced_recoveries: 0 }
const devSnapshot: Snapshot = {
  firmware: '0.8.0', uptime_ms: 468000, wifi_clients: 2, group_state: 'SYNCED',
  desired: { valid: true, generation: 42, mode: 'rgb', r: 255, g: 0, b: 80, brightness: 184, white: 0 },
  left: devSide, right: { ...devSide, rssi: -82 },
  observed: { left: { valid: true, mode: 'rgb', r: 255, g: 0, b: 80, brightness: 184 },
    right: { valid: true, mode: 'rgb', r: 255, g: 0, b: 80, brightness: 184 } },
  favorite: { mode: 'rgb', r: 145, g: 28, b: 202, brightness: 160 }, white_available: true,
  remote: {
    connected: true, mapping_complete: true, discovery: false, vt: false, last_button: 3, last_channel: 'D1',
    last_event_ms: 460000, last_action_accepted: true,
    button4: { type: 'favorite', r: 255, g: 0, b: 0, brightness: 64 },
    mapping: { d0: 4, d1: 3, d2: 2, d3: 1 },
    police: { speed: 'fast', state: 'idle', elapsed_ms: 0, timed_out: false },
    event_drops: 0,
    indicator: { on: true, reason: 'CONFIRMED_SPECIAL', gpio_level: 1, last_change_ms: 450000 },
  },
  system_health: {
    healthy: true, reset_reason: 'POWER_ON', previous_recovery_reason: '', stale_component: '',
    supervisor_restarts: 0, free_heap: 118000, minimum_free_heap: 96000,
    heartbeats_ms: { connection_manager: 468000, group_runtime: 468000, rf_input: 468000, indicator: 468000, web_events: 468000 },
    counters: { ble_forced_recoveries: 0, ble_critical_event_replacements: 0, group_api_timeouts: 0,
      group_api_busy: 0, group_api_response_drops: 0, rf_event_drops: 0, websocket_event_drops: 0 },
    indicator: { gpio_level: 1, last_change_ms: 450000 },
  },
}

function developmentSnapshot(): Snapshot {
  const snapshot = structuredClone(devSnapshot)
  const scenario = new URLSearchParams(location.search).get('scenario')
  if (scenario === 'split') {
    snapshot.observed.left = { valid: true, power: true, mode: 'rgb', r: 255, g: 20, b: 20, brightness: 166 }
    snapshot.observed.right = { valid: true, power: true, mode: 'rgb', r: 0, g: 110, b: 255, brightness: 210 }
  } else if (scenario === 'white') {
    snapshot.observed.left = { valid: true, power: true, mode: 'white', white: 190 }
    snapshot.observed.right = { valid: true, power: true, mode: 'white', white: 190 }
  } else if (scenario === 'reconnect') {
    snapshot.observed.left = { valid: true, power: true, mode: 'rgb', r: 255, g: 20, b: 20, brightness: 166 }
    snapshot.observed.right = { valid: true, power: true, mode: 'rgb', r: 0, g: 110, b: 255, brightness: 210 }
    snapshot.right = { ...snapshot.right, connected: false, ready: false, state: 'BACKOFF' }
    snapshot.group_state = 'DEGRADED'
  }
  return snapshot
}

async function jsonRequest<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, init)
  const body = await response.json()
  if (!response.ok) throw new Error(body.message ?? `HTTP ${response.status}`)
  return body as T
}

export async function loadSnapshot(): Promise<Snapshot> {
  if (import.meta.env.DEV) return developmentSnapshot()
  const [status, state, presets] = await Promise.all([
    jsonRequest<StatusResponse>('/api/v1/status'),
    jsonRequest<StateResponse>('/api/v1/state'),
    jsonRequest<PresetsResponse>('/api/v1/presets'),
  ])
  return { ...status, group_state: status.group.state, desired: state.desired,
    observed: state.observed, favorite: presets.favorite,
    white_available: presets.white_available }
}

export async function loadStatus(): Promise<StatusResponse> {
  if (import.meta.env.DEV) return {
    firmware: devSnapshot.firmware,
    uptime_ms: devSnapshot.uptime_ms,
    wifi_clients: devSnapshot.wifi_clients,
    group: { state: devSnapshot.group_state },
    left: structuredClone(devSnapshot.left),
    right: structuredClone(devSnapshot.right),
    observed: structuredClone(devSnapshot.observed),
    remote: structuredClone(devSnapshot.remote),
    system_health: structuredClone(devSnapshot.system_health),
  }
  return jsonRequest<StatusResponse>('/api/v1/status')
}

const putJson = <T>(path: string, body: unknown) => jsonRequest<T>(path, {
  method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: typeof body === 'string' ? body : JSON.stringify(body),
})

export const setRgb = (r: number, g: number, b: number, brightness: number) =>
  import.meta.env.DEV ? Promise.resolve({ accepted: true, generation: ++devGeneration, group_state: 'RECONCILING' }) :
    putJson<AcceptedResponse>('/api/v1/state', serializeRgbState({ r, g, b }, brightness))

export const setWhite = (brightness: number) =>
  import.meta.env.DEV ? Promise.resolve({ accepted: true, generation: ++devGeneration, group_state: 'RECONCILING' }) :
    putJson<AcceptedResponse>('/api/v1/state', serializeWhiteState(brightness))

export const saveFavorite = (favorite: FavoritePreset) =>
  import.meta.env.DEV ? Promise.resolve({ favorite, white_available: true }) :
    putJson<PresetsResponse>('/api/v1/presets/favorite', favorite)

export const forceResync = () => import.meta.env.DEV ?
  Promise.resolve({ accepted: true, generation: devGeneration, group_state: 'RECONCILING' }) :
  jsonRequest<AcceptedResponse>('/api/v1/resync', { method: 'POST' })

export const saveRemoteButton4 = (config: RemoteButton4Config) => {
  if (import.meta.env.DEV) {
    devSnapshot.remote.button4 = structuredClone(config)
    return Promise.resolve(structuredClone(devSnapshot.remote))
  }
  return putJson<RemoteState>('/api/v1/remote/button4', config)
}

export const saveRemotePoliceSpeed = (speed: PoliceSpeed) => {
  if (import.meta.env.DEV) {
    devSnapshot.remote.police.speed = speed
    return Promise.resolve(structuredClone(devSnapshot.remote))
  }
  return putJson<RemoteState>('/api/v1/remote/police', { speed })
}

export const testIndicator = () => import.meta.env.DEV ? Promise.resolve({ accepted: true }) :
  jsonRequest<{ accepted: boolean }>('/api/v1/indicator/test', { method: 'POST' })
