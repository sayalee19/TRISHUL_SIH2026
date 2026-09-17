export type GasKind = 'CO' | 'CH₄' | 'O₂'
export type AlarmState = 'normal' | 'alert'
export type NodeStatus = 'online' | 'degraded' | 'silent'
export type RouteMode = 'primary' | 'fallback'
export type SeismicState = 'flat' | 'spike'
export type PersonnelStatus = 'normal' | 'alert' | 'offline'

export type GasChip = {
  kind: GasKind
  state: AlarmState
}

export type VerifiedReading = {
  kind: GasKind
  value: string
  surveyedAgo: string
  baselineDeviation?: string
}

export type SaarthiNode = {
  id: string
  zone: string
  positionIndex: number
  status: NodeStatus
  lastReceivedSec: number
  mq: AlarmState
  seismic: SeismicState
  routing: RouteMode
  hop: string
  ambient?: GasChip[]
}

export type Worker = {
  id: string
  name: string
  role: string
  status: PersonnelStatus
  time: string
  zone: string
  battery: number
  nearestNodeId: string
  mukut: GasChip[]
  mukutAgo: string
  rath?: VerifiedReading
}

export const saarthiNodes: SaarthiNode[] = [
  { id: 'S-01', zone: 'Portal', positionIndex: 1, status: 'online', lastReceivedSec: 8, mq: 'normal', seismic: 'flat', routing: 'primary', hop: 'N+1 → S-02' },
  { id: 'S-02', zone: 'Main gate', positionIndex: 2, status: 'online', lastReceivedSec: 11, mq: 'normal', seismic: 'flat', routing: 'primary', hop: 'N+1 → S-03' },
  { id: 'S-03', zone: 'Belt road', positionIndex: 3, status: 'degraded', lastReceivedSec: 47, mq: 'normal', seismic: 'flat', routing: 'fallback', hop: 'N+2 → S-05' },
  { id: 'S-04', zone: 'Panel 4', positionIndex: 4, status: 'online', lastReceivedSec: 9, mq: 'normal', seismic: 'flat', routing: 'primary', hop: 'N+1 → S-05' },
  { id: 'S-05', zone: 'Crosscut 1', positionIndex: 5, status: 'silent', lastReceivedSec: 268, mq: 'normal', seismic: 'flat', routing: 'primary', hop: '—' },
  { id: 'S-06', zone: 'Return airway', positionIndex: 6, status: 'online', lastReceivedSec: 14, mq: 'alert', seismic: 'flat', routing: 'primary', hop: 'N+1 → S-07', ambient: [
    { kind: 'CO', state: 'normal' },
    { kind: 'CH₄', state: 'alert' },
    { kind: 'O₂', state: 'normal' },
  ] },
  { id: 'S-07', zone: 'Panel 5', positionIndex: 7, status: 'online', lastReceivedSec: 6, mq: 'alert', seismic: 'spike', routing: 'fallback', hop: 'N+2 → S-09', ambient: [
    { kind: 'CO', state: 'normal' },
    { kind: 'CH₄', state: 'alert' },
    { kind: 'O₂', state: 'normal' },
  ] },
  { id: 'S-08', zone: 'Dip heading', positionIndex: 8, status: 'online', lastReceivedSec: 18, mq: 'normal', seismic: 'flat', routing: 'fallback', hop: 'N+2 → S-10' },
  { id: 'S-09', zone: 'Junction 2', positionIndex: 9, status: 'online', lastReceivedSec: 12, mq: 'normal', seismic: 'flat', routing: 'primary', hop: 'N+1 → S-10' },
  { id: 'S-10', zone: 'Haulage', positionIndex: 10, status: 'degraded', lastReceivedSec: 39, mq: 'normal', seismic: 'flat', routing: 'fallback', hop: 'N+2 → S-01' },
  { id: 'S-11', zone: 'Ramp 1', positionIndex: 11, status: 'online', lastReceivedSec: 21, mq: 'normal', seismic: 'flat', routing: 'primary', hop: 'N+1 → S-01' },
]

