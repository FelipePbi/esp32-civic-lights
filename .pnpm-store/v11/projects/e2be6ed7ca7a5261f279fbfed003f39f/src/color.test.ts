import { describe, expect, it } from 'vitest'
import { hsvToRgb, rgbToHex, rgbToHsv } from './color'

describe('color conversion', () => {
  it('converts primary colors and formats hex', () => {
    expect(hsvToRgb({ h: 0, s: 1, v: 1 })).toEqual({ r: 255, g: 0, b: 0 })
    expect(hsvToRgb({ h: 120, s: 1, v: 1 })).toEqual({ r: 0, g: 255, b: 0 })
    expect(rgbToHex({ r: 255, g: 0, b: 80 })).toBe('#FF0050')
  })

  it('round trips an arbitrary RGB color within one channel value', () => {
    const input = { r: 126, g: 42, b: 211 }
    const output = hsvToRgb(rgbToHsv(input))
    expect(Math.abs(output.r - input.r)).toBeLessThanOrEqual(1)
    expect(Math.abs(output.g - input.g)).toBeLessThanOrEqual(1)
    expect(Math.abs(output.b - input.b)).toBeLessThanOrEqual(1)
  })
})
