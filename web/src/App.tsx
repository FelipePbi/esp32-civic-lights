import { useCallback, useEffect, useRef, useState } from 'react'
import { forceResync, saveFavorite, setRgb, setWhite } from './api'
import type { RGB } from './color'
import { useController } from './hooks/useController'
import { routeFromPathname, type AppRoute } from './router'
import type { AcceptedResponse, FavoritePreset } from './types'
import { throttleDelay } from './uiLogic'
import { ColorPage } from './pages/ColorPage'
import { DiagnosticsPage } from './pages/DiagnosticsPage'
import { HomePage, type PendingAction } from './pages/HomePage'
import { RemotePage } from './pages/RemotePage'
import { Icon } from './components/Icon'
import './styles.css'

export default function App() {
  const { snapshot, setSnapshot, websocketConnected, error, setError, refresh } = useController()
  const [route, setRoute] = useState<AppRoute>(() => routeFromPathname(location.pathname))
  const [color, setColor] = useState<RGB>({ r: 255, g: 0, b: 0 })
  const [brightness, setBrightness] = useState(64)
  const [lightMode, setLightMode] = useState<'rgb' | 'white'>('rgb')
  const [pendingGeneration, setPendingGeneration] = useState<number | null>(null)
  const [pendingAction, setPendingAction] = useState<PendingAction>(null)
  const [appliedAction, setAppliedAction] = useState<PendingAction>(null)
  const [savingFavorite, setSavingFavorite] = useState(false)
  const throttle = useRef<{ last: number; timer: number | null }>({ last: 0, timer: null })
  const latestAcceptedGeneration = useRef(0)
  const latest = useRef({ color, brightness, lightMode })
  latest.current = { color, brightness, lightMode }

  useEffect(() => {
    const onPopState = () => setRoute(routeFromPathname(location.pathname))
    window.addEventListener('popstate', onPopState)
    return () => window.removeEventListener('popstate', onPopState)
  }, [])

  useEffect(() => () => {
    if (throttle.current.timer !== null) clearTimeout(throttle.current.timer)
  }, [])

  const navigate = useCallback((path: string, replace = false) => {
    const next = routeFromPathname(path)
    if (location.pathname !== next) {
      if (replace) history.replaceState({}, '', next)
      else history.pushState({}, '', next)
    }
    setRoute(next)
    window.scrollTo(0, 0)
  }, [])

  const goHome = useCallback(() => navigate('/', true), [navigate])

  useEffect(() => {
    if (!snapshot?.desired.valid) return
    latestAcceptedGeneration.current = Math.max(latestAcceptedGeneration.current, snapshot.desired.generation)
    setLightMode(snapshot.desired.mode)
    if (snapshot.desired.mode === 'rgb') {
      setColor({ r: snapshot.desired.r, g: snapshot.desired.g, b: snapshot.desired.b })
      setBrightness(snapshot.desired.brightness)
    } else {
      setBrightness(snapshot.desired.white)
    }
  }, [snapshot?.desired.generation])

  useEffect(() => {
    if (pendingGeneration === null || !snapshot) return
    if (snapshot.desired.generation > pendingGeneration) {
      setAppliedAction(null)
      setPendingGeneration(null)
      setPendingAction(null)
      return
    }
    if (snapshot.group_state === 'SYNCED' && snapshot.desired.generation === pendingGeneration &&
        snapshot.left.verified_generation === pendingGeneration && snapshot.right.verified_generation === pendingGeneration) {
      setAppliedAction(pendingAction)
      setPendingGeneration(null)
      setPendingAction(null)
    }
  }, [snapshot, pendingGeneration, pendingAction])

  useEffect(() => {
    if (!appliedAction) return
    const timer = window.setTimeout(() => setAppliedAction(null), 1800)
    return () => clearTimeout(timer)
  }, [appliedAction])

  const accepted = (response: AcceptedResponse, action: PendingAction = null) => {
    if (response.generation < latestAcceptedGeneration.current) return
    latestAcceptedGeneration.current = response.generation
    setPendingGeneration(response.generation)
    setPendingAction(action)
    setAppliedAction(null)
    setError(null)
    if (action === 'resync') {
      setSnapshot(current => current ? { ...current, group_state: response.group_state } : current)
      window.setTimeout(() => { void refresh() }, 250)
    }
  }

  const sendCurrent = async () => {
    const current = latest.current
    try {
      accepted(current.lightMode === 'white'
        ? await setWhite(current.brightness)
        : await setRgb(current.color.r, current.color.g, current.color.b, current.brightness))
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Comando recusado')
    }
  }

  const scheduleCurrent = (final: boolean) => {
    const now = performance.now()
    const run = () => {
      throttle.current.last = performance.now()
      throttle.current.timer = null
      void sendCurrent()
    }
    const delay = throttleDelay(throttle.current.last, now, 100, final)
    if (delay === 0) {
      if (throttle.current.timer !== null) clearTimeout(throttle.current.timer)
      run()
    } else if (throttle.current.timer === null) {
      throttle.current.timer = window.setTimeout(run, delay)
    }
  }

  const changeColor = (next: RGB, final: boolean) => {
    setColor(next)
    setLightMode('rgb')
    latest.current.color = next
    latest.current.lightMode = 'rgb'
    scheduleCurrent(final)
  }

  const changeBrightness = (level: number, final: boolean) => {
    setBrightness(level)
    latest.current.brightness = level
    scheduleCurrent(final)
  }

  if (!snapshot) {
    return <main id="main-content" className="loading-shell">
      <div className="loading-emblem"><span /></div>
      <h1>CIVIC <span>LIGHTS</span></h1>
      <p>Conectando…</p>
      {error && <small>{error}</small>}
    </main>
  }

  const applyFavorite = async () => {
    const favorite = snapshot.favorite
    const next = { r: favorite.r, g: favorite.g, b: favorite.b }
    setColor(next)
    setBrightness(favorite.brightness)
    setLightMode('rgb')
    latest.current = { color: next, brightness: favorite.brightness, lightMode: 'rgb' }
    try {
      accepted(await setRgb(next.r, next.g, next.b, favorite.brightness), 'favorite')
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Favorita recusada')
    }
  }

  const storeFavorite = async () => {
    setSavingFavorite(true)
    const favorite: FavoritePreset = { mode: 'rgb', ...color, brightness }
    try {
      const response = await saveFavorite(favorite)
      setSnapshot(current => current ? { ...current, favorite: response.favorite } : current)
      setError(null)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Não foi possível salvar')
    } finally {
      setSavingFavorite(false)
    }
  }

  const applyWhite = async () => {
    setLightMode('white')
    latest.current.lightMode = 'white'
    try {
      accepted(await setWhite(brightness), 'white')
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Branco indisponível')
    }
  }

  const resync = async () => {
    try {
      accepted(await forceResync(), 'resync')
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Falha no resync')
    }
  }

  const common = { snapshot, websocketConnected }
  const page = route === '/color'
    ? <ColorPage {...common} color={color} brightness={brightness} pendingAction={pendingAction}
      onColor={changeColor} onBrightness={changeBrightness} onWhite={() => void applyWhite()}
      onFavorite={() => void applyFavorite()} onBack={goHome} />
    : route === '/remote'
        ? <RemotePage {...common} onBack={goHome} onError={setError}
          onRemoteSaved={remote => setSnapshot(current => current ? { ...current, remote } : current)} />
      : route === '/diagnostics'
        ? <DiagnosticsPage {...common} resyncing={pendingAction === 'resync'} onResync={() => void resync()} onBack={goHome} />
      : <HomePage {...common} pendingAction={pendingAction} appliedAction={appliedAction} savingFavorite={savingFavorite}
          onWhite={() => void applyWhite()} onFavorite={() => void applyFavorite()} onSaveFavorite={() => void storeFavorite()} onNavigate={navigate} />

  return <><a className="skip-link" href="#main-content">Pular para conteúdo</a>{page}{error && <div className="error-toast" role="alert" aria-live="assertive"><span>!</span><p>{error}</p><button type="button" aria-label="Fechar aviso" onClick={() => setError(null)}><Icon name="back" size={18} /></button></div>}</>
}
