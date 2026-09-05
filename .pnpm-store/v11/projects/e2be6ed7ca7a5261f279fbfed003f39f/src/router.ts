export type AppRoute = '/' | '/color' | '/remote' | '/diagnostics'

const ROUTES = new Set<AppRoute>(['/', '/color', '/remote', '/diagnostics'])

export function routeFromPathname(pathname: string): AppRoute {
  const normalized = pathname.length > 1 ? pathname.replace(/\/+$/, '') : pathname
  return ROUTES.has(normalized as AppRoute) ? normalized as AppRoute : '/'
}
