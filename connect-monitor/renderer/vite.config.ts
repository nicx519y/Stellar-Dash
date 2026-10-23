import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import path from "node:path";
import { readFileSync } from "node:fs";

const webconfig = path.resolve(__dirname, "../../application/www");
const webfontDir = path.join(webconfig, "public/fonts/sarasa-mono-sc");

export default defineConfig({
  root: path.resolve(__dirname),
  base: "./",
  plugins: [react(), {
    name: "webconfig-fonts",
    enforce: "pre",
    transform(source, id) {
      if (id.replaceAll("\\", "/") !== `${webfontDir.replaceAll("\\", "/")}/fonts.css`) return;
      // Let Vite bundle shared font files with relative URLs for Electron file://.
      return source.replaceAll("/fonts/sarasa-mono-sc/", "./");
    },
    generateBundle() {
      this.emitFile({ type: "asset", fileName: "fonts/sarasa-mono-sc/OFL.txt", source: readFileSync(path.join(webfontDir, "OFL.txt"), "utf8") });
    },
  }],
  build: {
    outDir: path.resolve(__dirname, "..", "dist", "renderer"),
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: path.resolve(__dirname, "index.html"),
        hitbox: path.resolve(__dirname, "hitbox.html"),
        latencyTable: path.resolve(__dirname, "latency-table.html"),
      },
    },
  },
  server: {
    port: 15173,
    strictPort: true,
    fs: {
      allow: [path.resolve(__dirname, ".."), path.join(webconfig, "lib"), webfontDir],
    },
  },
});
