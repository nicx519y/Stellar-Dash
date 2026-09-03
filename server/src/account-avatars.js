'use strict';

const ACCOUNT_AVATARS = Object.freeze([
    'ryu', 'luke', 'jamie', 'chun-li', 'guile', 'kimberly', 'juri', 'ken',
    'blanka', 'dhalsim', 'e-honda', 'dee-jay', 'manon', 'marisa', 'jp',
    'zangief', 'lily', 'cammy', 'rashid', 'aki', 'ed', 'akuma', 'm-bison',
    'terry', 'mai', 'elena', 'sagat', 'c-viper', 'alex', 'ingrid', 'yasmine',
].map(id => `sf6-${id}`));

const ACCOUNT_AVATAR_IDS = new Set(ACCOUNT_AVATARS);

function avatarUrl(avatarId) {
    return avatarId ? `/images/account-avatars/${avatarId}.webp` : null;
}

function normalizeAvatarId(value) {
    if (typeof value !== 'string' || !ACCOUNT_AVATAR_IDS.has(value)) {
        return null;
    }
    return value;
}

module.exports = {
    ACCOUNT_AVATARS,
    ACCOUNT_AVATAR_IDS,
    avatarUrl,
    normalizeAvatarId,
};
