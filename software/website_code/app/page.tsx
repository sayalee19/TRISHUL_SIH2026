'use client'

import { useMemo, useState } from 'react'
import {
  Activity, UserRound, AlertTriangle, ArrowDown, ArrowRight, Bell, Camera, Check, Clock3, HardHat, History, LockKeyhole, MessageSquare, Radio, RefreshCw, Search, Shield, Sparkles, Thermometer, TriangleAlert, Users, Wifi, Zap,
} from 'lucide-react'
import {
  chipsAgree,
  events,
  formatAge,
  meshSummary,
  nodeById,
  saarthiNodes,
  workers,
  zoneAmbient,
  type GasChip,
  type SaarthiNode,
  type Worker,
} from '@/lib/telemetry'

const mesh = meshSummary(saarthiNodes)

function StatusDot({ status }: { status: string }) {
  return <span className={`status-dot ${status}`} aria-label={status} />
}

function Metric({ icon: Icon, label, value, tone = '' }: { icon: typeof Activity; label: string; value: string; tone?: string }) {
  return <div className="metric"><Icon size={17} className={tone} /><div><strong>{value}</strong><span>{label}</span></div></div>
}

function GasChips({ chips }: { chips: GasChip[] }) {
  return (
    <div className="gas-chips" >
      {chips.map((chip) => (
        <span key={chip.kind} className={`gas-chip ${chip.state}`}>
          <b>{chip.kind}</b>
          {chip.state === 'alert' ? 'Alert' : 'Normal'}
        </span>
      ))}
    </div>
  )
}

function SeismicMark({ state }: { state: SaarthiNode['seismic'] }) {
  const points = state === 'spike' ? '0 10 8 9 12 2 16 11 24 10' : '0 10 8 10 16 10 24 10'
  return (
    <svg className={`seismic-mark ${state}`} viewBox="0 0 24 14" aria-hidden="true">
      <polyline fill="none" stroke="currentColor" strokeWidth="1.6" points={points} />
    </svg>
  )
}

function SaarthiStrip({ selectedNodeId, onSelectNode }: { selectedNodeId?: string; onSelectNode: (node: SaarthiNode) => void }) {
  return (
    <div className="node-strip" role="list" aria-label="Saarthi nodes in tunnel order">
      {mesh.ordered.map((node, index) => (
        <button
          key={node.id}
          type="button"
          className={`node-tile ${node.status} ${node.mq} ${selectedNodeId === node.id ? 'selected' : ''}`}
          onClick={() => onSelectNode(node)}
          aria-label={`${node.id} ${node.zone} ${node.status} MQ ${node.mq} routing ${node.routing}`}
          aria-pressed={selectedNodeId === node.id}
        >
          <div className="node-tile-head">
            <strong>{node.id}</strong>
            <span className={`node-status ${node.status}`}>{node.status}</span>
          </div>
          <p className="node-zone">{node.zone}</p>
          <div className="node-signals">
            <span className={`mq-pill ${node.mq}`}>MQ {node.mq}</span>
            <span className={`seismic-pill ${node.seismic}`}>
              <SeismicMark state={node.seismic} />
              {node.seismic === 'spike' ? 'spike' : 'quiet'}
            </span>
          </div>
          <div className={`route-row ${node.routing}`}>
            <ArrowRight size={12} />
            {node.status === 'silent' ? 'no route' : node.routing === 'primary' ? `primary ${node.hop}` : `fallback ${node.hop}`}
          </div>
          {index < mesh.ordered.length - 1 && <i className="node-link" aria-hidden="true" />}
        </button>
      ))}
    </div>
  )
}

function corroboration(worker: Worker, zone: GasChip[]) {
  if (worker.status === 'offline') return 'Mukut last hop is stale — treat as unconfirmed.'
  if (chipsAgree(worker.mukut, zone)) {
    const alerted = worker.mukut.some((c) => c.state === 'alert')
    return alerted
      ? 'Mukut and nearest Saarthi both flag alert — two independent alarm-only sensors agree.'
      : 'Mukut and nearest Saarthi both nominal.'
  }
  return 'Mukut and zone ambient disagree — treat as unverified until Rath or a second hop confirms.'
}

