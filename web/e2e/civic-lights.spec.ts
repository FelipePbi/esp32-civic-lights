import { expect, test } from '@playwright/test'
import AxeBuilder from '@axe-core/playwright'
import { installControllerMock } from './mockController'

const viewports = [
  { width: 390, height: 844 },
  { width: 393, height: 852 },
  { width: 402, height: 874 },
  { width: 430, height: 932 },
]

test.beforeEach(async ({ page }) => installControllerMock(page))

test.describe('Civic Lights responsive shell', () => {
  for (const viewport of viewports) {
    test(`Home fits ${viewport.width}x${viewport.height} without scroll`, async ({ page }) => {
      await page.setViewportSize(viewport)
      await page.goto('/')
      await expect(page.getByRole('heading', { name: /CIVIC LIGHTS/ })).toBeVisible()
      await expect(page.getByTestId('headlight-left')).toBeVisible()
      await expect(page.getByTestId('headlight-right')).toBeVisible()
      await expect(page.getByText('SINCRONIZADO', { exact: true })).toBeVisible()
      await expect(page.getByRole('link', { name: /COR E BRILHO/ })).toBeVisible()
      await expect(page.getByRole('link', { name: /CONTROLE REMOTO/ })).toBeVisible()
      await expect(page.getByRole('link', { name: /DIAGNÓSTICO/ })).toBeVisible()

      const dimensions = await page.evaluate(() => ({
        innerWidth,
        innerHeight,
        scrollWidth: document.documentElement.scrollWidth,
        scrollHeight: document.documentElement.scrollHeight,
      }))
      expect(dimensions.scrollWidth).toBeLessThanOrEqual(dimensions.innerWidth)
      expect(dimensions.scrollHeight).toBeLessThanOrEqual(dimensions.innerHeight + 1)
    })
  }
})

test('Home contains only status, quick access and navigation', async ({ page }) => {
  await page.setViewportSize({ width: 402, height: 874 })
  await page.goto('/')
  await expect(page.getByText('CONTROLE LOCAL')).toBeVisible()
  await expect(page.getByRole('button', { name: /Branco real/ })).toBeVisible()
  await expect(page.getByRole('button', { name: /Favorita/ })).toBeVisible()
  await expect(page.getByText('Firmware', { exact: true })).toHaveCount(0)
  await expect(page.getByText('192.168.4.1')).toHaveCount(0)
  await expect(page.getByText('AÇÕES A / B / C / D')).toHaveCount(0)
  await expect(page.locator('footer')).toHaveCount(0)
})

test('client navigation preserves one SPA and every route renders', async ({ page }) => {
  await page.setViewportSize({ width: 402, height: 874 })
  await page.goto('/')

  await page.getByRole('link', { name: /COR E BRILHO/ }).click()
  await expect(page).toHaveURL(/\/color$/)
  await expect(page.getByRole('heading', { name: 'COR E BRILHO' })).toBeVisible()
  await expect(page.getByLabel('Saturação e intensidade da cor')).toBeVisible()
  await page.getByRole('button', { name: 'Voltar' }).click()

  await page.getByRole('link', { name: /CONTROLE REMOTO/ }).click()
  await expect(page).toHaveURL(/\/remote$/)
  await expect(page.getByRole('heading', { name: 'CONTROLE REMOTO' })).toBeVisible()
  await expect(page.getByText('AÇÕES A / B / C / D')).toBeVisible()
  await page.getByRole('button', { name: 'Voltar' }).click()

  await page.getByRole('link', { name: /DIAGNÓSTICO/ }).click()
  await expect(page).toHaveURL(/\/diagnostics$/)
  await expect(page.getByRole('heading', { name: 'DIAGNÓSTICO' })).toBeVisible()
  await expect(page.getByRole('button', { name: /Ressincronizar conjunto/ })).toBeVisible()
})

