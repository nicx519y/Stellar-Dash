"use client";

import { SidebarList, SidebarListItems, SidebarListItem } from '@/components/ui/sidebar-list';
import { toaster } from '@/components/ui/toaster';
import { GameProfile, PROFILE_NAME_MAX_LENGTH } from "@/types/gamepad-config";
import { useEffect, useRef, useState } from "react";
import { Box, IconButton, Text } from "@chakra-ui/react";
import { Tooltip } from "@/components/ui/tooltip";
import { LuLayers3, LuPencil, LuTriangleAlert } from "react-icons/lu";
import { FaLock } from "react-icons/fa";
import { openForm } from '@/components/dialog-form';
import { useGamepadFields } from "@/contexts/gamepad-config-context";
import { useLanguage } from '@/contexts/language-context';
import { profileSlots } from '@/lib/profile-slots';

export function ProfileSelect({ disabled = false }: { disabled?: boolean }) {
    const { profileList, switchProfile, stageDeferredProfileDetails, configEditingBlocked,
        deviceConnected, deviceSession, dataIsReady, isLoading, rfBindingBusy } = useGamepadFields("profileList", "switchProfile", "stageDeferredProfileDetails", "configEditingBlocked", "deviceConnected", "deviceSession", "dataIsReady", "isLoading", "rfBindingBusy");
    const { t } = useLanguage();
    const [operation, setOperation] = useState<'dialog' | 'save' | 'switch' | null>(null);
    const busy = useRef(false);
    const mounted = useRef(false);
    useEffect(() => {
        mounted.current = true;
        return () => { mounted.current = false; };
    }, []);
    const { slots, compatible } = profileSlots(profileList);
    const blocked = disabled || !deviceConnected || !dataIsReady || isLoading || configEditingBlocked || rfBindingBusy || !compatible;
    const live = useRef({ blocked, profileList, deviceSession });
    live.current = { blocked, profileList, deviceSession };

    const select = async (profile: GameProfile) => {
        if (live.current.blocked || busy.current || profile.id === profileList.defaultId) return;
        busy.current = true;
        setOperation('switch');
        try {
            await switchProfile(profile.id);
        } catch {
            toaster.error({ title: t.PROFILE_SELECT_OPERATION_FAILED });
        } finally {
            busy.current = false;
            setOperation(null);
        }
    };

    const rename = async (profile: GameProfile) => {
        if (live.current.blocked || busy.current) return;
        const session = live.current.deviceSession;
        busy.current = true;
        setOperation('dialog');
        try {
            const result = await openForm({
                title: t.DIALOG_RENAME_PROFILE_TITLE,
                fields: [{
                    name: 'profileName', label: t.PROFILE_NAME_LABEL,
                    defaultValue: profile.name, placeholder: t.PROFILE_NAME_PLACEHOLDER,
                    validate: (value) => {
                        if (!value.length || value.length > PROFILE_NAME_MAX_LENGTH)
                            return t.PROFILE_SELECT_VALIDATION_LENGTH.replace('{0}', String(value.length));
                        if (!/^[A-Za-z0-9_-]+$/.test(value)) return t.PROFILE_SELECT_VALIDATION_SPECIAL_CHARS;
                        if (value === profile.name) return t.PROFILE_SELECT_VALIDATION_SAME_NAME;
                        if (live.current.profileList.items.some((item) => item.id !== profile.id && item.name === value))
                            return t.PROFILE_SELECT_VALIDATION_EXISTS;
                        return undefined;
                    },
                }],
            });
            if (!result || !mounted.current || live.current.blocked || live.current.deviceSession !== session || !live.current.profileList.items.some(
                (item) => item.id === profile.id && item.slotIndex === profile.slotIndex)) return;
            setOperation('save');
            stageDeferredProfileDetails(profile.id, {
                id: profile.id, name: result.profileName,
            });
        } catch {
            toaster.error({ title: t.PROFILE_SELECT_OPERATION_FAILED });
        } finally {
            busy.current = false;
            setOperation(null);
        }
    };

    return (
        <SidebarList as="section" data-testid="profile-sidebar" aria-label={t.PROFILE_SELECT_TITLE}
            title={t.PROFILE_SELECT_TITLE} h="100%" color={disabled ? 'fg.muted' : undefined}
            titleAction={dataIsReady && !compatible ? <Tooltip content={t.PROFILE_SELECT_FIRMWARE_REQUIRED}>
                <IconButton aria-label={t.PROFILE_SELECT_FIRMWARE_REQUIRED} title={t.PROFILE_SELECT_FIRMWARE_REQUIRED}
                    variant="ghost" color="orange.fg" h="18px" minW="18px" p={0}><LuTriangleAlert /></IconButton>
            </Tooltip> : undefined}>
            <SidebarListItems overflowY="auto" flex={1}>
                {slots.map((profile, index) => {
                    const selected = profile?.id === profileList.defaultId;
                    return (
                        <SidebarListItem key={index} selected={selected} aria-pressed={selected}
                            disabled={blocked || !!operation || !profile}
                            onClick={() => profile && void select(profile)}
                            rowProps={{ css: {
                                '& .profile-edit': { opacity: 0, pointerEvents: 'none' },
                                '&:hover .profile-edit, &:focus-within .profile-edit': { opacity: 1, pointerEvents: 'auto' },
                                '& .profile-edit svg': { opacity: 0.55, transition: 'opacity 150ms ease' },
                                '& .profile-edit:hover svg, & .profile-edit:focus-visible svg': { opacity: 1 },
                                '@media (hover: none)': { '& .profile-edit': { opacity: 1, pointerEvents: 'auto' } },
                            } }}
                            action={<Tooltip content={t.PROFILE_SELECT_RENAME_BUTTON}>
                                <IconButton className="profile-edit" size="xs" variant="ghost" flexShrink={0}
                                    color="inherit"
                                    _hover={{ bg: 'transparent' }}
                                    aria-label={t.PROFILE_SELECT_RENAME_BUTTON + ': ' + (profile?.name || t.PROFILE_SELECT_UNAVAILABLE)}
                                    disabled={blocked || !!operation || !profile}
                                    onClick={() => profile && void rename(profile)}>
                                    <LuPencil />
                                </IconButton>
                            </Tooltip>}>
                            <Box as={LuLayers3} boxSize="15px" flexShrink={0} aria-hidden="true" />
                            <Text as="span" truncate title={profile?.name}>{profile?.name || t.PROFILE_SELECT_UNAVAILABLE}</Text>
                            {profile?.isCompetitionProfile && <Box flexShrink={0} color="orange.400"><FaLock aria-label={t.SETTINGS_KEY_MAPPING_COMPETITION_MODE_LABEL} size={10} /></Box>}
                        </SidebarListItem>
                    );
                })}
            </SidebarListItems>
        </SidebarList>
    );
}
