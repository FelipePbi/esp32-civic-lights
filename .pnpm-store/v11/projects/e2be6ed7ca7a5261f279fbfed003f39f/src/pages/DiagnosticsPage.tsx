import type { Snapshot } from '../types'
import { PageHeader, SectionHeading } from '../components/AppChrome'
import { TelemetryCard } from '../components/ActionComponents'
import { CompactConnectionStatus, GroupStatus } from '../components/StatusComponents'
import { Icon } from '../components/Icon'

const formatUptime = (milliseconds: number) => {
  const minutes = Math.floor(milliseconds / 60000)
  const hours = Math.floor(minutes / 60)
  return hours > 0 ? `${hours}h ${minutes % 60}m` : `${minutes} min`
}

const formatReason = (reason: number) => reason ? `0x${reason.toString(16).toUpperCase()}` : '—'

const formatEventAge = (uptimeMs: number, eventMs?: number) => {
  if (!eventMs) return '—'
  const ageMs = Math.max(0, uptimeMs - eventMs)
  return ageMs < 1000 ? `${ageMs} ms atrás` : `${(ageMs / 1000).toFixed(1)} s atrás`
}

export function DiagnosticsPage({ snapshot, websocketConnected, resyncing, onResync, onBack }: {
  snapshot: Snapshot
  websocketConnected: boolean
  resyncing: boolean
  onResync: () => void
  onBack: () => void
}) {
  return <main id="main-content" className="app-shell subpage diagnostics-page">
    <PageHeader title="DIAGNÓSTICO" connected={websocketConnected} onBack={onBack} />
    <CompactConnectionStatus left={snapshot.left} right={snapshot.right} groupState={snapshot.group_state} />
    <GroupStatus state={snapshot.group_state} generation={snapshot.desired.generation} compact />

    <section className="telemetry-section">
      <SectionHeading>Telemetria</SectionHeading>
      <div className="telemetry-grid">
        <TelemetryCard label="FIRMWARE" value={snapshot.firmware} detail="ESP-IDF · BLE CENTRAL" />
        <TelemetryCard label="UPTIME" value={formatUptime(snapshot.uptime_ms)} detail="DESDE O ÚLTIMO BOOT" />
        <TelemetryCard label="LEFT RSSI" value={snapshot.left.connected ? `${snapshot.left.rssi} dBm` : '—'} detail={`${snapshot.left.reconnect_count} RECONEXÕES`} icon="signal" />
        <TelemetryCard label="RIGHT RSSI" value={snapshot.right.connected ? `${snapshot.right.rssi} dBm` : '—'} detail={`${snapshot.right.reconnect_count} RECONEXÕES`} icon="signal" />
        <TelemetryCard label="WI-FI" value={`${snapshot.wifi_clients} cliente${snapshot.wifi_clients === 1 ? '' : 's'}`} detail="CIVIC-LIGHTS" icon="wifi" />
        <TelemetryCard label="WEBSOCKET" value={websocketConnected ? 'Online' : 'Reconectando'} detail="EVENTOS EM TEMPO REAL" icon="pulse" />
        <TelemetryCard label="GERAÇÃO" value={`G${snapshot.desired.generation}`} detail={`LEFT G${snapshot.left.verified_generation}`} />
        <TelemetryCard label="STRICT SYNC" value={snapshot.group_state === 'SYNCED' ? 'Verificado' : snapshot.group_state} detail={`RIGHT G${snapshot.right.verified_generation}`} icon="sync" />
        <TelemetryCard label="SISTEMA" value={snapshot.system_health.healthy ? 'Saudável' : 'Travado'} detail={snapshot.system_health.stale_component || 'HEARTBEATS OK'} icon="pulse" />
        <TelemetryCard label="ÚLTIMO RESET" value={snapshot.system_health.reset_reason} detail={snapshot.system_health.previous_recovery_reason || 'SEM RECUPERAÇÃO'} />
        <TelemetryCard label="HEAP LIVRE" value={`${Math.round(snapshot.system_health.free_heap / 1024)} KB`} detail={`MÍN ${Math.round(snapshot.system_health.minimum_free_heap / 1024)} KB`} />
        <TelemetryCard label="RECUPERAÇÕES" value={`${snapshot.system_health.counters.ble_forced_recoveries}`} detail={`${snapshot.system_health.supervisor_restarts} REINÍCIOS`} icon="sync" />
        <TelemetryCard label="ÚLTIMA DESCONEXÃO" value={`L ${formatEventAge(snapshot.uptime_ms, snapshot.left.last_disconnect_ms)}`} detail={`RIGHT ${formatEventAge(snapshot.uptime_ms, snapshot.right.last_disconnect_ms)}`} icon="signal" />
        <TelemetryCard label="MOTIVO" value={`L ${formatReason(snapshot.left.last_disconnect_reason ?? 0)}`} detail={`RIGHT ${formatReason(snapshot.right.last_disconnect_reason ?? 0)}`} />
        <TelemetryCard label="CLASSIFICAÇÃO" value={`L ${snapshot.left.disconnect_classification ?? 'NORMAL_DISCONNECT'}`} detail={`RIGHT ${snapshot.right.disconnect_classification ?? 'NORMAL_DISCONNECT'}`} />
        <TelemetryCard label="FAST RECOVERY" value={`L ${snapshot.left.fast_recovery ?? 'IDLE'}`} detail={`RIGHT ${snapshot.right.fast_recovery ?? 'IDLE'}`} icon="sync" />
        <TelemetryCard label="ÚLTIMA RECUPERAÇÃO" value={`${Math.max(snapshot.left.last_recovery_ms ?? 0, snapshot.right.last_recovery_ms ?? 0) / 1000}s`} detail="ATÉ GROUP SYNCED" />
        <TelemetryCard label="ÚLTIMO ADVERTISING" value={`${Math.max(snapshot.left.last_adv_after_loss_ms ?? 0, snapshot.right.last_adv_after_loss_ms ?? 0)} ms`} detail="APÓS PERDA" icon="signal" />
        <TelemetryCard label="SUPERVISION TIMEOUT" value={`L ${snapshot.left.supervision_timeout_accepted_ms ?? 0} ms`} detail={`RIGHT ${snapshot.right.supervision_timeout_accepted_ms ?? 0} MS · PEDIDO ${snapshot.left.supervision_timeout_requested_ms ?? 0} MS`} />
        <TelemetryCard label="API DO GRUPO" value={`${snapshot.system_health.counters.group_api_timeouts} timeout`} detail={`${snapshot.system_health.counters.group_api_busy} OCUPADO`} />
        <TelemetryCard label="RF DESCARTES" value={`${snapshot.system_health.counters.rf_event_drops}`} detail="CONTROLE INDEPENDENTE" />
        <TelemetryCard label="LED GPIO23" value={snapshot.system_health.indicator.gpio_level ? 'Alto' : 'Baixo'} detail={snapshot.remote.indicator.reason} />
      </div>
    </section>

    <section className="diagnostic-actions">
      <button type="button" disabled={resyncing} onClick={onResync}><Icon name="sync" />{resyncing ? 'Ressincronizando…' : 'Ressincronizar conjunto'}</button>
      <p>Comando cria nova reconciliação verificada. Nenhum lado recebe alteração isolada.</p>
    </section>
  </main>
}
