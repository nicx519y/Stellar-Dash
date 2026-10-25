import { create } from 'zustand';

// Header actions share the log page's lock; navigation stays available to ask
// whether the user wants to interrupt a read.
export const useLogOperation = create<{ operation: 'read' | 'clear' | 'policy' | null;
  setOperation: (operation: 'read' | 'clear' | 'policy' | null) => void }>(set => ({
  operation: null, setOperation: operation => set({ operation }),
}));
