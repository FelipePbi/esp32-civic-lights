import type { ObservedState, SideStatus } from '../types'
import { mapObservedStateToHeadlightVisual } from '../headlight'
import { sideStatusLabel } from '../uiLogic'
import { HeadlightVisual } from './HeadlightVisual'
import { Icon } from './Icon'

export function SignalMeter({ rssi }: { rssi: number }) {
  const strength = rssi >= -65 ? 4 : rssi >= -75 ? 3 : rssi >= -85 ? 2 : 1
  return <span className="signal-meter" aria-label={`Sinal ${rssi} dBm`}>
    {[1, 2, 3, 4].map(level => <i key={level} className={level <= strength ? 'active' : ''} />)}
    <b>{rssi} dBm</b>
  </span>
}

export function HeadlightCard({ name, side, observed }: { name: 'LEFT' | 'RIGHT'; side: SideStatus; observed: ObservedState }) {
  const visual = mapObservedStateToHeadlightVisual(side, observed)
  const status = sideStatusLabel(side)
  return <article className={`headlight-card state-${visual.connectionState}`} data-testid={`headlight-${name.toLowerCase()}`}>
    <div className="headlight-card-top">
      <strong>{name}</strong>
      <span className="connection-label"><i aria-hidden="true" />{status}</span>
    </div>
    <HeadlightVisual side={name === 'LEFT' ? 'left' : 'right'} {...visual} />
    <div className="headlight-card-meta">
      {side.connected ? <SignalMeter rssi={side.rssi} /> : <span className="signal-offline"><Icon name="signal" size={14} /> Sem sinal</span>}
      <span>{visual.illuminated ? visual.mode === 'white' ? 'WHITE' : visual.color : observed.valid ? 'SEM CONFIRMAÇÃO' : 'AGUARDANDO ESTADO'}</span>
    </div>
  </article>
}

const groupContent: Record<string, { title: string; detail: string; tone: string }> = {
  SYNCED: { title: 'SINCRONIZADO', detail: 'Ambos os faróis estão sincronizados', tone: 'success' },
  RECONCILING: { title: 'RECONCILIANDO', detail: 'Aplicando e verificando nos dois lados', tone: 'warning' },
  POWER_CYCLE_RECOVERY: { title: 'RECUPERANDO', detail: 'Recuperando conexão dos faróis', tone: 'warning' },
  DEGRADED: { title: 'DEGRADADO', detail: 'Aguardando conexão completa', tone: 'warning' },
  UNSYNCED: { title: 'FORA DE SINCRONIA', detail: 'Verificação dos lados divergiu', tone: 'danger' },
  ERROR: { title: 'ERRO', detail: 'Não foi possível verificar o conjunto', tone: 'danger' },
  UNINITIALIZED: { title: 'INICIALIZANDO', detail: 'Consultando estado dos faróis', tone: 'neutral' },
  DISCONNECTED: { title: 'DESCONECTADO', detail: 'Nenhum farol conectado', tone: 'neutral' },
}

export function GroupStatus({ state, generation, compact = false }: { state: string; generation: number; compact?: boolean }) {
  const content = groupContent[state] ?? { title: state, detail: 'Estado do conjunto', tone: 'neutral' }
  return <section className={`group-status ${content.tone} ${compact ? 'compact' : ''}`} aria-label="Estado do conjunto">
    <span className="group-status-icon"><Icon name={state === 'SYNCED' ? 'check' : 'sync'} size={18} /></span>
    <span><small>ESTADO DO CONJUNTO</small><strong>{content.title}</strong>{!compact && <em>{content.detail}</em>}</span>
    <b>G{generation}</b>
  </section>
}

export function CompactConnectionStatus({ left, right, groupState }: { left: SideStatus; right: SideStatus; groupState: string }) {
  return <div className="compact-connection" aria-label="Resumo das conexões">
    <span><b>LEFT</b><i className={left.ready ? 'ready' : ''} />{left.connected ? `${left.rssi} dBm` : sideStatusLabel(left)}</span>
    <span className={`compact-group ${groupState === 'SYNCED' ? 'ready' : ''}`}><Icon name={groupState === 'SYNCED' ? 'check' : 'sync'} size={13} />{groupContent[groupState]?.title ?? groupState}</span>
    <span><b>RIGHT</b><i className={right.ready ? 'ready' : ''} />{right.connected ? `${right.rssi} dBm` : sideStatusLabel(right)}</span>
  </div>
}
