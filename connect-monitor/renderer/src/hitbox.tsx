import { useCallback, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";

import type { HitboxOptions, HitboxSummary } from "../../shared/monitor-types";
import { HitboxCanvas } from "./ui/HitboxCanvas";

function initialCompactMode() {
  return new URLSearchParams(window.location.search).get("compact") !== "0";
}

function HitboxApp() {
  const [options, setOptions] = useState<HitboxOptions>(()=>({compact:initialCompactMode(),sourceMode:"RF24G",bindingGeneration:-1}));

  useEffect(() => {
    let active=true,received=false;
    const stop=window.connectMonitorApi?.onHitboxOptions?.(value=>{received=true;setOptions(value);});
    window.connectMonitorApi?.getHitboxOptions?.().then(value=>{if(active && !received && value)setOptions(value);}).catch(()=>{});
    return ()=>{active=false;stop?.();};
  }, []);

  const publishSummary = useCallback((summary: HitboxSummary) => {
    window.connectMonitorApi?.publishHitboxSummary?.(summary);
  }, []);

  return <HitboxCanvas compact={options.compact} sourceMode={options.sourceMode??"RF24G"} bindingGeneration={options.bindingGeneration??-1} onSummary={publishSummary} />;
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
