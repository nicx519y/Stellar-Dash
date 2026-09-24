import type { GameProfile, GameProfileList } from '../types/gamepad-config';

export function profileSlots(list: GameProfileList): { slots: (GameProfile | undefined)[]; compatible: boolean } {
    const count = Number.isInteger(list.maxNumProfiles) && list.maxNumProfiles > 0 && list.maxNumProfiles <= 255
        ? list.maxNumProfiles : 0;
    const slots: (GameProfile | undefined)[] = Array.from({ length: count });
    const ids = new Set<string>();
    let compatible = count > 0 && list.items.length === count;
    for (const profile of list.items) {
        const index = profile.slotIndex;
        if (index === undefined || !Number.isInteger(index) || index < 0 || index >= count ||
            slots[index] || !profile.id || ids.has(profile.id)) {
            compatible = false;
            continue;
        }
        slots[index] = profile;
        ids.add(profile.id);
    }
    // Legacy names may be shown, but this never invents actionable profile IDs.
    if (!compatible) return { slots: slots.map((profile, index) => profile ?? list.items[index]), compatible: false };
    return { slots, compatible: true };
}