test('direct trailing-slash route keeps assets and back does not reopen the page', async ({ page }) => {
  await page.goto('/color/')
  await expect(page.getByRole('heading', { name: 'COR E BRILHO' })).toBeVisible()
  await expect(page.locator('link[rel="manifest"]')).toHaveAttribute('href', '/manifest.webmanifest')
  await page.getByRole('button', { name: 'Voltar' }).click()
  await expect(page).toHaveURL(/\/$/)
  await expect(page.getByRole('heading', { name: /CIVIC LIGHTS/ })).toBeVisible()
  await page.goBack()
  await expect(page).not.toHaveURL(/\/color\/?$/)
})

test('quick actions report applying instead of claiming immediate success', async ({ page }) => {
  await page.goto('/?hold=1')
  await page.getByRole('button', { name: /Branco real/ }).click()
  await expect(page.getByRole('button', { name: /Aplicando/ })).toBeDisabled()
})

test('quick action reports Applied only after verified snapshot', async ({ page }) => {
  await page.goto('/')
  await page.getByRole('button', { name: /Branco real/ }).click()
  await expect(page.getByRole('button', { name: /Aplicado/ })).toBeVisible()
})

test('superseded generation never reports stale action as Applied', async ({ page }) => {
  await page.goto('/?supersede=1')
  await page.getByRole('button', { name: /Branco real/ }).click()
  await expect(page.getByRole('button', { name: /Aplicando/ })).toBeHidden()
  await expect(page.getByRole('button', { name: /Branco real/ })).toBeVisible()
  await expect(page.getByRole('button', { name: /Aplicado/ })).toHaveCount(0)
  await expect(page.getByTestId('headlight-left').locator('.headlight-visual')).toHaveCSS('--lamp-color', '#006EFF')
})

test('Favorite and Save Favorite preserve REST payloads', async ({ page }) => {
  await page.goto('/?hold=1')
  const applyRequest = page.waitForRequest(request => request.url().endsWith('/api/v1/state') && request.method() === 'PUT')
  await page.getByRole('button', { name: /Favorita/ }).click()
  expect((await applyRequest).postDataJSON()).toEqual({ mode: 'rgb', r: 145, g: 28, b: 202, brightness: 160 })

  const saveRequest = page.waitForRequest(request => request.url().endsWith('/api/v1/presets/favorite') && request.method() === 'PUT')
  await page.getByRole('button', { name: /Salvar cor atual como favorita/ }).click()
  expect((await saveRequest).postDataJSON()).toEqual({ mode: 'rgb', r: 145, g: 28, b: 202, brightness: 160 })
})

test('diagnostic Resync uses Group Controller endpoint', async ({ page }) => {
  await page.goto('/diagnostics?hold=1')
  const request = page.waitForRequest(value => value.url().endsWith('/api/v1/resync') && value.method() === 'POST')
  await page.getByRole('button', { name: /Ressincronizar conjunto/ }).click()
  await request
  await expect(page.getByRole('button', { name: /Ressincronizando/ })).toBeDisabled()
})

test('Button D editor persists RGB config through remote endpoint', async ({ page }) => {
  await page.goto('/remote')
  await page.getByLabel('AÇÃO DO BOTÃO D').selectOption('rgb')
  await page.locator('.remote-color-well input[type="color"]').fill('#8000ff')
  await page.locator('.remote-brightness input[type="range"]').fill('80')
  const request = page.waitForRequest(value =>
    value.url().endsWith('/api/v1/remote/button4') && value.method() === 'PUT')
  await page.getByRole('button', { name: /Salvar botão D/ }).click()
  expect((await request).postDataJSON()).toEqual({
    type: 'rgb', r: 128, g: 0, b: 255, brightness: 80,
  })
})

test('Police speed persists through dedicated remote endpoint', async ({ page }) => {
  await page.goto('/remote')
  await page.getByLabel('Velocidade Police').selectOption('very_fast')
  const request = page.waitForRequest(value =>
    value.url().endsWith('/api/v1/remote/police') && value.method() === 'PUT')
  await page.locator('.remote-police-editor').getByRole('button', { name: 'SALVAR' }).click()
  expect((await request).postDataJSON()).toEqual({ speed: 'very_fast' })
  await expect(page.getByLabel('Velocidade Police')).toHaveValue('very_fast')
})

