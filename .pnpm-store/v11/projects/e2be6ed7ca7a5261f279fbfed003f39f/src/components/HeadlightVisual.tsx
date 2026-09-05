import { useId, type CSSProperties } from 'react'
import type { HeadlightVisualState } from '../headlight'

interface Props extends HeadlightVisualState {
  side: 'left' | 'right'
  compact?: boolean
}

export function HeadlightVisual({ side, color, brightness, visualOpacity, glowStrength, illuminated, connectionState, mode, compact = false }: Props) {
  const uid = useId().replace(/:/g, '')
  const shell = `shell-${uid}`
  const lens = `lens-${uid}`
  const mask = `signature-${uid}`
  const glow = `glow-${uid}`
  const style = {
    '--lamp-color': color,
    '--lamp-opacity': visualOpacity,
    '--glow-strength': glowStrength,
  } as CSSProperties
  const stateLabel = illuminated
    ? `${mode === 'white' ? 'branco real' : color}, brilho ${Math.round(brightness / 255 * 100)}%`
    : connectionState === 'reconnecting' ? 'reconectando, iluminação não confirmada'
      : connectionState === 'connecting' ? 'conectando, aguardando estado'
        : 'sem iluminação confirmada'

  return <div className={`headlight-visual ${compact ? 'compact' : ''} ${illuminated ? 'illuminated' : 'dark'} state-${connectionState}`}
    style={style} role="img" aria-label={`Farol ${side === 'left' ? 'esquerdo' : 'direito'}: ${stateLabel}`}>
    <svg viewBox="0 0 240 104" focusable="false">
      <defs>
        <linearGradient id={shell} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#34383e" />
          <stop offset=".42" stopColor="#111317" />
          <stop offset="1" stopColor="#050607" />
        </linearGradient>
        <linearGradient id={lens} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#88909b" stopOpacity=".34" />
          <stop offset=".35" stopColor="#22262c" stopOpacity=".65" />
          <stop offset="1" stopColor="#050607" stopOpacity=".96" />
        </linearGradient>
        <filter id={glow} x="-50%" y="-80%" width="200%" height="260%">
          <feGaussianBlur stdDeviation="7" />
        </filter>
        <mask id={mask}>
          <rect width="240" height="104" fill="black" />
          <path d="M29 61C54 22 111 13 196 31C208 34 216 40 221 47C196 42 176 43 157 47C132 52 112 65 91 72C69 80 47 76 29 61Z" fill="white" />
          <path d="M55 68C91 70 111 51 143 45C164 41 186 41 211 48" fill="none" stroke="white" strokeWidth="5" strokeLinecap="round" />
          <path d="M91 77C126 64 152 55 198 55" fill="none" stroke="white" strokeWidth="3" strokeLinecap="round" />
        </mask>
      </defs>
      <g transform={side === 'right' ? 'translate(240 0) scale(-1 1)' : undefined}>
        <path className="headlight-shadow" d="M12 70C26 34 64 14 115 10C155 7 197 18 228 42L218 69C175 87 98 96 42 88C26 85 16 78 12 70Z" />
        <path className="headlight-shell" fill={`url(#${shell})`} d="M14 66C29 34 66 17 115 13C157 10 196 20 225 43L214 67C172 83 100 91 45 84C29 82 19 75 14 66Z" />
        <path className="headlight-bezel" d="M25 64C43 37 74 24 118 20C155 17 190 26 215 44L207 60C165 72 105 80 54 76C40 75 30 70 25 64Z" />
        <path className="headlight-lens" fill={`url(#${lens})`} d="M29 61C49 38 79 28 119 25C154 22 185 29 208 44C181 40 160 42 139 48C115 55 95 70 70 72C52 74 38 69 29 61Z" />
        <circle className="projector-ring" cx="82" cy="52" r="19" />
        <circle className="projector-core" cx="82" cy="52" r="12" />
        <rect className="signature-glow" width="240" height="104" fill="var(--lamp-color)" mask={`url(#${mask})`} filter={`url(#${glow})`} />
        <rect className="signature-light" width="240" height="104" fill="var(--lamp-color)" mask={`url(#${mask})`} />
        <path className="lens-highlight" d="M42 49C79 24 145 18 201 40" />
        <path className="housing-line" d="M18 67C67 91 164 79 215 62" />
      </g>
    </svg>
  </div>
}
