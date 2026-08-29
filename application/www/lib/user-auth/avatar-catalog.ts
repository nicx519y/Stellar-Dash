export interface AccountAvatarOption {
  id: string;
  name: string;
  src: string;
}

const CHARACTERS = [
  ['ryu', 'Ryu'], ['luke', 'Luke'], ['jamie', 'Jamie'],
  ['chun-li', 'Chun-Li'], ['guile', 'Guile'], ['kimberly', 'Kimberly'],
  ['juri', 'Juri'], ['ken', 'Ken'], ['blanka', 'Blanka'],
  ['dhalsim', 'Dhalsim'], ['e-honda', 'E. Honda'], ['dee-jay', 'Dee Jay'],
  ['manon', 'Manon'], ['marisa', 'Marisa'], ['jp', 'JP'],
  ['zangief', 'Zangief'], ['lily', 'Lily'], ['cammy', 'Cammy'],
  ['rashid', 'Rashid'], ['aki', 'A.K.I.'], ['ed', 'Ed'],
  ['akuma', 'Akuma'], ['m-bison', 'M. Bison'], ['terry', 'Terry'],
  ['mai', 'Mai'], ['elena', 'Elena'], ['sagat', 'Sagat'],
  ['c-viper', 'C. Viper'], ['alex', 'Alex'], ['ingrid', 'Ingrid'],
  ['yasmine', 'Yasmine'],
] as const;

export const ACCOUNT_AVATARS: readonly AccountAvatarOption[] = CHARACTERS.map(
  ([slug, name]) => {
    const id = `sf6-${slug}`;
    return { id, name, src: `/images/account-avatars/${id}.webp` };
  },
);

export const ACCOUNT_AVATAR_IDS = new Set(ACCOUNT_AVATARS.map(item => item.id));
