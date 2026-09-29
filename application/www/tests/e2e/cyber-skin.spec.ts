import { expect, test, type Page, type TestInfo } from "@playwright/test"

const SKIN_STORAGE_KEY = "hbox.webconfig.skin.v1"
const LANGUAGE_STORAGE_KEY = "preferred_language"
const routes = [
  "global",
  "keys",
  "buttons-performance",
  "lighting",
  "firmware",
  "view-logs",
] as const

const viewports = [
  { name: "1440x900", width: 1440, height: 900 },
  { name: "1718x916", width: 1718, height: 916 },
  { name: "1920x1080", width: 1920, height: 1080 },
] as const

async function setStoredPreferences(
  page: Page,
  skin: "classic" | "cyber",
  language: "en" | "zh",
) {
  await page.addInitScript(
    ({ skinKey, languageKey, skinValue, languageValue }) => {
      window.localStorage.setItem(skinKey, skinValue)
      window.localStorage.setItem(languageKey, languageValue)
    },
    {
      skinKey: SKIN_STORAGE_KEY,
      languageKey: LANGUAGE_STORAGE_KEY,
      skinValue: skin,
      languageValue: language,
    },
  )
}

async function assertNoPageOverflow(page: Page) {
  const metrics = await page.evaluate(() => {
    const root = document.documentElement
    const body = document.body
    return {
      rootWidth: [root.scrollWidth, root.clientWidth],
      rootHeight: [root.scrollHeight, root.clientHeight],
      bodyWidth: [body.scrollWidth, body.clientWidth],
      bodyHeight: [body.scrollHeight, body.clientHeight],
    }
  })

  expect(metrics.rootWidth[0]).toBe(metrics.rootWidth[1])
  expect(metrics.rootHeight[0]).toBe(metrics.rootHeight[1])
  expect(metrics.bodyWidth[0]).toBe(metrics.bodyWidth[1])
  expect(metrics.bodyHeight[0]).toBe(metrics.bodyHeight[1])
}

async function attachViewportShot(
  page: Page,
  testInfo: TestInfo,
  name: string,
) {
  await testInfo.attach(name, {
    body: await page.screenshot({
      animations: "disabled",
      fullPage: false,
    }),
    contentType: "image/png",
  })
}

test("skin selection persists and Cyber is present at first hydrated frame", async ({
  page,
}) => {
  await setStoredPreferences(page, "classic", "en")
  await page.goto("/global/")
  await page.getByRole("button", { name: "Cyber HUD skin" }).click()

  await expect(page.locator("html")).toHaveAttribute("data-skin", "cyber")
  await expect(page.locator(".cyber-shell")).toBeVisible()
  await page.reload()
  await expect(page.locator("html")).toHaveAttribute("data-skin", "cyber")
  await expect(page.locator(".cyber-shell")).toBeVisible()
  await expect(page.getByRole("button", { name: "Cyber HUD skin" })).toHaveAttribute(
    "aria-pressed",
    "true",
  )
})

for (const viewport of viewports) {
  test.describe(viewport.name, () => {
    test.use({ viewport: { width: viewport.width, height: viewport.height } })

    for (const language of ["en", "zh"] as const) {
      test(`Cyber single-screen smoke (${language})`, async ({
        page,
      }, testInfo) => {
        await setStoredPreferences(page, "cyber", language)

        for (const route of routes) {
          await page.goto(`/${route}/`)
          await expect(page.locator("html")).toHaveAttribute("data-skin", "cyber")
          await expect(page.locator(".cyber-shell")).toBeVisible()
          await expect(page.locator(".cyber-viewport-guard")).toBeHidden()
          await assertNoPageOverflow(page)
          await attachViewportShot(
            page,
            testInfo,
            `${route}-${language}-${viewport.name}.png`,
          )
        }
      })
    }
  })
}

test("undersized Cyber viewport shows the explicit minimum-size guard", async ({
  page,
}) => {
  await page.setViewportSize({ width: 1366, height: 768 })
  await setStoredPreferences(page, "cyber", "en")
  await page.goto("/global/")
  await expect(page.locator(".cyber-viewport-guard")).toBeVisible()
  await assertNoPageOverflow(page)
})
