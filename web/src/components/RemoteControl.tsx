import { useEffect, useMemo, useState, type CSSProperties } from 'react'
import { saveRemoteButton4, saveRemotePoliceSpeed, testIndicator } from '../api'
import { rgbToHex } from '../color'
import type { DesiredState, PoliceSpeed, RemoteActionType, RemoteButton4Config, RemoteState } from '../types'
import { brightnessToPercent, hexToRgb } from '../uiLogic'
import { Icon } from './Icon'

const actionLabels: Record<RemoteActionType, string> = {
  favorite: 'Favorita', rgb: 'Cor personalizada', white: 'Branco real',
  police: 'Police',
}

const policeSpeedOptions: { id: PoliceSpeed; label: string; timing: string }[] = [
  { id: 'slow', label: 'Lenta', timing: '400/200 ms' },
  { id: 'normal', label: 'Normal', timing: '300/150 ms' },
  { id: 'fast', label: 'Rápida', timing: '220/150 ms' },
  { id: 'very_fast', label: 'Muito rápida', timing: '150 ms/fase' },
]

const buttonLabel = (button: number | null) => button === null ? '?' : ['?', 'A', 'B', 'C', 'D'][button] ?? '?'

function ButtonRail({ number, title, detail, fixed }: { number: string; title: string; detail: string; fixed?: boolean }) {
  return <div className="remote-button-rail">
    <b>{number}</b><span><strong>{title}</strong><small>{detail}</small></span>
    {fixed && <em>FIXO</em>}
  </div>
}

