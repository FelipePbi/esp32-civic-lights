import { describe, expect, it } from 'vitest'
import { routeFromPathname } from './router'

describe('client routing', () => {
  it('recognizes every application route', () => {
    expect(routeFromPathname('/')).toBe('/')
    expect(routeFromPathname('/color')).toBe('/color')
    expect(routeFromPathname('/remote')).toBe('/remote')
    expect(routeFromPathname('/diagnostics')).toBe('/diagnostics')
  })

  it('falls back to Home for unknown paths', () => {
    expect(routeFromPathname('/settings')).toBe('/')
    expect(routeFromPathname('/animations')).toBe('/')
  })
})
