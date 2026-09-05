import { useEffect, useRef, useState, type KeyboardEvent, type PointerEvent } from 'react'
import { hsvToRgb, rgbToHsv, type HSV, type RGB } from '../color'

interface Props {
  value: RGB
  onChange: (color: RGB, final: boolean) => void
}

const clamp = (value: number) => Math.max(0, Math.min(1, value))

export function ColorPicker({ value, onChange }: Props) {
  const [hsv, setHsv] = useState<HSV>(() => rgbToHsv(value))
  const hsvRef = useRef(hsv)
  const dragging = useRef(false)
  useEffect(() => { hsvRef.current = hsv }, [hsv])
  useEffect(() => {
    if (!dragging.current) setHsv(rgbToHsv(value))
  }, [value.r, value.g, value.b])

  const commit = (next: HSV, final: boolean) => {
    hsvRef.current = next
    setHsv(next)
    onChange(hsvToRgb(next), final)
  }

  const fromPointer = (event: PointerEvent<HTMLDivElement>, final: boolean) => {
    const rect = event.currentTarget.getBoundingClientRect()
    commit({ ...hsvRef.current, s: clamp((event.clientX - rect.left) / rect.width),
      v: clamp(1 - (event.clientY - rect.top) / rect.height) }, final)
  }

  const keyAdjust = (event: KeyboardEvent<HTMLDivElement>) => {
    const step = event.shiftKey ? .1 : .02
    let { s, v } = hsvRef.current
    if (event.key === 'ArrowLeft') s -= step
    else if (event.key === 'ArrowRight') s += step
    else if (event.key === 'ArrowDown') v -= step
    else if (event.key === 'ArrowUp') v += step
    else return
    event.preventDefault()
    commit({ ...hsvRef.current, s: clamp(s), v: clamp(v) }, true)
  }

  return <div className="picker">
    <div className="sv-plane" role="slider" tabIndex={0} aria-label="Saturação e intensidade da cor"
      aria-valuetext={`${Math.round(hsv.s * 100)}% saturação, ${Math.round(hsv.v * 100)}% intensidade`}
      style={{ backgroundColor: `hsl(${hsv.h} 100% 50%)` }} onKeyDown={keyAdjust}
      onPointerDown={event => { dragging.current = true; event.currentTarget.setPointerCapture(event.pointerId); fromPointer(event, false) }}
      onPointerMove={event => { if (dragging.current) fromPointer(event, false) }}
      onPointerUp={event => { fromPointer(event, true); dragging.current = false; event.currentTarget.releasePointerCapture(event.pointerId) }}
      onPointerCancel={() => { onChange(hsvToRgb(hsvRef.current), true); dragging.current = false }}>
      <span className="picker-target" style={{ left: `${hsv.s * 100}%`, top: `${(1 - hsv.v) * 100}%` }} />
    </div>
    <label className="hue-label"><span>Espectro</span>
      <input className="hue" type="range" min="0" max="360" value={Math.round(hsv.h)}
        aria-label="Matiz" onInput={event => commit({ ...hsvRef.current, h: Number(event.currentTarget.value) }, false)}
        onPointerUp={() => onChange(hsvToRgb(hsvRef.current), true)}
        onKeyUp={() => onChange(hsvToRgb(hsvRef.current), true)} />
    </label>
  </div>
}
