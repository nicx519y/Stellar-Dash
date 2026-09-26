"use client";

import { GameProfile, PROFILE_NAME_MAX_LENGTH } from "@/types/gamepad-config";
import { useEffect, useRef, useState } from "react";
import { Box, Button, HStack, IconButton, Separator, Text, VStack } from "@chakra-ui/react";
import { Tooltip } from "@/components/ui/tooltip";
import { LuLayers3, LuPencil } from "react-icons/lu";
import { FaLock } from "react-icons/fa";
import { openForm } from '@/components/dialog-form';
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from '@/contexts/language-context';
import { profileSlots } from '@/lib/profile-slots';

export function ProfileSelect({ disabled = false }: { disabled?: boolean }) {
    const { profileList, switchProfile, stageDeferredProfileDetails, configEditingBlocked,
        deviceConnected, deviceSession, dataIsReady, isLoading, rfBindingBusy } = useGamepadConfig();
    const { t } = useLanguage();
    const [operation, setOperation] = useState<'dialog' | 'save' | 'switch' | null>(null);
    const [error, setError] = useState('');
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
        setError('');
        try {
            await switchProfile(profile.id);
        } catch {
            setError(t.PROFILE_SELECT_OPERATION_FAILED);
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
        setError('');
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
            setError(t.PROFILE_SELECT_OPERATION_FAILED);
        } finally {
            busy.current = false;
            setOperation(null);
        }
    };

    return (
        <VStack as="section" aria-label={t.PROFILE_SELECT_TITLE} align="stretch" gap={3} h="100%" minH={0}>
            <Text fontSize="md" fontWeight="semibold" color={disabled ? 'fg.muted' : 'fg'} flexShrink={0}>
                {t.PROFILE_SELECT_TITLE}
            </Text>
            {dataIsReady && !compatible && <Text role="status" fontSize="xs" color="fg.muted">{t.PROFILE_SELECT_FIRMWARE_REQUIRED}</Text>}
            {error && <Text role="alert" fontSize="xs" color="fg.error">{error}</Text>}
            <VStack align="stretch" gap={0} overflowY="auto" minH={0} flex={1}
                separator={<Separator />}>
                {slots.map((profile, index) => {
                    const selected = profile?.id === profileList.defaultId;
                    return (
                        <HStack key={index} gap={0} flexShrink={0} minH="36px" borderRadius="md"
                            bg={selected ? 'green.solid' : 'transparent'}
                            transition="background-color 150ms ease"
                            _hover={{ bg: selected ? 'green.500' : 'bg.emphasized' }}
                            _focusWithin={{ bg: selected ? 'green.500' : 'bg.emphasized' }}
                            css={{
                                '& .profile-edit': { opacity: 0, pointerEvents: 'none' },
                                '&:hover .profile-edit, &:focus-within .profile-edit': { opacity: 1, pointerEvents: 'auto' },
                                '& .profile-edit svg': { opacity: 0.55, transition: 'opacity 150ms ease' },
                                '& .profile-edit:hover svg, & .profile-edit:focus-visible svg': { opacity: 1 },
                                '@media (hover: none)': { '& .profile-edit': { opacity: 1, pointerEvents: 'auto' } },
                            }}>
                            <Button size="sm" variant="ghost" flex={1} minW={0} px={2} justifyContent="flex-start"
                                color={selected ? 'green.contrast' : 'fg.muted'} aria-pressed={selected}
                                _hover={{ bg: 'transparent' }}
                                disabled={blocked || !!operation || !profile}
                                onClick={() => profile && void select(profile)}>
                                <Box flexShrink={0} aria-hidden="true"><LuLayers3 size={15} /></Box>
                                <Text as="span" truncate title={profile?.name}>{profile?.name || t.PROFILE_SELECT_UNAVAILABLE}</Text>
                                {profile?.isCompetitionProfile && <Box flexShrink={0} color="orange.400"><FaLock aria-label={t.SETTINGS_KEY_MAPPING_COMPETITION_MODE_LABEL} size={10} /></Box>}
                            </Button>
                            <Tooltip content={t.PROFILE_SELECT_RENAME_BUTTON}>
                                <IconButton className="profile-edit" size="xs" variant="ghost" flexShrink={0}
                                    color={selected ? 'green.contrast' : undefined}
                                    _hover={{ bg: 'transparent' }}
                                    aria-label={t.PROFILE_SELECT_RENAME_BUTTON + ': ' + (profile?.name || t.PROFILE_SELECT_UNAVAILABLE)}
                                    disabled={blocked || !!operation || !profile}
                                    onClick={() => profile && void rename(profile)}>
                                    <LuPencil />
                                </IconButton>
                            </Tooltip>
                        </HStack>
                    );
                })}
            </VStack>
        </VStack>
    );
}
