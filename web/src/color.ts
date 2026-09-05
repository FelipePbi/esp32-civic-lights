export interface RGB { r: number; g: number; b: number }
export interface HSV { h: number; s: number; v: number }

export function hsvToRgb({ h, s, v }: HSV): RGB {
  const hue = ((h % 360) + 360) % 360
  const chroma = v * s
  const x = chroma * (1 - Math.abs((hue / 60) % 2 - 1))
  const m = v - chroma
  const channels = hue < 60 ? [chroma, x, 0] : hue < 120 ? [x, chroma, 0] :
    hue < 180 ? [0, chroma, x] : hue < 240 ? [0, x, chroma] :
      hue < 300 ? [x, 0, chroma] : [chroma, 0, x]
  return { r: Math.round((channels[0] + m) * 255), g: Math.round((channels[1] + m) * 255), b: Math.round((channels[2] + m) * 255) }
}

export function rgbToHsv({ r, g, b }: RGB): HSV {
  const [red, green, blue] = [r, g, b].map(channel => channel / 255)
  const max = Math.max(red, green, blue)
  const min = Math.min(red, green, blue)
  const delta = max - min
  let h = 0
  if (delta !== 0) {
    if (max === red) h = 60 * (((green - blue) / delta) % 6)
    else if (max === green) h = 60 * ((blue - red) / delta + 2)
    else h = 60 * ((red - green) / delta + 4)
  }
  return { h: h < 0 ? h + 360 : h, s: max === 0 ? 0 : delta / max, v: max }
}

export const rgbToHex = ({ r, g, b }: RGB) => `#${[r, g, b].map(value => value.toString(16).padStart(2, '0')).join('').toUpperCase()}`
