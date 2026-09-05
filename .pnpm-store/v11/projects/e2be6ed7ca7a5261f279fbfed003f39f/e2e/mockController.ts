import type { Page } from '@playwright/test'

const side = (rssi: number) => ({
  connected: true,
  ready: true,
  state: 'READY',
  rssi,
  reconnect_count: 2,
  verified_generation: 42,
  forced_recoveries: 0,
})

const baseSnapshot = () => ({
  type: 'snapshot',
  firmware: '0.8.0',
  uptime_ms: 468000,
  wifi_clients: 2,
  group_state: 'SYNCED',
  desired: { valid: true, generation: 42, mode: 'rgb', r: 255, g: 0, b: 80, brightness: 184, white: 0 },
  left: side(-78),
  right: side(-82),
  observed: {
    left: { valid: true, power: true, mode: 'rgb', r: 255, g: 0, b: 80, brightness: 184 },
    right: { valid: true, power: true, mode: 'rgb', r: 255, g: 0, b: 80, brightness: 184 },
  },
  favorite: { mode: 'rgb', r: 145, g: 28, b: 202, brightness: 160 },
  white_available: true,
  remote: {
    connected: true,
    mapping_complete: true,
    discovery: false,
    vt: false,
    last_button: 3,
    last_channel: 'D1',
    last_event_ms: 460000,
    last_action_accepted: true,
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
})

export async function installControllerMock(page: Page) {
  const snapshot = baseSnapshot()
  let initialized = false
  let pushSnapshot: (() => void) | null = null

  const initializeScenario = () => {
    if (initialized) return
    initialized = true
    const scenario = new URL(page.url()).searchParams.get('scenario')
    if (scenario === 'split') {
      snapshot.observed.left = { valid: true, power: true, mode: 'rgb', r: 255, g: 20, b: 20, brightness: 166 }
      snapshot.observed.right = { valid: true, power: true, mode: 'rgb', r: 0, g: 110, b: 255, brightness: 210 }
    } else if (scenario === 'white') {
      snapshot.observed.left = { valid: true, power: true, mode: 'white', r: 0, g: 0, b: 0, brightness: 0, white: 190 }
      snapshot.observed.right = { valid: true, power: true, mode: 'white', r: 0, g: 0, b: 0, brightness: 0, white: 190 }
    } else if (scenario === 'reconnect') {
      snapshot.observed.left = { valid: true, power: true, mode: 'rgb', r: 255, g: 20, b: 20, brightness: 166 }
      snapshot.observed.right = { valid: true, power: true, mode: 'rgb', r: 0, g: 110, b: 255, brightness: 210 }
      snapshot.right = { ...snapshot.right, connected: false, ready: false, state: 'BACKOFF' }
      snapshot.group_state = 'DEGRADED'
    } else if (scenario === 'unknown') {
      snapshot.observed.left = { valid: false, power: false, mode: 'rgb', r: 0, g: 0, b: 0, brightness: 0 }
      snapshot.observed.right = { valid: false, power: false, mode: 'rgb', r: 0, g: 0, b: 0, brightness: 0 }
    } else if (scenario === 'offline') {
      snapshot.left = { ...snapshot.left, connected: false, ready: false, state: 'DISCONNECTED' }
      snapshot.right = { ...snapshot.right, connected: false, ready: false, state: 'DISCONNECTED' }
      snapshot.group_state = 'DEGRADED'
    }
  }

  await page.routeWebSocket('**/ws', socket => {
    initializeScenario()
    pushSnapshot = () => socket.send(JSON.stringify(snapshot))
    pushSnapshot()
  })

  await page.route('**/api/v1/**', async route => {
    initializeScenario()
    const request = route.request()
    const path = new URL(request.url()).pathname
    const method = request.method()

    if (path === '/api/v1/status' && method === 'GET') {
      return route.fulfill({ json: {
        firmware: snapshot.firmware,
        uptime_ms: snapshot.uptime_ms,
        wifi_clients: snapshot.wifi_clients,
        capabilities: { rgb: true, white: snapshot.white_available },
        group: { state: snapshot.group_state, generation: snapshot.desired.generation, controller_started: true },
        left: snapshot.left,
        right: snapshot.right,
        observed: snapshot.observed,
        remote: snapshot.remote,
        system_health: snapshot.system_health,
      } })
    }
    if (path === '/api/v1/state' && method === 'GET') {
      return route.fulfill({ json: {
        desired: snapshot.desired,
        observed: snapshot.observed,
        verified_generation: { left: snapshot.left.verified_generation, right: snapshot.right.verified_generation },
      } })
    }
    if (path === '/api/v1/state' && method === 'PUT') {
      const body = request.postDataJSON() as { mode: 'rgb' | 'white'; r?: number; g?: number; b?: number; brightness: number }
      const generation = ++snapshot.desired.generation
      snapshot.desired = body.mode === 'white'
        ? { ...snapshot.desired, valid: true, generation, mode: 'white', brightness: 0, white: body.brightness }
        : { valid: true, generation, mode: 'rgb', r: body.r ?? 0, g: body.g ?? 0, b: body.b ?? 0, brightness: body.brightness, white: 0 }
      const observed = body.mode === 'white'
        ? { valid: true, power: true, mode: 'white', r: 0, g: 0, b: 0, brightness: 0, white: body.brightness }
        : { valid: true, power: true, mode: 'rgb', r: body.r ?? 0, g: body.g ?? 0, b: body.b ?? 0, brightness: body.brightness }
      snapshot.observed = { left: { ...observed }, right: { ...observed } }
      snapshot.left.verified_generation = generation
      snapshot.right.verified_generation = generation
      snapshot.group_state = 'SYNCED'
      await route.fulfill({ status: 202, json: { accepted: true, generation, group_state: 'RECONCILING' } })
      const parameters = new URL(page.url()).searchParams
      if (parameters.has('supersede')) setTimeout(() => {
        const externalGeneration = ++snapshot.desired.generation
        snapshot.desired = { valid: true, generation: externalGeneration, mode: 'rgb', r: 0, g: 110, b: 255, brightness: 190, white: 0 }
        snapshot.observed = {
          left: { valid: true, power: true, mode: 'rgb', r: 0, g: 110, b: 255, brightness: 190 },
          right: { valid: true, power: true, mode: 'rgb', r: 0, g: 110, b: 255, brightness: 190 },
        }
        snapshot.left.verified_generation = externalGeneration
        snapshot.right.verified_generation = externalGeneration
        pushSnapshot?.()
      }, 25)
      else if (!parameters.has('hold')) setTimeout(() => pushSnapshot?.(), 25)
      return
    }
    if (path === '/api/v1/presets' && method === 'GET') {
      return route.fulfill({ json: { favorite: snapshot.favorite, white_available: snapshot.white_available } })
    }
    if (path === '/api/v1/presets/favorite' && method === 'PUT') {
      snapshot.favorite = request.postDataJSON() as typeof snapshot.favorite
      return route.fulfill({ json: { favorite: snapshot.favorite, white_available: true } })
    }
    if (path === '/api/v1/resync' && method === 'POST') {
      snapshot.group_state = 'RECONCILING'
      await route.fulfill({ status: 202, json: { accepted: true, generation: snapshot.desired.generation, group_state: 'RECONCILING' } })
      if (!new URL(page.url()).searchParams.has('hold')) setTimeout(() => {
        snapshot.group_state = 'SYNCED'
        pushSnapshot?.()
      }, 25)
      return
    }
    if (path === '/api/v1/remote/button4' && method === 'PUT') {
      snapshot.remote.button4 = request.postDataJSON() as typeof snapshot.remote.button4
      await route.fulfill({ json: snapshot.remote })
      setTimeout(() => pushSnapshot?.(), 10)
      return
    }
    if (path === '/api/v1/remote/police' && method === 'PUT') {
      snapshot.remote.police.speed = request.postDataJSON().speed
      await route.fulfill({ json: snapshot.remote })
      setTimeout(() => pushSnapshot?.(), 10)
      return
    }
    if (path === '/api/v1/indicator/test' && method === 'POST') {
      return route.fulfill({ status: 202, json: { accepted: true } })
    }
    return route.fulfill({ status: 404, json: { message: 'Not found' } })
  })
}
