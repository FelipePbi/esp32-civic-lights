import process from 'node:process'

const baseUrl = process.env.CIVIC_BASE_URL ?? 'http://192.168.4.1'
const expectedFirmware = process.env.CIVIC_EXPECT_FIRMWARE ?? '0.8.0'
const rgbCountArg = process.argv.find(value => value.startsWith('--rgb-count='))
const rgbCount = rgbCountArg ? Number(rgbCountArg.split('=')[1]) : 0
const wsUrl = baseUrl.replace(/^http/, 'ws') + '/ws'

const check = (condition, message) => {
  if (!condition) throw new Error(message)
}

function checkObserved(observed, label) {
  check(observed && typeof observed.valid === 'boolean', `${label}: missing observed.valid`)
  if (!observed.valid) return
  check(observed.mode === 'rgb' || observed.mode === 'white', `${label}: invalid observed.mode`)
  if (observed.mode === 'white') check(Number.isInteger(observed.white), `${label}: missing observed.white`)
  else {
    check(Number.isInteger(observed.r) && Number.isInteger(observed.g) && Number.isInteger(observed.b),
      `${label}: missing observed RGB`)
    check(Number.isInteger(observed.brightness), `${label}: missing observed.brightness`)
  }
}

async function request(path, options = {}, expected = 200) {
  let response
  try {
    response = await fetch(baseUrl + path, { signal: AbortSignal.timeout(5000), ...options })
  } catch (error) {
    const detail = error instanceof Error ? `${error.name}: ${error.message}` : String(error)
    throw new Error(`${path}: request failed (${detail})`, { cause: error })
  }
  check(response.status === expected, `${path}: expected HTTP ${expected}, got ${response.status}`)
  const type = response.headers.get('content-type') ?? ''
  return type.includes('json') ? response.json() : response.text()
}

function openSocket() {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(wsUrl)
    const timer = setTimeout(() => { socket.close(); reject(new Error('WebSocket snapshot timeout')) }, 5000)
    socket.addEventListener('error', () => { clearTimeout(timer); reject(new Error('WebSocket connection failed')) }, { once: true })
    socket.addEventListener('message', event => {
      try {
        const value = JSON.parse(event.data)
        if (value.type === 'snapshot') { clearTimeout(timer); resolve({ socket, snapshot: value }) }
      } catch { /* Continue until a valid snapshot or timeout. */ }
    })
  })
}

function waitForSync(socket, generation) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      socket.removeEventListener('message', onMessage)
      reject(new Error(`generation ${generation}: SYNCED timeout`))
    }, 15000)
    const onMessage = event => {
      try {
        const value = JSON.parse(event.data)
        const desiredGeneration = value.desired?.generation
        if (desiredGeneration > generation) {
          clearTimeout(timer)
          socket.removeEventListener('message', onMessage)
          reject(new Error(`generation ${generation}: superseded by ${desiredGeneration}`))
        } else if (value.group_state === 'SYNCED' && desiredGeneration === generation &&
                   value.left?.verified_generation === generation &&
                   value.right?.verified_generation === generation) {
          clearTimeout(timer)
          socket.removeEventListener('message', onMessage)
          resolve(value)
        }
      } catch { /* Ignore malformed frames. */ }
    }
    socket.addEventListener('message', onMessage)
  })
}

const putState = payload => request('/api/v1/state', {
  method: 'PUT',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify(payload),
}, 202)

console.log(`Testing ${baseUrl}`)
const index = await request('/')
check(index.includes('<div id="root"></div>') && index.includes('<title>Civic Lights</title>'),
  'index.html does not contain the Vite application shell')
for (const route of ['/color', '/remote', '/diagnostics']) {
  const shell = await request(route)
  check(shell.includes('<div id="root"></div>'), `${route}: SPA fallback missing`)
}

const status = await request('/api/v1/status')
check(status.firmware === expectedFirmware, `unexpected firmware ${status.firmware}`)
check(typeof status.group?.state === 'string', 'missing group status')
check(typeof status.left?.connected === 'boolean' && typeof status.right?.connected === 'boolean', 'missing side status')
check(status.capabilities?.rgb === true && typeof status.capabilities?.white === 'boolean', 'missing capabilities')

const initialState = await request('/api/v1/state')
check(initialState.desired && initialState.observed && initialState.verified_generation, 'invalid state response')
checkObserved(initialState.observed.left, 'state LEFT')
checkObserved(initialState.observed.right, 'state RIGHT')
const presets = await request('/api/v1/presets')
check(presets.favorite?.mode === 'rgb' && typeof presets.white_available === 'boolean', 'invalid presets response')

await request('/api/v1/not-found', {}, 404)
await request('/api/v1/state', {
  method: 'PUT', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ mode: 'rgb', r: 256, g: 0, b: 0, brightness: 64 }),
}, 400)
await request('/api/v1/state', {
  method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: 'x'.repeat(385),
}, 400)

const first = await openSocket()
const second = await openSocket()
check(first.snapshot.firmware === expectedFirmware && second.snapshot.firmware === expectedFirmware,
  'two-client snapshots failed')
checkObserved(first.snapshot.observed?.left, 'WebSocket LEFT')
checkObserved(first.snapshot.observed?.right, 'WebSocket RIGHT')
second.socket.close()
console.log('HTTP schema, errors and two WebSocket clients: PASS')

if (rgbCount > 0) {
  check(Number.isInteger(rgbCount) && rgbCount <= 100, 'rgb-count must be an integer from 1 to 100')
  check(status.left.ready && status.right.ready && status.group.state === 'SYNCED', 'RGB stress requires both sides READY and SYNCED')
  const original = initialState.desired.mode === 'white'
    ? { mode: 'white', brightness: initialState.desired.white }
    : { mode: 'rgb', r: initialState.desired.r, g: initialState.desired.g,
        b: initialState.desired.b, brightness: initialState.desired.brightness }
  try {
    for (let index = 0; index < rgbCount; index++) {
      const payload = { mode: 'rgb', r: index * 73 % 256, g: index * 151 % 256,
        b: index * 211 % 256, brightness: 48 + index * 29 % 176 }
      const accepted = await putState(payload)
      check(accepted.accepted === true && accepted.generation > 0, `RGB ${index + 1}: not accepted`)
      await waitForSync(first.socket, accepted.generation)
      process.stdout.write(`\rRGB synchronized: ${index + 1}/${rgbCount}`)
    }
    process.stdout.write('\n')
  } finally {
    const restored = await putState(original)
    await waitForSync(first.socket, restored.generation)
  }
  const final = await request('/api/v1/status')
  check(final.group.state === 'SYNCED', 'final group is not SYNCED')
  console.log(`${rgbCount}-change RGB test and restore: PASS`)
}

first.socket.close()
console.log('Web hardware tests: PASS')