export function RemoteControl({ remote, desired, whiteAvailable, onSaved, onError }: {
  remote: RemoteState
  desired: DesiredState
  whiteAvailable: boolean
  onSaved: (remote: RemoteState) => void
  onError: (message: string | null) => void
}) {
  const [config, setConfig] = useState<RemoteButton4Config>(remote.button4)
  const [policeSpeed, setPoliceSpeed] = useState<PoliceSpeed>(remote.police.speed)
  const [saving, setSaving] = useState(false)
  const [savingPolice, setSavingPolice] = useState(false)
  const [testingIndicator, setTestingIndicator] = useState(false)
  useEffect(() => setConfig(remote.button4), [remote.button4.type, remote.button4.r,
    remote.button4.g, remote.button4.b, remote.button4.brightness])
  useEffect(() => setPoliceSpeed(remote.police.speed), [remote.police.speed])
  const color = useMemo(() => rgbToHex(config), [config.r, config.g, config.b])
  const mapping = Object.entries(remote.mapping).map(([channel, button]) => `${channel.toUpperCase()}→${buttonLabel(button)}`).join('  ')
  const update = (next: Partial<RemoteButton4Config>) => setConfig(current => ({ ...current, ...next }))
  const useCurrent = () => {
    if (desired.mode !== 'rgb') return
    update({ type: 'rgb', r: desired.r, g: desired.g, b: desired.b, brightness: desired.brightness })
  }
  const save = async () => {
    setSaving(true)
    try { onSaved(await saveRemoteButton4(config)); onError(null) }
    catch (cause) { onError(cause instanceof Error ? cause.message : 'Falha ao salvar botão D') }
    finally { setSaving(false) }
  }
  const runIndicatorTest = async () => {
    setTestingIndicator(true)
    try {
      await testIndicator()
      onError(null)
      await new Promise(resolve => window.setTimeout(resolve, 3100))
    } catch (cause) {
      onError(cause instanceof Error ? cause.message : 'Falha no autoteste do LED')
    } finally { setTestingIndicator(false) }
  }
  const savePolice = async () => {
    setSavingPolice(true)
    try { onSaved(await saveRemotePoliceSpeed(policeSpeed)); onError(null) }
    catch (cause) { onError(cause instanceof Error ? cause.message : 'Falha ao salvar velocidade Police') }
    finally { setSavingPolice(false) }
  }

  return <section className="remote-module" aria-labelledby="remote-title">
    <header>
      <span><small>RX480E · 433 MHz</small><strong id="remote-title">AÇÕES A / B / C / D</strong></span>
      <i className={remote.connected ? 'ready' : ''}>{remote.discovery ? 'DISCOVERY' : remote.connected ? 'READY' : 'OFFLINE'}</i>
    </header>
    <div className="remote-live-strip">
      <span>MAP <b>{remote.mapping_complete ? mapping : 'DESCOBERTA PENDENTE'}</b></span>
      <span>ÚLTIMO <b>{remote.last_channel ? `${buttonLabel(remote.last_button)} / ${remote.last_channel}` : '—'}</b></span>
      <span>LED <b className={remote.indicator.on ? 'hot' : ''}>{remote.indicator.on ? 'ON' : 'OFF'}</b></span>
    </div>
    <button type="button" className="remote-indicator-test" disabled={testingIndicator}
      onClick={() => void runIndicatorTest()}>
      {testingIndicator ? 'LED EM TESTE · OFF → ON → OFF' : 'TESTAR LED INDICADOR · 3 S'}
    </button>
    <div className="remote-fixed-grid">
      <ButtonRail number="A" title="Branco padrão" detail="Canal WHITE real" fixed />
      <ButtonRail number="B" title="Vermelho" detail="RGB · 100%" fixed />
      <ButtonRail number="C" title="Police" detail="RED×2 / BLUE · OFF" fixed />
    </div>
    <div className="remote-police-editor">
      <span className="remote-police-sequence" aria-hidden="true"><i /><i /><i /></span>
      <label className="remote-field"><span>VELOCIDADE POLICE · BRILHO 100%</span>
        <select aria-label="Velocidade Police" value={policeSpeed}
          onChange={event => setPoliceSpeed(event.target.value as PoliceSpeed)}>
          {policeSpeedOptions.map(option => <option key={option.id} value={option.id}>{option.label} · {option.timing}</option>)}
        </select>
      </label>
      <button type="button" disabled={savingPolice || policeSpeed === remote.police.speed}
        onClick={() => void savePolice()}>{savingPolice ? 'SALVANDO…' : 'SALVAR'}</button>
    </div>
    <div className="remote-button4-editor">
      <ButtonRail number="D" title="Personalizado" detail="Persistido em NVS" />
      <label className="remote-field"><span>AÇÃO DO BOTÃO D</span>
        <select value={config.type} onChange={event => update({ type: event.target.value as RemoteActionType })}>
          {(Object.keys(actionLabels) as RemoteActionType[]).map(type =>
            <option key={type} value={type} disabled={type === 'white' && !whiteAvailable}>{actionLabels[type]}</option>)}
        </select>
      </label>
      {config.type === 'rgb' && <div className="remote-color-config">
        <label className="remote-color-well" style={{ '--remote-color': color } as CSSProperties}>
          <input type="color" value={color} onChange={event => { const rgb = hexToRgb(event.target.value); if (rgb) update(rgb) }} />
          <span>{color}</span>
        </label>
        <label className="remote-brightness"><span>BRILHO <output>{brightnessToPercent(config.brightness)}%</output></span>
          <input type="range" min="1" max="255" value={config.brightness} onChange={event => update({ brightness: Number(event.target.value) })} />
        </label>
        <button type="button" className="remote-current" disabled={desired.mode !== 'rgb'} onClick={useCurrent}>Usar cor atual</button>
      </div>}
      {config.type === 'white' && <label className="remote-field remote-range"><span>BRILHO WHITE <output>{brightnessToPercent(config.brightness)}%</output></span>
        <input type="range" min="1" max="255" value={config.brightness} onChange={event => update({ brightness: Number(event.target.value) })} />
      </label>}
      <p className="remote-action-note">{config.type === 'favorite' ? 'Usa preset Favorita salvo.' : config.type === 'police' ? 'Clique alterna Police; timeout automático em 30 s.' : 'Ação sempre passa por Desired State e Strict Sync.'}</p>
      <button type="button" className="remote-save" disabled={saving} onClick={() => void save()}><Icon name="save" size={16} />{saving ? 'Salvando…' : 'Salvar botão D'}</button>
    </div>
  </section>
}