export default function Page() {
  const [filter, setFilter] = useState('all')
  const [selected, setSelected] = useState(workers[1])
  const [incident, setIncident] = useState(false)
  const [acknowledged, setAcknowledged] = useState(false)
  const visibleWorkers = useMemo(() => filter === 'all' ? workers : workers.filter((w) => w.status === filter), [filter])
  const nearest = nodeById(selected.nearestNodeId)
  const ambient = nearest ? zoneAmbient(nearest) : []
  const gasAlert = selected.mukut.some((c) => c.state === 'alert') || ambient.some((c) => c.state === 'alert')

  function selectWorker(worker: Worker) {
    setSelected(worker)
    setAcknowledged(false)
  }

  function selectNode(node: SaarthiNode) {
    const occupant = workers.find((w) => w.nearestNodeId === node.id)
    if (occupant) selectWorker(occupant)
  }

  return <main className="app-shell">
    <header className="topbar">
      <div className="brand"><div className="brand-mark"><Shield size={27} color="#00a6e2"/></div><div><h1>COALMINE COMMAND CENTER</h1><p>Underground Safety &amp; Response Monitoring</p></div></div>
      <div className="headline-metrics">
        <Metric icon={Users} label="Personnel Underground" value="24" tone="cyan" />
        <Metric icon={Activity} label="Zones nominal" value={String(mesh.zonesNominal).padStart(2, '0')} tone="green" />
        <Metric icon={TriangleAlert} label="Zones alert" value={String(mesh.zonesAlert).padStart(2, '0')} tone="red" />
      </div>
      <div className="top-actions"><span className="online-pill"><span className="pulse" /> System Online</span></div>
    </header>

    <section className="status-strip">
      <span><Wifi size={15} className="green" /> Mesh integrity {mesh.onlineCount}/{mesh.total} online</span>
      <span><Radio size={15} className="cyan" /> {mesh.fallback} node{mesh.fallback === 1 ? '' : 's'} on fallback (N+2)</span>
      <span><Clock3 size={15} className="amber" /> Data freshness {formatAge(mesh.oldestSec)}</span>
      <span><TriangleAlert size={15} /> {mesh.zonesAlert} zone{mesh.zonesAlert === 1 ? '' : 's'} flagging gas or seismic</span>
      <span><LockKeyhole size={15} /> Incident mode: {incident ? 'ACTIVE' : 'Standby'}</span>
      <button onClick={() => setIncident(!incident)} className={incident ? 'incident-active' : ''}>{incident ? 'Disable incident mode' : 'Activate incident mode'}</button>
    </section>

    {incident && <section className="incident-console" aria-live="polite"><div className="incident-console-header"><div><span className="eyebrow">Incident mode active</span><h2>Live visual verification</h2></div><span className="recording-pill"><span className="record-dot" /> LIVE · 09:42:18</span></div><div className="camera-grid"><div className="camera-feed rgb-feed"><div className="feed-label"><span><Camera size={14} /> RGB CAMERA · RATH-07</span><b>1080p</b></div><div className="feed-overlay"><span>SECTOR A · PANEL 5</span><span>09:42:18</span></div><div className="feed-scan" /><div className="feed-center"><Camera size={26} /><strong>RGB LIVE STREAM</strong><small>Awaiting connected camera source</small></div></div><div className="camera-feed thermal-feed"><div className="feed-label"><span><Thermometer size={14} /> THERMAL CAMERA · RATH-07</span><b>IR</b></div><div className="thermal-heat heat-one" /><div className="thermal-heat heat-two" /><div className="thermal-heat heat-three" /><div className="feed-overlay"><span>THERMAL RANGE −20°C / 120°C</span><span>09:42:18</span></div><div className="feed-center"><Thermometer size={26} /><strong>THERMAL LIVE STREAM</strong><small>Awaiting connected camera source</small></div></div></div></section>}

  <div className="workspace">
      <aside className="left-panel"><div className="panel-heading"><div><h2>Personnel Overview</h2><p>Headcount reconciliation · underground</p></div>
      <button className="quiet-button"><RefreshCw size={15} /></button></div>
      <div className="search-box"><Search size={16} />
      <input placeholder="Search personnel..." aria-label="Search personnel" /></div>
      <div className="filter-grid">{['all', 'normal', 'alert', 'offline'].map((item) => <button key={item} onClick={() => setFilter(item)} className={filter === item ? 'active' : ''}>{item === 'all' ? 'All personnel' : item[0].toUpperCase() + item.slice(1)}</button>)}</div>
      <div className="reconcile-card"><div><span>Headcount reconciled</span><strong>24 <small>/ 24</small></strong></div><Check size={19} /><div className="progress"><i /></div></div><div className="worker-list">{visibleWorkers.map((worker) => <button key={worker.id} className={`worker-card ${selected.id === worker.id ? 'selected' : ''}`} onClick={() => selectWorker(worker)}>
        <div className="worker-card-head"><span className="avatar"><UserRound size={16} /></span><div><strong>{worker.name}</strong><span>{worker.id} · {worker.role}</span></div><StatusDot status={worker.status} /></div><div className="worker-card-foot"><span><Clock3 size={12} /> {worker.time}</span><span>{worker.zone}</span><b className={worker.status}>{worker.status}</b></div></button>)}</div><button className="history-button"><History size={15} /> View shift history <ArrowDown size={14} /></button></aside>

      <section className="center-panel">
        <div className="section-title">
          <div>
            <h2>Saarthi mesh</h2>
            <p><span className="live-dot" /> Tunnel order by position index · no schematic coordinates</p>
          </div>
          <div className="mesh-badges">
            <span>{mesh.onlineCount} of {mesh.total} online</span>
            <span className={mesh.fallback ? 'warn' : ''}>{mesh.fallback} fallback route{mesh.fallback === 1 ? '' : 's'}</span>
            <span>{mesh.degradedCount} degraded · {mesh.silentCount} silent</span>
          </div>
        </div>
        <div className="mesh-metrics">
          <div><strong>{mesh.zonesNominal}</strong><span>Zones nominal</span></div>
          <div className={mesh.zonesAlert ? 'warn' : ''}><strong>{String(mesh.zonesAlert).padStart(2, '0')}</strong><span>Zones alert</span></div>
          <div className={mesh.fallback ? 'warn' : ''}><strong>{mesh.fallback}</strong><span>Fallback (N+2)</span></div>
          <div><strong>{formatAge(mesh.oldestSec)}</strong><span>Oldest last received</span></div>
        </div>
        <SaarthiStrip selectedNodeId={nearest?.id} onSelectNode={selectNode} />
        <div className="event-panel">
          <div className="event-header"><h3>Recent events</h3><button className="link-button">Open event log <ArrowDown size={13} /></button></div>
          <div className="event-list">{events.map(([time, text, source, tone]) => <div className="event-row" key={time + source}><time>{time}</time><span className={`event-icon ${tone}`}>{tone === 'alert' ? <AlertTriangle size={13} /> : tone === 'info' ? <Radio size={13} /> : <Activity size={13} />}</span><span>{text}</span><b>{source}</b></div>)}</div>
        </div>
      </section>

      <aside className="right-panel" >
        <div className="detail-header" >
          <div>
            <span className="eyebrow">Selected personnel</span>
            <h2>{selected.name}</h2>
            <p>{selected.id} · {selected.zone}</p>
          </div>
          <StatusDot status={selected.status} />
        </div>
        <div className="detail-scroll">
          <div className="freshness"><span><span className="pulse" /> Mukut hop {selected.mukutAgo}</span><span>Nearest node {selected.nearestNodeId}</span></div>

          <div className="signal-card gas-card">
            <div className="card-label"><span><HardHat size={16} /> Mukut reading</span></div>
            <p className="sensor-caption">threshold sensor — alarm only</p>
            <GasChips chips={selected.mukut} />
          </div>

          {nearest && (
            <div className="signal-card gas-card">
              <div className="card-label"><span><Radio size={16} /> Zone ambient</span><span className="muted-label">{nearest.id}</span></div>
              <p className="sensor-caption">fixed infrastructure in this segment — alarm only</p>
              <GasChips chips={ambient} />
              <p className="corroboration">{corroboration(selected, ambient)}</p>
            </div>
          )}

          {selected.rath ? (
            <div className="signal-card verified-card">
              <div className="card-label"><span><Check size={16} /> Last verified reading</span></div>
              <p className="verified-line">{selected.rath.kind} {selected.rath.value} · verified via Rath survey · {selected.rath.surveyedAgo}</p>
              {selected.rath.baselineDeviation && <p className="sensor-caption">{selected.rath.baselineDeviation}</p>}
            </div>
          ) : (
            <div className="signal-card verified-card empty">
              <div className="card-label"><span>Last verified reading</span></div>
              <p className="sensor-caption">No Rath survey on this segment yet — chips above are alarm-only, not calibrated values.</p>
            </div>
          )}

          <div className="signal-card">
            <div className="card-label"><span><Zap size={16} /> Device health</span></div>
            <div className="health-row"><span>Mukut battery</span><strong>{selected.battery}%</strong></div>
            <div className="battery"><i style={{ width: `${selected.battery}%` }} /></div>
            {nearest && (
              <>
                <div className="health-row"><span>Saarthi hop</span><strong>{nearest.status === 'silent' ? 'silent' : nearest.routing === 'primary' ? 'primary N+1' : 'fallback N+2'}</strong></div>
                <p className="sensor-caption">{nearest.id} last received {formatAge(nearest.lastReceivedSec)}</p>
              </>
            )}
          </div>

          {gasAlert && selected.status === 'alert' && (
            <div className={`alert-card ${acknowledged ? 'acknowledged' : ''}`}>
              <div><AlertTriangle size={17} /><strong>{acknowledged ? 'Alert acknowledged' : 'CH₄ alarm — Mukut and S-07'}</strong></div>
            <p>{acknowledged ? 'Commander has taken ownership of this event.' : 'Mukut and nearest Saarthi both flag CH₄. Rath last verified CH₄ 0.8% on this segment 4 min ago.'}</p>
              <button onClick={() => setAcknowledged(true)} disabled={acknowledged}>{acknowledged ? 'Acknowledged' : 'Acknowledge alert'}</button>
            </div>
          )}

          
        </div>
      </aside>
    </div>
    <footer><span>© 2026 CoalMine Command Center · Mine Safety Operations</span><span>Data stream: <b>Online</b> · Protocol v2.4</span></footer>
  </main>
}

function SettingsIcon() { return <Sparkles size={15} /> }
