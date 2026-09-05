import process from 'node:process'

const baseUrl = process.env.CIVIC_BASE_URL ?? 'http://192.168.4.1'
const verifyReboot = process.argv.includes('--verify-reboot')
const allowSp624e = process.argv.includes('--allow-sp624e')
const wsUrl = baseUrl.replace(/^http/, 'ws') + '/ws'
const sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds))
const check = (condition, message) => { if (!condition) throw new Error(message) }

async function request(path, options = {}, expected = 200) {
  const response = await fetch(baseUrl + path, { signal: AbortSignal.timeout(5000), ...options })
  check(response.status === expected, `${path}: expected HTTP ${expected}, got ${response.status}`)
  return response.json()
}

const post = (path, expected = 202) => request(path, { method: 'POST' }, expected)
const put = (path, body) => request(path, {
  method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
})

function openSocket() {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(wsUrl)
    const timer = setTimeout(() => reject(new Error('WebSocket timeout')), 5000)
    socket.addEventListener('error', () => reject(new Error('WebSocket failed')), { once: true })
    socket.addEventListener('message', event => {
      try {
        const value = JSON.parse(event.data)
        if (value.type === 'snapshot') { clearTimeout(timer); resolve(socket) }
      } catch { /* Ignore malformed frame. */ }
    })
  })
}

function waitForFrame(socket, predicate, timeoutMs, label) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      socket.removeEventListener('message', onMessage)
      reject(new Error(`${label}: timeout after ${timeoutMs} ms`))
    }, timeoutMs)
    const onMessage = event => {
      try {
        const value = JSON.parse(event.data)
        if (!predicate(value)) return
        clearTimeout(timer)
        socket.removeEventListener('message', onMessage)
        resolve(value)
      } catch { /* Ignore malformed frame. */ }
    }
    socket.addEventListener('message', onMessage)
  })
}

async function capturePress(socket, label, observeMs) {
  console.log(`ACTION REQUIRED: ${label}`)
  let count = 0
  let captured = null
  const onMessage = event => {
    try {
      const value = JSON.parse(event.data)
      if (value.type === 'remote_button' && value.remote?.last_channel) {
        count++
        captured = value.remote
      }
    } catch { /* Ignore malformed frame. */ }
  }
  socket.addEventListener('message', onMessage)
  const deadline = Date.now() + 30000
  while (captured === null && Date.now() < deadline) await sleep(25)
  if (captured === null) {
    socket.removeEventListener('message', onMessage)
    throw new Error(`${label}: no RF event in 30 s`)
  }
  await sleep(observeMs)
  socket.removeEventListener('message', onMessage)
  check(count === 1, `${label}: expected one event, got ${count}`)
  console.log(`  PASS channel=${captured.last_channel} VT=${captured.vt ? 1 : 0} events=${count}`)
  await sleep(300)
  return captured.last_channel.toLowerCase()
}

console.log(`RF bench target ${baseUrl}`)
const status = await request('/api/v1/status')
check(status.firmware === '0.8.0', `expected firmware 0.8.0, got ${status.firmware}`)
if (!allowSp624e) {
  check(!status.left?.ready && !status.right?.ready,
    'SP624E is READY. Disconnect headlights or rerun explicitly with -AllowSp624E.')
}
const initial = await request('/api/v1/remote')
check(initial.connected === true, 'RX480E manager not ready')

if (verifyReboot) {
  check(initial.mapping_complete, 'mapping did not survive reboot')
  check(initial.button4?.type === 'rgb' && initial.button4.r === 128 &&
        initial.button4.g === 0 && initial.button4.b === 255 &&
        initial.button4.brightness === 64,
        'Button 4 PURPLE config did not survive reboot')
  console.log('Mapping and Button D NVS reboot restore: PASS')
  process.exit(0)
}

const socket = await openSocket()
let discoveryStarted = false
try {
  const discovery = await post('/api/v1/remote/discovery/start')
  check(discovery.discovery === true, 'discovery mode did not start')
  discoveryStarted = true
  console.log('Discovery ON: logical actions suppressed')

  const mapping = []
  const labels = ['A', 'B', 'C', 'D']
  for (let button = 1; button <= 4; button++) {
    mapping.push(await capturePress(socket, `press BUTTON ${labels[button - 1]} once, then release`, 700))
  }
  check(new Set(mapping).size === 4, `channels must be unique, got ${mapping.join(', ')}`)

  for (let button = 1; button <= 4; button++) {
    const channel = await capturePress(socket,
      `hold BUTTON ${labels[button - 1]} for about 2 seconds, then release`, 2300)
    check(channel === mapping[button - 1],
      `BUTTON ${labels[button - 1]}: short=${mapping[button - 1]} long=${channel}`)
  }

  const payload = Object.fromEntries(mapping.map((channel, index) => [`button${index + 1}`, channel]))
  const saved = await put('/api/v1/remote/mapping', payload)
  check(saved.mapping_complete === true, 'mapping was not persisted')
  console.log(`Physical mapping persisted: ${mapping.map((channel, index) => `${labels[index]}→${channel.toUpperCase()}`).join('  ')}`)

  await post('/api/v1/remote/discovery/stop')
  discoveryStarted = false
  console.log('Discovery OFF: logical actions enabled')

  const indicatorOn = waitForFrame(socket,
    value => value.remote?.indicator?.on === true, 3500, 'indicator ON')
  await post('/api/v1/indicator/test')
  await indicatorOn
  await waitForFrame(socket,
    value => value.remote?.indicator?.on === false, 3500, 'indicator OFF')
  console.log('Indicator state sequence OFF → ON → OFF: PASS (confirm LED visually)')

  const purple = { type: 'rgb', r: 128, g: 0, b: 255, brightness: 64 }
  const configured = await put('/api/v1/remote/button4', purple)
  check(configured.button4.type === 'rgb' && configured.button4.r === 128 &&
        configured.button4.b === 255, 'Button 4 PURPLE save failed')
  console.log('Button D PURPLE saved. Power-cycle ESP32, then run with -VerifyReboot.')
  console.log('RF bench phase 1: PASS')
} finally {
  if (discoveryStarted) {
    try { await post('/api/v1/remote/discovery/stop') } catch { /* Reboot also clears it. */ }
  }
  socket.close()
}
