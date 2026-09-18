"use client"

import * as React from "react"

export type VisualSkin = "classic" | "cyber"

export const SKIN_STORAGE_KEY = "hbox.webconfig.skin.v1"
export const DEFAULT_VISUAL_SKIN: VisualSkin = "classic"

interface SkinContextValue {
  skin: VisualSkin
  isCyber: boolean
  setSkin: (skin: VisualSkin) => void
  toggleSkin: () => void
}

const SkinContext = React.createContext<SkinContextValue | undefined>(undefined)
const useIsomorphicLayoutEffect =
  typeof window === "undefined" ? React.useEffect : React.useLayoutEffect

export function isVisualSkin(value: unknown): value is VisualSkin {
  return value === "classic" || value === "cyber"
}

function applyDocumentSkin(skin: VisualSkin) {
  document.documentElement.setAttribute("data-skin", skin)
}

/**
 * Intended for an inline script in the document head. It keeps the root
 * attribute in sync before React hydration, avoiding a flash back to Classic.
 */
export const skinInitializationScript = `
;(function () {
  var skin = "classic";
  try {
    var saved = window.localStorage.getItem(${JSON.stringify(SKIN_STORAGE_KEY)});
    if (saved === "cyber" || saved === "classic") skin = saved;
  } catch (_) {}
  document.documentElement.setAttribute("data-skin", skin);
})();
`

export function SkinProvider({ children }: { children: React.ReactNode }) {
  // Keep server/client markup deterministic. The inline initializer has
  // already applied the persisted attribute before this effect runs.
  const [skin, setSkinState] =
    React.useState<VisualSkin>(DEFAULT_VISUAL_SKIN)

  const commitSkin = React.useCallback(
    (nextSkin: VisualSkin, persist: boolean) => {
      applyDocumentSkin(nextSkin)
      setSkinState(nextSkin)

      if (!persist) return

      try {
        window.localStorage.setItem(SKIN_STORAGE_KEY, nextSkin)
      } catch {
        // The root attribute still makes skin switching work when storage is
        // unavailable (private mode, embedded browser policy, or quota).
      }
    },
    [],
  )

  // Read the attribute written by the inline head script in a layout effect.
  // The Cyber/Classical branch is therefore corrected before the first paint,
  // while server and hydration markup remain deterministic.
  useIsomorphicLayoutEffect(() => {
    let initialSkin: VisualSkin = DEFAULT_VISUAL_SKIN
    const attributeSkin = document.documentElement.getAttribute("data-skin")

    if (isVisualSkin(attributeSkin)) {
      initialSkin = attributeSkin
    } else {
      try {
        const storedSkin = window.localStorage.getItem(SKIN_STORAGE_KEY)
        if (isVisualSkin(storedSkin)) initialSkin = storedSkin
      } catch {
        // Use Classic when storage access is unavailable.
      }
    }

    commitSkin(initialSkin, false)

    const handleStorage = (event: StorageEvent) => {
      if (event.key !== SKIN_STORAGE_KEY) return
      commitSkin(
        isVisualSkin(event.newValue) ? event.newValue : DEFAULT_VISUAL_SKIN,
        false,
      )
    }

    window.addEventListener("storage", handleStorage)
    return () => window.removeEventListener("storage", handleStorage)
  }, [commitSkin])

  const setSkin = React.useCallback(
    (nextSkin: VisualSkin) => commitSkin(nextSkin, true),
    [commitSkin],
  )

  const toggleSkin = React.useCallback(() => {
    setSkin(skin === "cyber" ? "classic" : "cyber")
  }, [setSkin, skin])

  const value = React.useMemo<SkinContextValue>(
    () => ({
      skin,
      isCyber: skin === "cyber",
      setSkin,
      toggleSkin,
    }),
    [setSkin, skin, toggleSkin],
  )

  return <SkinContext.Provider value={value}>{children}</SkinContext.Provider>
}

export function useSkin() {
  const context = React.useContext(SkinContext)

  if (!context) {
    throw new Error("useSkin must be used within a SkinProvider")
  }

  return context
}