export const workers: Worker[] = [
  {
    id: 'MIN-2041',
    name: 'Ramesh Kumar',
    role: 'Cutting Crew',
    status: 'normal',
    time: '09:42:18',
    zone: 'Sector A · Panel 4',
    battery: 64,
    nearestNodeId: 'S-04',
    mukutAgo: '8 sec ago',
    mukut: [
      { kind: 'CO', state: 'normal' },
      { kind: 'CH₄', state: 'normal' },
      { kind: 'O₂', state: 'normal' },
    ],
    rath: { kind: 'CH₄', value: '0.4%', surveyedAgo: '12 min ago', baselineDeviation: 'within baseline' },
  },
  {
    id: 'MIN-2088',
    name: 'Suresh Pradhan',
    role: 'Roof Bolting',
    status: 'alert',
    time: '09:41:54',
    zone: 'Sector A · Panel 5',
    battery: 64,
    nearestNodeId: 'S-07',
    mukutAgo: '6 sec ago',
    mukut: [
      { kind: 'CO', state: 'normal' },
      { kind: 'CH₄', state: 'alert' },
      { kind: 'O₂', state: 'normal' },
    ],
    rath: { kind: 'CH₄', value: '0.8%', surveyedAgo: '4 min ago', baselineDeviation: '+0.3% vs baseline' },
  },
  {
    id: 'MIN-2112',
    name: 'Anita Devi',
    role: 'Survey Team',
    status: 'normal',
    time: '09:42:06',
    zone: 'Sector B · Junction 2',
    battery: 91,
    nearestNodeId: 'S-09',
    mukutAgo: '12 sec ago',
    mukut: [
      { kind: 'CO', state: 'normal' },
      { kind: 'CH₄', state: 'normal' },
      { kind: 'O₂', state: 'normal' },
    ],
  },
  {
    id: 'MIN-2157',
    name: 'Mohan Singh',
    role: 'Haulage Crew',
    status: 'offline',
    time: '09:38:22',
    zone: 'Sector C · Ramp 1',
    battery: 18,
    nearestNodeId: 'S-11',
    mukutAgo: '4 min ago',
    mukut: [
      { kind: 'CO', state: 'normal' },
      { kind: 'CH₄', state: 'normal' },
      { kind: 'O₂', state: 'normal' },
    ],
  },
]

export const events = [
  ['09:42:18', 'Mukut CH₄ alert · Panel 5', 'MIN-2088', 'alert'],
  ['09:42:12', 'Saarthi S-07 routing fallback N+2', 'S-07', 'info'],
  ['09:41:54', 'Zone ambient agrees with Mukut alert', 'S-07', 'alert'],
  ['09:40:31', 'Rath survey stored · CH₄ 0.8%', 'RATH-07', 'info'],
] as const

export function nodeById(id: string) {
  return saarthiNodes.find((node) => node.id === id)
}

export function zoneAmbient(node: SaarthiNode): GasChip[] {
  if (node.ambient) return node.ambient
  return [
    { kind: 'CO', state: node.mq },
    { kind: 'CH₄', state: node.mq },
    { kind: 'O₂', state: 'normal' },
  ]
}

export function zoneIsAlert(node: SaarthiNode) {
  return node.mq === 'alert' || node.seismic === 'spike'
}

export function meshSummary(nodes: SaarthiNode[]) {
  const ordered = [...nodes].sort((a, b) => a.positionIndex - b.positionIndex)
  const online = ordered.filter((n) => n.status !== 'silent')
  const fallback = online.filter((n) => n.routing === 'fallback').length
  const zonesAlert = ordered.filter(zoneIsAlert).length
  const oldest = online.reduce((max, n) => Math.max(max, n.lastReceivedSec), 0)
  return {
    ordered,
    total: ordered.length,
    onlineCount: online.length,
    silentCount: ordered.length - online.length,
    degradedCount: ordered.filter((n) => n.status === 'degraded').length,
    fallback,
    zonesAlert,
    zonesNominal: ordered.length - zonesAlert,
    oldestSec: oldest,
    meshHealthy: online.length === ordered.length && fallback === 0,
  }
}

export function formatAge(seconds: number) {
  if (seconds < 60) return `${seconds} sec ago`
  const minutes = Math.floor(seconds / 60)
  return `${minutes} min ago`
}

export function chipsAgree(a: GasChip[], b: GasChip[]) {
  return a.every((chip) => b.find((other) => other.kind === chip.kind)?.state === chip.state)
}
