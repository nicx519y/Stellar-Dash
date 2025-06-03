'use client';

import { useState } from 'react';
import { useNavigationBlocker } from './use-navigation-blocker';
import { useLanguage } from '@/contexts/language-context';

/**
 * Hook to show a warning when the user tries to navigate away from a page with unsaved changes.
 * @returns [boolean, (value: boolean) => void] - A tuple containing the current dirty state and a function to set it.
 */
export default function useUnsavedChangesWarning(title?: string, message?: string): [boolean, (value: boolean) => void] {
  const [isDirty, setIsDirty] = useState(false);
  const { t } = useLanguage();

  useNavigationBlocker(
    isDirty,
    title ?? t.UNSAVED_CHANGES_WARNING_TITLE,
    message ?? t.UNSAVED_CHANGES_WARNING_MESSAGE
  );

  return [isDirty, setIsDirty];
}