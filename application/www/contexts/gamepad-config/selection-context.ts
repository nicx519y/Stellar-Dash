import { createContext, useContext, useMemo, useSyncExternalStore } from 'react';
import { SelectionStore, selectFields } from './selection-store';

/** One context carries a stable store; each hook subscribes only to named fields. */
export function createSelectionContext<T extends object>() {
    const Context = createContext<SelectionStore<T> | undefined>(undefined);
    function useFields<K extends keyof T>(...keys: K[]): Pick<T, K> {
        const store = useContext(Context);
        if (!store) throw new Error('useGamepadFields must be used within a GamepadConfigProvider');
        const keySignature = keys.join('\0');
        const getSnapshot = useMemo(() => selectFields(store, keys), [store, keySignature]);
        return useSyncExternalStore(store.subscribe, getSnapshot, getSnapshot);
    }
    return { Context, useFields };
}