test('indicator self-test uses the bounded hardware endpoint', async ({ page }) => {
  await page.goto('/remote')
  const request = page.waitForRequest(value =>
    value.url().endsWith('/api/v1/indicator/test') && value.method() === 'POST')
  await page.getByRole('button', { name: /TESTAR LED INDICADOR/ }).click()
  await request
  await expect(page.getByRole('button', { name: /LED EM TESTE/ })).toBeDisabled()
})

for (const route of ['/', '/color', '/remote', '/diagnostics']) {
  test(`WCAG A/AA automated audit passes at ${route}`, async ({ page }) => {
    await page.setViewportSize({ width: 402, height: 874 })
    await page.goto(route)
    const results = await new AxeBuilder({ page })
      .withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'])
      .analyze()
    expect(results.violations).toEqual([])
  })
}

test('LEFT and RIGHT render independent observed colors', async ({ page }) => {
  await page.goto('/?scenario=split')
  const left = page.getByTestId('headlight-left').locator('.headlight-visual')
  const right = page.getByTestId('headlight-right').locator('.headlight-visual')
  await expect(left).toHaveCSS('--lamp-color', '#FF1414')
  await expect(right).toHaveCSS('--lamp-color', '#006EFF')
  await expect(left).toHaveClass(/illuminated/)
  await expect(right).toHaveClass(/illuminated/)
})

test('WHITE uses cold white on both sides', async ({ page }) => {
  await page.goto('/?scenario=white')
  await expect(page.getByTestId('headlight-left').locator('.headlight-visual')).toHaveCSS('--lamp-color', '#F4F7FF')
  await expect(page.getByTestId('headlight-right').locator('.headlight-visual')).toHaveCSS('--lamp-color', '#F4F7FF')
})

test('reconnect darkens only affected side', async ({ page }) => {
  await page.goto('/?scenario=reconnect')
  await expect(page.getByTestId('headlight-left').locator('.headlight-visual')).toHaveClass(/illuminated/)
  const right = page.getByTestId('headlight-right').locator('.headlight-visual')
  await expect(right).toHaveClass(/dark/)
  await expect(right).toHaveCSS('--glow-strength', '0')
  await expect(page.getByTestId('headlight-right')).toContainText('Reconectando')
})

test('unknown state is announced without invented color and both offline show disconnected group', async ({ page }) => {
  await page.goto('/?scenario=unknown')
  await expect(page.getByTestId('headlight-left').locator('.headlight-visual')).toHaveAttribute('aria-label', /sem iluminação confirmada/)
  await expect(page.getByTestId('headlight-left').locator('.headlight-visual')).not.toHaveAttribute('aria-label', /#73777F/)

  const offlinePage = await page.context().newPage()
  await installControllerMock(offlinePage)
  await offlinePage.goto('/?scenario=offline')
  await expect(offlinePage.getByText('DESCONECTADO', { exact: true })).toBeVisible()
  await offlinePage.close()
})

for (const route of ['/color', '/remote', '/diagnostics']) {
  test(`direct navigation works at ${route}`, async ({ page }) => {
    await page.goto(route)
    await expect(page.getByRole('button', { name: 'Voltar' })).toBeVisible()
  })
}

test('captures iPhone 16 Pro route references', async ({ page }) => {
  await page.setViewportSize({ width: 402, height: 874 })
  const captures = [
    ['/', 'home.png'],
    ['/color', 'color.png'],
    ['/remote', 'remote.png'],
    ['/diagnostics', 'diagnostics.png'],
  ] as const
  for (const [route, file] of captures) {
    await page.goto(route)
    await expect(page.locator('main')).toBeVisible()
    await page.screenshot({ path: `../docs/screenshots/${file}`, fullPage: true })
  }
})
