export type IconName =
  | 'palette'
  | 'sparkles'
  | 'pulse'
  | 'wifi'
  | 'signal'
  | 'chevron'
  | 'star'
  | 'sync'
  | 'back'
  | 'sun'
  | 'save'
  | 'remote'
  | 'check'

export function Icon({ name, size = 20 }: { name: IconName; size?: number }) {
  const common = { fill: 'none', stroke: 'currentColor', strokeWidth: 1.8, strokeLinecap: 'round' as const, strokeLinejoin: 'round' as const }
  const paths: Record<IconName, React.ReactNode> = {
    palette: <><path {...common} d="M12 3a9 9 0 1 0 0 18h1.3a2 2 0 0 0 1.6-3.2 1.8 1.8 0 0 1 1.45-2.86H18A3 3 0 0 0 21 12a9 9 0 0 0-9-9Z" /><circle cx="7.5" cy="10" r="1" fill="currentColor" /><circle cx="10.5" cy="6.8" r="1" fill="currentColor" /><circle cx="15" cy="7.5" r="1" fill="currentColor" /></>,
    sparkles: <><path {...common} d="m12 3 1.2 3.3L16.5 7.5l-3.3 1.2L12 12l-1.2-3.3-3.3-1.2 3.3-1.2L12 3Z" /><path {...common} d="m18 13 .8 2.2 2.2.8-2.2.8L18 19l-.8-2.2L15 16l2.2-.8L18 13ZM6 14l.9 2.1L9 17l-2.1.9L6 20l-.9-2.1L3 17l2.1-.9L6 14Z" /></>,
    pulse: <path {...common} d="M3 12h4l2-6 4 12 2.2-6H21" />,
    wifi: <><path {...common} d="M4.9 9.5a10.4 10.4 0 0 1 14.2 0M7.8 12.6a6.2 6.2 0 0 1 8.4 0M10.5 15.7a2.2 2.2 0 0 1 3 0" /><circle cx="12" cy="19" r="1" fill="currentColor" /></>,
    signal: <><path {...common} d="M5 18v-3M10 18v-6M15 18V9M20 18V5" /></>,
    chevron: <path {...common} d="m9 5 7 7-7 7" />,
    star: <path {...common} d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9L12 3Z" />,
    sync: <><path {...common} d="M20 7h-5V2" /><path {...common} d="M4.8 9A8 8 0 0 1 18.6 6L20 7M4 17h5v5" /><path {...common} d="M19.2 15A8 8 0 0 1 5.4 18L4 17" /></>,
    back: <><path {...common} d="m15 18-6-6 6-6" /><path {...common} d="M9 12h11" /></>,
    sun: <><circle {...common} cx="12" cy="12" r="3.5" /><path {...common} d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4" /></>,
    save: <><path {...common} d="M5 3h12l2 2v16H5V3Z" /><path {...common} d="M8 3v6h8V3M8 21v-7h8v7" /></>,
    remote: <><path {...common} d="M8 3h8a3 3 0 0 1 3 3v12a3 3 0 0 1-3 3H8a3 3 0 0 1-3-3V6a3 3 0 0 1 3-3Z" /><circle {...common} cx="9" cy="9" r="1.5" /><circle {...common} cx="15" cy="9" r="1.5" /><circle {...common} cx="9" cy="15" r="1.5" /><circle {...common} cx="15" cy="15" r="1.5" /></>,
    check: <path {...common} d="m5 12 4 4L19 6" />,
  }
  return <svg className="icon" width={size} height={size} viewBox="0 0 24 24" aria-hidden="true">{paths[name]}</svg>
}
