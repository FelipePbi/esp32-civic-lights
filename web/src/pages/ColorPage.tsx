import type { RGB } from '../color'
import { rgbToHex } from '../color'
import type { Snapshot } from '../types'
import { brightnessToPercent } from '../uiLogic'
import { PageHeader, SectionHeading } from '../components/AppChrome'
import { ColorPicker } from '../components/ColorPicker'
import { CompactConnectionStatus } from '../components/StatusComponents'
import { Icon } from '../components/Icon'
import type { PendingAction } from './HomePage'

interface Props {
  snapshot: Snapshot
  websocketConnected: boolean
  color: RGB
  brightness: number
  pendingAction: PendingAction
  onColor: (color: RGB, final: boolean) => void
  onBrightness: (brightness: number, final: boolean) => void
  onWhite: () => void
  onFavorite: () => void
  onBack: () => void
}

export function ColorPage({ snapshot, websocketConnected, color, brightness, pendingAction, onColor, onBrightness, onWhite, onFavorite, onBack }: Props) {
  return <main id="main-content" className="app-shell subpage color-page">
    <PageHeader title="COR E BRILHO" connected={websocketConnected} onBack={onBack} />
    <CompactConnectionStatus left={snapshot.left} right={snapshot.right} groupState={snapshot.group_state} />

    <section className="color-controls">
      <SectionHeading>Cor ambiente</SectionHeading>
      <div className="color-workbench">
        <ColorPicker value={color} onChange={onColor} />
        <label className="brightness-vertical">
          <span className="brightness-icon"><Icon name="sun" size={20} /></span>
          <strong>{brightnessToPercent(brightness)}<small>%</small></strong>
          <input type="range" min="0" max="255" value={brightness} aria-label="Brilho"
            onInput={event => onBrightness(Number(event.currentTarget.value), false)}
            onPointerUp={event => onBrightness(Number(event.currentTarget.value), true)}
            onKeyUp={event => onBrightness(Number(event.currentTarget.value), true)} />
          <span className="brightness-track-label">BRILHO</span>
        </label>
      </div>
      <div className="color-readout-compact">
        <span className="selected-swatch" style={{ backgroundColor: rgbToHex(color) }} />
        <span><small>SELECIONADA</small><strong>{rgbToHex(color)}</strong></span>
        <output>{color.r} · {color.g} · {color.b}</output>
      </div>
    </section>

    <section className="color-presets">
      <SectionHeading>Atalhos</SectionHeading>
      <div>
        <button type="button" disabled={!snapshot.white_available || pendingAction === 'white'} onClick={onWhite}><Icon name="sun" />{pendingAction === 'white' ? 'Aplicando…' : 'Branco real'}</button>
        <button type="button" disabled={pendingAction === 'favorite'} onClick={onFavorite}>
          <span className="preset-dot" style={{ backgroundColor: rgbToHex(snapshot.favorite) }} />{pendingAction === 'favorite' ? 'Aplicando…' : 'Favorita'}
        </button>
      </div>
    </section>

    <div className={`verification-strip ${snapshot.group_state === 'SYNCED' ? 'verified' : ''}`} aria-live="polite">
      <Icon name={snapshot.group_state === 'SYNCED' ? 'check' : 'sync'} size={16} />
      {snapshot.group_state === 'SYNCED' ? 'Último ajuste verificado nos dois lados' : 'Aplicando e verificando nos dois lados'}
    </div>
  </main>
}
