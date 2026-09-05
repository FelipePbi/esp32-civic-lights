import { useCallback, useEffect, useRef, useState } from 'react'
import { loadSnapshot } from '../api'
import type { Snapshot } from '../types'
import { parseWebSocketEvent } from '../uiLogic'

export function useController() {
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null)
  const [websocketConnected, setWebsocketConnected] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const retry = useRef(0)

  const refresh = useCallback(async () => {
    try {
      setSnapshot(await loadSnapshot())
      setError(null)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'ESP32 indisponível')
    }
  }, [])

  useEffect(() => { void refresh() }, [refresh])

  // Refresh a complete snapshot only while WebSocket is unavailable. Five
  // seconds keeps fallback eventual without competing with weak BLE links.
  useEffect(() => {
    if (websocketConnected) return
    const poll = window.setInterval(() => { void refresh() }, 5000)
    return () => clearInterval(poll)
  }, [websocketConnected, refresh])

  useEffect(() => {
    let socket: WebSocket | null = null
    let timer = 0
    let stopped = false
    const connect = () => {
      const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:'
      socket = new WebSocket(`${protocol}//${location.host}/ws`)
      socket.onopen = () => { retry.current = 0; setWebsocketConnected(true) }
      socket.onmessage = event => {
        try {
          const update = parseWebSocketEvent(event.data)
          if (update) {
            setSnapshot(update)
            setError(null)
          }
        } catch { /* Ignore malformed frames and retain the last valid snapshot. */ }
      }
      socket.onerror = () => socket?.close()
      socket.onclose = () => {
        setWebsocketConnected(false)
        if (!stopped) {
          const delays = [500, 1000, 2000, 5000]
          const delay = delays[Math.min(retry.current++, delays.length - 1)]
          timer = window.setTimeout(connect, delay)
        }
      }
    }
    connect()
    return () => { stopped = true; clearTimeout(timer); socket?.close() }
  }, [])

  return { snapshot, setSnapshot, websocketConnected, error, setError, refresh }
}
