import { create } from 'zustand';

// Header actions share the log page's lock; navigation stays available to ask
// whether the user wants to interrupt a read.
export const useLogOperation = create<{ operation: 'read' | 'clear' | null;
  setOperation: (operation: 'read' | 'clear' | null) => void }>(set => ({
  operation: null, setOperation: operation => set({ operation }),
}));
