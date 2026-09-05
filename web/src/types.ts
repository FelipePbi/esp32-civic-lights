export type LightMode = 'rgb' | 'white'

export interface DesiredState {
  valid: boolean
  generation: number
  mode: LightMode
  r: number
  g: number
  b: number
  brightness: number
  white: number
}

export interface SideStatus {
  connected: boolean
  ready: boolean
  state: string
  rssi: number
  reconnect_count: number
  verified_generation: number
  forced_recoveries: number
  last_disconnect_ms?: number
  last_disconnect_reason?: number
  disconnect_classification?: string
  fast_recovery?: 'IDLE' | 'ACTIVE' | 'PASS' | 'FAILED'
  last_recovery_ms?: number
  last_adv_after_loss_ms?: number
  supervision_timeout_requested_ms?: number
  supervision_timeout_accepted_ms?: number
  power_cycle_suspected_count?: number
  connection_0x3e_count?: number
  avg_adv_to_ready_ms?: number
  max_adv_to_ready_ms?: number
}

export interface ObservedState {
  valid: boolean
  power?: boolean
  mode?: LightMode
  r?: number
  g?: number
  b?: number
  brightness?: number
  white?: number
}

export interface FavoritePreset {
  mode: 'rgb'
  r: number
  g: number
  b: number
  brightness: number
}

export type RemoteActionType = 'favorite' | 'rgb' | 'white' | 'police'
export type PoliceSpeed = 'slow' | 'normal' | 'fast' | 'very_fast'

export interface RemoteButton4Config {
  type: RemoteActionType
  r: number
  g: number
  b: number
  brightness: number
}

export interface RemoteState {
  connected: boolean
  mapping_complete: boolean
  discovery: boolean
  vt: boolean
  last_button: number | null
  last_channel: string | null
  last_event_ms: number | null
  last_action_accepted: boolean | null
  button4: RemoteButton4Config
  mapping: { d0: number | null; d1: number | null; d2: number | null; d3: number | null }
  police: { speed: PoliceSpeed; state: string; elapsed_ms: number; timed_out: boolean }
  event_drops: number
  indicator: { on: boolean; reason: string; gpio_level: number; last_change_ms: number }
}

export interface SystemHealth {
  healthy: boolean
  reset_reason: string
  previous_recovery_reason: string
  stale_component: string
  supervisor_restarts: number
  free_heap: number
  minimum_free_heap: number
  heartbeats_ms: Record<string, number>
  counters: {
    ble_forced_recoveries: number
    ble_critical_event_replacements: number
    group_api_timeouts: number
    group_api_busy: number
    group_api_response_drops: number
    rf_event_drops: number
    websocket_event_drops: number
  }
  indicator: { gpio_level: number; last_change_ms: number }
}

export interface Snapshot {
  type?: string
  firmware: string
  uptime_ms: number
  wifi_clients: number
  group_state: string
  desired: DesiredState
  left: SideStatus
  right: SideStatus
  observed: { left: ObservedState; right: ObservedState }
  favorite: FavoritePreset
  white_available: boolean
  remote: RemoteState
  system_health: SystemHealth
}

export interface AcceptedResponse {
  accepted: boolean
  generation: number
  group_state: string
}
