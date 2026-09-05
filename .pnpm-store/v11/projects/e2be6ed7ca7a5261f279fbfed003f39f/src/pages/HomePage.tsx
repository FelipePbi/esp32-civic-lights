import { rgbToHex } from '../color'
import type { Snapshot } from '../types'
import { AppHeader, SectionHeading } from '../components/AppChrome'
import { NavigationCard, QuickAction } from '../components/ActionComponents'
import { GroupStatus, HeadlightCard } from '../components/StatusComponents'
import { Icon } from '../components/Icon'

export type PendingAction = 'white' | 'favorite' | 'resync' | null

interface Props {
  snapshot: Snapshot
  websocketConnected: boolean
  pendingAction: PendingAction
  appliedAction: PendingAction
  savingFavorite: boolean
  onWhite: () => void
  onFavorite: () => void
  onSaveFavorite: () => void
  onNavigate: (path: string) => void
}

export function HomePage({ snapshot, websocketConnected, pendingAction, appliedAction, savingFavorite, onWhite, onFavorite, onSaveFavorite, onNavigate }: Props) {
  const homeGroupState = snapshot.group_state === 'UNINITIALIZED' ? snapshot.group_state
    : !snapshot.left.connected && !snapshot.right.connected ? 'DISCONNECTED' : snapshot.group_state
  return <main id="main-content" className="app-shell home-shell">
    <AppHeader connected={websocketConnected} />

    <section className="headlight-deck" aria-label="Faróis LEFT e RIGHT">
      <HeadlightCard name="LEFT" side={snapshot.left} observed={snapshot.observed.left} />
      <HeadlightCard name="RIGHT" side={snapshot.right} observed={snapshot.observed.right} />
    </section>

    <GroupStatus state={homeGroupState} generation={snapshot.desired.generation} />

    <section className="home-quick">
      <SectionHeading>Acesso rápido</SectionHeading>
      <div className="quick-action-grid">
        <QuickAction icon="sun" title="Branco real" detail={snapshot.white_available ? 'CANAL WHITE' : 'INDISPONÍVEL'} tone="white"
          disabled={!snapshot.white_available} busy={pendingAction === 'white'} applied={appliedAction === 'white'} onClick={onWhite} />
        <QuickAction icon="star" title="Favorita" detail={rgbToHex(snapshot.favorite)} tone="favorite"
          busy={pendingAction === 'favorite'} applied={appliedAction === 'favorite'} onClick={onFavorite} />
      </div>
      <button type="button" className="save-current" disabled={savingFavorite} onClick={onSaveFavorite}>
        <Icon name={savingFavorite ? 'sync' : 'save'} size={16} />{savingFavorite ? 'Salvando…' : 'Salvar cor atual como favorita'}
      </button>
    </section>

    <nav className="home-navigation" aria-label="Navegação principal">
      <SectionHeading>Controles</SectionHeading>
      <div className="navigation-list">
        <NavigationCard icon="palette" title="COR E BRILHO" detail="Escolha o clima" href="/color" onNavigate={onNavigate} />
        <NavigationCard icon="remote" title="CONTROLE REMOTO" detail="Teclas A / B / C / D" href="/remote" onNavigate={onNavigate} />
        <NavigationCard icon="pulse" title="DIAGNÓSTICO" detail="Telemetria do sistema" href="/diagnostics" onNavigate={onNavigate} />
      </div>
    </nav>
  </main>
}
