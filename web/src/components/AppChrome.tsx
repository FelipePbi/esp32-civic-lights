import type { ReactNode } from 'react'
import { Icon } from './Icon'

export function LiveBadge({ connected }: { connected: boolean }) {
  return <div className={`live-badge ${connected ? 'is-live' : 'is-reconnecting'}`} aria-live="polite">
    <span aria-hidden="true" />{connected ? 'AO VIVO' : 'RECONECTANDO'}
  </div>
}

export function AppHeader({ connected }: { connected: boolean }) {
  return <header className="app-header">
    <div><h1>CIVIC <span>LIGHTS</span></h1><p>CONTROLE LOCAL</p></div>
    <LiveBadge connected={connected} />
  </header>
}

export function PageHeader({ title, connected, onBack }: { title: string; connected: boolean; onBack: () => void }) {
  return <header className="page-header">
    <button type="button" className="icon-button" onClick={onBack} aria-label="Voltar"><Icon name="back" /></button>
    <h1>{title}</h1>
    <LiveBadge connected={connected} />
  </header>
}

export function SectionHeading({ children, action }: { children: ReactNode; action?: ReactNode }) {
  return <div className="section-heading"><h2>{children}</h2>{action}</div>
}
