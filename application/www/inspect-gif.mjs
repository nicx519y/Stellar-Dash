import { readFile } from 'node:fs/promises';
import { parseGIF, decompressFrames } from 'gifuct-js';

const inputPath = process.argv[2];
if (!inputPath) {
  process.stderr.write('Usage: node inspect-gif.mjs <path-to-gif>\n');
  process.exit(1);
}

const buf = await readFile(inputPath);
const header = buf.subarray(0, 6).toString('ascii');
const gif = parseGIF(new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength));
const lsd = gif?.lsd ?? {};

const frames = decompressFrames(gif, true);
const delaysCs = frames.map((f) => (typeof f?.delay === 'number' ? f.delay : 0));
const totalCs = delaysCs.reduce((a, b) => a + (b > 0 ? b : 10), 0);
const totalMs = totalCs * 10;
const fps = frames.length > 0 ? (frames.length / (totalMs / 1000)) : 0;

process.stdout.write(
  JSON.stringify(
    {
      file: inputPath,
      header,
      lsd: { width: lsd.width ?? null, height: lsd.height ?? null },
      frameCount: frames.length,
      totalMs,
      fps,
      firstFrames: frames.slice(0, 10).map((f, i) => ({
        i,
        delayCs: typeof f?.delay === 'number' ? f.delay : null,
        dims: f?.dims ?? null,
        patchBytes: f?.patch?.length ?? null,
      })),
    },
    null,
    2
  ) + '\n'
);

