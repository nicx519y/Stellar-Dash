export type Language = 'en' | 'zh';
const STORAGE_KEY = 'preferred_language';

export function readLanguagePreference(): Language {
  try {
    return localStorage.getItem(STORAGE_KEY) === 'zh' ? 'zh' : 'en';
  } catch {
    return 'en';
  }
}

export function saveLanguagePreference(language: Language) {
  try {
    localStorage.setItem(STORAGE_KEY, language);
  } catch {
    // Language selection still works when browser storage is unavailable.
  }
}
