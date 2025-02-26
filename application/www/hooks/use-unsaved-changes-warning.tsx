'use client';

import { useEffect, useState } from 'react';
import { navigationEvents } from '@/lib/event-manager';
import { openConfirm } from '@/components/dialog-confirm';
import { useLanguage } from '@/contexts/language-context';

/**
 * Hook to show a warning when the user tries to navigate away from a page with unsaved changes.
 * @returns [boolean, (value: boolean) => void] - A tuple containing the current dirty state and a function to set it.
 */
export default function useUnsavedChangesWarning(title?: string, message?: string): [boolean, (value: boolean) => void] {
  const [isDirty, setIsDirty] = useState(false);
  const { t } = useLanguage();
  
  useEffect(() => {
    if (!isDirty) return;

    const handleBeforeNavigate = async ({ path }: { path: string }) => {
      try {
        return await openConfirm({
          title: title ?? t.UNSAVED_CHANGES_WARNING_TITLE,
          message: message ?? t.UNSAVED_CHANGES_WARNING_MESSAGE,
        });
      } catch {
        return false;
      }
    };

    navigationEvents.addListener(handleBeforeNavigate);
    return () => navigationEvents.removeListener(handleBeforeNavigate);
  }, [isDirty]);

  return [isDirty, setIsDirty];
}