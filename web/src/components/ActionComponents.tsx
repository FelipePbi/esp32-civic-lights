import type { ReactNode } from 'react'
import type { IconName } from './Icon'
import { Icon } from './Icon'

export function QuickAction({ icon, title, detail, tone, disabled, busy, applied, onClick }: {
  icon: IconName
  title: string
  detail: string
  tone?: 'white' | 'favorite'
  disabled?: boolean
  busy?: boolean
  applied?: boolean
  onClick: () => void
}) {
  return <button type="button" className={`quick-action ${tone ?? ''}`} disabled={disabled || busy} onClick={onClick}>
    <span className="quick-action-icon"><Icon name={applied ? 'check' : icon} /></span>
    <span><strong>{busy ? 'Aplicando…' : applied ? 'Aplicado' : title}</strong><small>{detail}</small></span>
  </button>
}

export function NavigationCard({ icon, title, detail, href, onNavigate }: {
  icon: IconName
  title: string
  detail: string
  href: string
  onNavigate: (href: string) => void
}) {
  return <a className="navigation-card" href={href} onClick={event => { event.preventDefault(); onNavigate(href) }}>
    <span className="navigation-icon"><Icon name={icon} /></span>
    <span><strong>{title}</strong><small>{detail}</small></span>
    <Icon name="chevron" size={18} />
  </a>
}

export function TelemetryCard({ label, value, detail, icon }: { label: string; value: ReactNode; detail?: string; icon?: IconName }) {
  return <article className="telemetry-card">
    <div>{icon && <Icon name={icon} size={17} />}<small>{label}</small></div>
    <strong>{value}</strong>{detail && <p>{detail}</p>}
  </article>
}
