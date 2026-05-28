import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { ChakraProvider, defaultSystem } from "@chakra-ui/react";

import { App } from "./ui/App";

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
    background: #05080d !important;
  }

  body {
    position: fixed !important;
    inset: 0 !important;
  }

  html::-webkit-scrollbar,
  body::-webkit-scrollbar {
    width: 0 !important;
    height: 0 !important;
  }
`;
document.head.appendChild(globalStyle);

const el = document.getElementById("root");
if (el) {
  createRoot(el).render(
    <StrictMode>
      <ChakraProvider value={defaultSystem}>
        <App />
      </ChakraProvider>
    </StrictMode>,
  );
}
