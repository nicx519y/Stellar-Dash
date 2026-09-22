import { parentPort, workerData } from "node:worker_threads";
import { MonitorEventStore } from "./event-store";
import type { MonitorEvent } from "./types";

const port = parentPort!;
const store = new MonitorEventStore(workerData.baseDir);
let generation = -1;
port.on("message", (message: {
  type: "work" | "close"; generation: number; events: MonitorEvent[];
  queries: { id: number; before: number; limit: number }[];
}) => {
  if (message.type === "close") { store.clear(); port.close(); return; }
  // Clear is ordered after any in-progress disk operation and before new rows.
  if (generation !== message.generation) { store.clear(); generation = message.generation; }
  store.append(message.events);
  const results = message.queries.map(query => ({ id: query.id, rows: store.queryBefore(query.before, query.limit) }));
  port.postMessage({ generation, results, error: store.lastWriteError });
});
