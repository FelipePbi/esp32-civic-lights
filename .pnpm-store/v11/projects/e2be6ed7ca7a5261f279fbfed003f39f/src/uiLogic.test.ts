import { describe, expect, it } from 'vitest'
import {
  brightnessToPercent,
  hexToRgb,
  isFavoritePreset,
  isRgb,
  parseWebSocketEvent,
  observedLightPresentation,
  percentToBrightness,
  serializeRgbState,
  serializeWhiteState,
  sideStatusLabel,
  throttleDelay,
} from './uiLogic'
import type { SideStatus, Snapshot } from './types'

const side = (overrides: Partial<SideStatus> = {}): SideStatus => ({
  connected: false,
  ready: false,
  state: 'IDLE',
  rssi: -90,
  reconnect_count: 0,
  verified_generation: 0,
  forced_recoveries: 0,
  ...overrides,
})

const snapshot: Snapshot = {
  type: 'snapshot',
  firmware: '0.7.6',
  uptime_ms: 1,
  wifi_clients: 1,
  group_state: 'SYNCED',
  desired: { valid: true, generation: 7, mode: 'rgb', r: 1, g: 2, b: 3, brightness: 4, white: 0 },
  left: side({ connected: true, ready: true, state: 'READY', verified_generation: 7 }),
  right: side({ connected: true, ready: true, state: 'READY', verified_generation: 7 }),
  observed: {
    left: { valid: true, mode: 'rgb', r: 1, g: 2, b: 3, brightness: 4 },
    right: { valid: true, mode: 'rgb', r: 1, g: 2, b: 3, brightness: 4 },
  },
  favorite: { mode: 'rgb', r: 4, g: 5, b: 6, brightness: 7 },
  white_available: true,
  remote: {
    connected: true, mapping_complete: true, discovery: false, vt: false, last_button: 2,
    last_channel: 'D1', last_event_ms: 1, last_action_accepted: true,
    button4: { type: 'favorite', r: 255, g: 0, b: 0, brightness: 64 },
    mapping: { d0: 1, d1: 2, d2: 3, d3: 4 },
    police: { speed: 'fast', state: 'idle', elapsed_ms: 0, timed_out: false },
    event_drops: 0,
    indicator: { on: false, reason: 'CONFIRMED_WHITE', gpio_level: 0, last_change_ms: 0 },
  },
  system_health: {
    healthy: true, reset_reason: 'POWER_ON', previous_recovery_reason: '', stale_component: '',
    supervisor_restarts: 0, free_heap: 100000, minimum_free_heap: 90000,
    heartbeats_ms: {},
    counters: { ble_forced_recoveries: 0, ble_critical_event_replacements: 0,
      group_api_timeouts: 0, group_api_busy: 0, group_api_response_drops: 0,
      rf_event_drops: 0, websocket_event_drops: 0 },
    indicator: { gpio_level: 0, last_change_ms: 0 },
  },
}

describe('color and brightness conversion', () => {
  it('validates integer RGB channels from 0 to 255', () => {
    expect(isRgb({ r: 0, g: 128, b: 255 })).toBe(true)
    expect(isRgb({ r: -1, g: 0, b: 0 })).toBe(false)
    expect(isRgb({ r: 1.5, g: 0, b: 0 })).toBe(false)
    expect(isRgb({ r: 0, g: 0, b: 256 })).toBe(false)
  })

  it('converts valid hexadecimal RGB and rejects malformed input', () => {
    expect(hexToRgb('#ff0080')).toEqual({ r: 255, g: 0, b: 128 })
    expect(hexToRgb('00Aa10')).toEqual({ r: 0, g: 170, b: 16 })
    expect(hexToRgb('#xyz')).toBeNull()
  })

  it('maps byte brightness to percent and clamps the inverse', () => {
    expect(brightnessToPercent(0)).toBe(0)
    expect(brightnessToPercent(255)).toBe(100)
    expect(percentToBrightness(50)).toBe(128)
    expect(percentToBrightness(200)).toBe(255)
  })
})

describe('API payload serialization', () => {
  it('serializes exact RGB and WHITE request shapes', () => {
    expect(JSON.parse(serializeRgbState({ r: 9, g: 8, b: 7 }, 6))).toEqual({ mode: 'rgb', r: 9, g: 8, b: 7, brightness: 6 })
    expect(JSON.parse(serializeWhiteState(128))).toEqual({ mode: 'white', brightness: 128 })
  })
})

describe('WebSocket and presentation logic', () => {
  it('accepts a complete snapshot and rejects malformed frames', () => {
    expect(parseWebSocketEvent(JSON.stringify(snapshot))).toEqual(snapshot)
    expect(parseWebSocketEvent('{bad')).toBeNull()
    expect(parseWebSocketEvent(JSON.stringify({ type: 'snapshot' }))).toBeNull()
  })

  it('maps side lifecycle states to user-facing labels', () => {
    expect(sideStatusLabel(side({ ready: true }))).toBe('Conectado')
    expect(sideStatusLabel(side({ state: 'BACKOFF' }))).toBe('Reconectando')
    expect(sideStatusLabel(side({ state: 'FAST_RECOVERY' }))).toBe('Reconectando')
    expect(sideStatusLabel(side({ state: 'RECOVERING' }))).toBe('Reconectando')
    expect(sideStatusLabel(side({ state: 'SYNC_PENDING', connected: true }))).toBe('Verificando')
    expect(sideStatusLabel(side({ state: 'CONNECTING' }))).toBe('Conectando')
    expect(sideStatusLabel(side({ connected: true, state: 'QUERYING_STATE' }))).toBe('Conectando')
    expect(sideStatusLabel(side({ state: 'ERROR' }))).toBe('Erro')
    expect(sideStatusLabel(side())).toBe('Offline')
  })

  it('presents actual observed RGB and white state per side', () => {
    expect(observedLightPresentation({ valid: true, power: true, mode: 'rgb', r: 255, g: 9, b: 222, brightness: 128 }))
      .toEqual({ label: '#FF09DE', detail: 'RGB 255 / 9 / 222 · 50%', color: '#FF09DE', active: true })
    expect(observedLightPresentation({ valid: true, power: true, mode: 'white', white: 155 }))
      .toEqual({ label: 'Branco real', detail: 'CANAL W · 61%', color: '#FFF8E7', active: true })
    expect(observedLightPresentation({ valid: true, power: false, mode: 'rgb' }).label).toBe('Desligado')
    expect(observedLightPresentation({ valid: false }).label).toBe('Sem leitura')
  })
})

describe('interaction guards', () => {
  it('throttles intermediate events but sends final events immediately', () => {
    expect(throttleDelay(1000, 1040, 100, false)).toBe(60)
    expect(throttleDelay(1000, 1100, 100, false)).toBe(0)
    expect(throttleDelay(1000, 1001, 100, true)).toBe(0)
  })

  it('validates persisted favorite presets', () => {
    expect(isFavoritePreset(snapshot.favorite)).toBe(true)
    expect(isFavoritePreset({ ...snapshot.favorite, brightness: 256 })).toBe(false)
    expect(isFavoritePreset({ ...snapshot.favorite, mode: 'white' })).toBe(false)
  })
})
