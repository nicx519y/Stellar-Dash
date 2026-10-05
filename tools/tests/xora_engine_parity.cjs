const fs = require("node:fs"),
  assert = require("node:assert/strict");
const { LightEngine } = require("../../common/xora-light-engine.cjs");
const sources = fs
  .readdirSync("resources/xora")
  .filter((n) => n.endsWith(".json"))
  .sort()
  .map((n) => JSON.parse(fs.readFileSync(`resources/xora/${n}`)))
  .filter((s) => s.type !== "switch-mapping");
const engines = sources.map((s) =>
  Array.from({ length: 20 }, () => new LightEngine(s)),
);
const points = require("../../common/xora-light-topology.json");
let largest = 0;
for (const line of fs
  .readFileSync(process.argv[2], "utf8")
  .trim()
  .split(/\r?\n/)) {
  const [j, scenario, t, ...native] = line.split(",").map(Number);
  const expected = engines[j][scenario]
    .render(t * 137, t % 10 < 2 ? 0x3fffff : t % 10 < 5 ? 3 : 0, points, 22, {
      sync: !!(scenario % 2),
      oneShot: !!(Math.floor(scenario / 2) % 2),
      speed: Math.floor(scenario / 4) + 1,
    })
    .flat();
  assert.equal(native.length, expected.length);
  for (let i = 0; i < native.length; i++) {
    const delta = Math.abs(native[i] - expected[i]);
    largest = Math.max(largest, delta);
    assert.ok(
      delta <= 2,
      `${sources[j].resourceId} frame ${t} channel ${i}: C++=${native[i]} JS=${expected[i]}`,
    );
  }
}
console.log(
  `120 frames x 20 scenarios x 10 effects x 62 board LEDs: C++/browser match within ${largest} RGB levels`,
);
