import { PageHeader } from '../components/AppChrome'
import { RemoteControl } from '../components/RemoteControl'
import { CompactConnectionStatus } from '../components/StatusComponents'
import type { RemoteState, Snapshot } from '../types'

export function RemotePage({ snapshot, websocketConnected, onBack, onRemoteSaved, onError }: {
  snapshot: Snapshot
  websocketConnected: boolean
  onBack: () => void
  onRemoteSaved: (remote: RemoteState) => void
  onError: (message: string | null) => void
}) {
  return <main id="main-content" className="app-shell subpage remote-page">
    <PageHeader title="CONTROLE REMOTO" connected={websocketConnected} onBack={onBack} />
    <CompactConnectionStatus left={snapshot.left} right={snapshot.right} groupState={snapshot.group_state} />
    <RemoteControl remote={snapshot.remote} desired={snapshot.desired}
      whiteAvailable={snapshot.white_available} onSaved={onRemoteSaved} onError={onError} />
    <p className="remote-page-note">
      A/B/C são funções fixas. D é configurável e persistido no ESP32.
    </p>
  </main>
}
