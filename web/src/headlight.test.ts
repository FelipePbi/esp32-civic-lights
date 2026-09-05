import { describe, expect, it } from 'vitest'
import { mapBrightnessToVisualOpacity, mapObservedStateToHeadlightVisual } from './headlight'
import type { ObservedState, SideStatus } from './types'

const side = (overrides: Partial<SideStatus> = {}): SideStatus => ({
  connected: true,
  ready: true,
  state: 'READY',
  rssi: -80,
  reconnect_count: 0,
  verified_generation: 7,
  forced_recoveries: 0,
  ...overrides,
})

const observed = (overrides: Partial<ObservedState> = {}): ObservedState => ({
  valid: true,
  power: true,
  mode: 'rgb',
  r: 255,
  g: 20,
  b: 20,
  brightness: 128,
  ...overrides,
})

describe('HeadlightVisual mapping', () => {
  it('maps independent observed RGB colors', () => {
    const left = mapObservedStateToHeadlightVisual(side(), observed())
    const right = mapObservedStateToHeadlightVisual(side(), observed({ r: 0, g: 110, b: 255 }))
    expect(left.color).toBe('#FF1414')
    expect(right.color).toBe('#006EFF')
  })

  it('uses neutral cold white for real WHITE mode', () => {
    const visual = mapObservedStateToHeadlightVisual(side(), observed({ mode: 'white', white: 200 }))
    expect(visual.color).toBe('#F4F7FF')
    expect(visual.mode).toBe('white')
    expect(visual.illuminated).toBe(true)
  })

  it('maps brightness perceptually without hiding an active low level', () => {
    expect(mapBrightnessToVisualOpacity(0)).toBeCloseTo(0.2)
    expect(mapBrightnessToVisualOpacity(255)).toBe(1)
    expect(mapBrightnessToVisualOpacity(128)).toBeGreaterThan(0.59)
  })

  it('removes glow when offline, reconnecting, unknown, invalid or powered off', () => {
    const cases = [
      mapObservedStateToHeadlightVisual(side({ connected: false, ready: false, state: 'IDLE' }), observed()),
      mapObservedStateToHeadlightVisual(side({ connected: false, ready: false, state: 'BACKOFF' }), observed()),
      mapObservedStateToHeadlightVisual(side({ connected: false, ready: false, state: 'SCANNING' }), observed()),
      mapObservedStateToHeadlightVisual(side(), observed({ valid: false })),
      mapObservedStateToHeadlightVisual(side(), observed({ power: false })),
    ]
    for (const visual of cases) {
      expect(visual.illuminated).toBe(false)
      expect(visual.glowStrength).toBe(0)
    }
  })

  it('keeps healthy LEFT red while reconnecting RIGHT is dark', () => {
    const left = mapObservedStateToHeadlightVisual(side(), observed())
    const right = mapObservedStateToHeadlightVisual(
      side({ connected: false, ready: false, state: 'BACKOFF' }),
      observed(),
    )
    expect(left.color).toBe('#FF1414')
    expect(left.illuminated).toBe(true)
    expect(right.connectionState).toBe('reconnecting')
    expect(right.illuminated).toBe(false)
  })

  it('maps fast recovery lifecycle states to the intended transient visuals', () => {
    for (const state of ['FAST_RECOVERY', 'RECOVERING']) {
      expect(mapObservedStateToHeadlightVisual(
        side({ connected: false, ready: false, state }), observed(),
      ).connectionState).toBe('reconnecting')
    }
    expect(mapObservedStateToHeadlightVisual(
      side({ ready: false, state: 'SYNC_PENDING' }), observed(),
    ).connectionState).toBe('connecting')
  })
})
