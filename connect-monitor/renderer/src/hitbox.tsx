import { useCallback, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";

import type { HitboxSummary } from "../../shared/monitor-types";
import { HitboxCanvas } from "./ui/HitboxCanvas";

function initialCompactMode() {
  return new URLSearchParams(window.location.search).get("compact") !== "0";
}

function HitboxApp() {
  const [compact, setCompact] = useState(initialCompactMode);

  useEffect(() => {
    return window.connectMonitorApi?.onHitboxOptions?.((options) => {
      setCompact(options.compact);
    });
  }, []);

  const publishSummary = useCallback((summary: HitboxSummary) => {
    window.connectMonitorApi?.publishHitboxSummary?.(summary);
  }, []);

  return <HitboxCanvas compact={compact} onSummary={publishSummary} />;
}

const globalStyle = document.createElement("style");
globalStyle.textContent = `
  html,
  body,
  #root {
    width: 100% !important;
    height: 100% !important;
    min-width: 0 !important;
    min-height: 0 !important;
    margin: 0 !important;
    padding: 0 !important;
    overflow: hidden !important;
    background: transparent !important;
  }
`;
document.head.appendChild(globalStyle);

const el = document.getElementById("root");
if (el) {
  createRoot(el).render(<HitboxApp />);
}
