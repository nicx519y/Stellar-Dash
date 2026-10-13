/** Committed snapshots keep actions stable while subscribing only to requested fields. */
export class SelectionStore<T extends object> {
    private current: T;
    private snapshot: T;
    private readonly listeners = new Set<() => void>();
    private readonly actions = new Map<keyof T, unknown>();

    constructor(initial: T) {
        this.current = initial;
        this.snapshot = this.normalize(initial);
    }

    private normalize(value: T): T {
        const next = { ...value };
        for (const key of Object.keys(value) as (keyof T)[]) {
            if (typeof value[key] !== 'function') continue;
            if (!this.actions.has(key)) {
                this.actions.set(key, (...args: unknown[]) => {
                    const action = this.current[key] as (...args: unknown[]) => unknown;
                    return action(...args);
                });
            }
            next[key] = this.actions.get(key) as T[keyof T];
        }
        return next;
    }

    /** Called only after React commits, never from an abandoned render. */
    publish(value: T): void {
        this.current = value;
        const next = this.normalize(value);
        const changed = (Object.keys(next) as (keyof T)[]).some(key => !Object.is(next[key], this.snapshot[key]));
        if (!changed) return;
        this.snapshot = next;
        for (const listener of this.listeners) listener();
    }

    getSnapshot = (): T => this.snapshot;
    subscribe = (listener: () => void): (() => void) => {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    };
}

/** A selector retains object identity until one of its fields changes. */
export function selectFields<T extends object, K extends keyof T>(store: SelectionStore<T>, keys: readonly K[]) {
    let selected: Pick<T, K> | undefined;
    return (): Pick<T, K> => {
        const snapshot = store.getSnapshot();
        if (!selected || keys.some(key => !Object.is(selected![key], snapshot[key]))) {
            const next = {} as Pick<T, K>;
            for (const key of keys) next[key] = snapshot[key];
            selected = next;
        }
        return selected;
    };
}
