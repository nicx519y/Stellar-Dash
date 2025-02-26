"use client"

import { PROFILE_NAME_MAX_LENGTH } from "@/types/gamepad-config";
import { useMemo } from "react";
import {
    IconButton,
    Stack,
    createListCollection,
} from "@chakra-ui/react"

import {
    MenuContent,
    MenuItem,
    MenuRoot,
    MenuTrigger,
} from "@/components/ui/menu"

import {
    SelectContent,
    SelectItem,
    SelectRoot,
    SelectTrigger,
    SelectValueText,
} from "@/components/ui/select"

import { LuTrash, LuPlus, LuPencil, LuMenu } from "react-icons/lu"
import { openConfirm } from '@/components/dialog-confirm';
import { openForm } from '@/components/dialog-form';
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from '@/contexts/language-context';

export function ProfileSelect() {

    const { profileList, switchProfile, createProfile, deleteProfile, updateProfileDetails } = useGamepadConfig();
    const { t } = useLanguage();

    const defaultProfile = useMemo(() => {
        const profile = profileList.items.find(p => p.id === profileList.defaultId);
        return profile;
    }, [profileList]);

    const profilesCollection = useMemo(() => {
        try {
            return createListCollection({
                items: profileList.items.map(p => ({
                    value: p.id,
                    label: p.name,
                })),
            });
        } catch {
            return createListCollection({ items: [] });
        }
    }, [profileList]);

    /**
     * Validate the profile name.
     * @param name - The name to validate.
     * @param setInvalid - The function to set the invalid state.
     * @param setErrorMessage - The function to set the error message.
     * @returns - Whether the profile name is valid.
     */
    const validateProfileName = (name: string): [boolean, string] => {

        if (/[!@#$%^&*()_+\[\]{}|;:'",.<>?/\\]/.test(name)) {
            return [false, t.PROFILE_SELECT_VALIDATION_SPECIAL_CHARS];
        }

        if (name.length > PROFILE_NAME_MAX_LENGTH || name.length < 1) {
            return [false, t.PROFILE_SELECT_VALIDATION_LENGTH.replace("{0}", name.length.toString())];
        }

        if (name === defaultProfile?.name) {
            return [false, t.PROFILE_SELECT_VALIDATION_SAME_NAME];
        }

        if (profileList.items.find(p => p.name === name)) {
            return [false, t.PROFILE_SELECT_VALIDATION_EXISTS];
        }

        return [true, ""];
    }

    /**
     * Change the default profile.
     * @param value - The id of the profile to set as default.
     */
    const onDefaultProfileChange = async (value: string) => {
        if (value === defaultProfile?.id) {
            return;
        }
        return await switchProfile(value);
    }

    /**
     * Open the rename dialog.
     */
    const renameProfileClick = async () => {
        const result = await openForm({
            title: t.DIALOG_RENAME_PROFILE_TITLE,
            fields: [{
                name: "profileName",
                label: t.PROFILE_NAME_LABEL,
                defaultValue: defaultProfile?.name,
                placeholder: t.PROFILE_NAME_PLACEHOLDER,
                validate: (value) => {
                    const [isValid, errorMessage] = validateProfileName(value);
                    if (!isValid) {
                        return errorMessage;
                    }
                    return undefined;
                }
            }]
        });

        if (result) {
            await updateProfileDetails(defaultProfile?.id ?? "", {
                id: defaultProfile?.id ?? "",
                name: result.profileName
            });
        }
    };

    /**
     * Open the add dialog.
     */
    const createProfileClick = async () => {
        const result = await openForm({
            title: t.PROFILE_CREATE_DIALOG_TITLE,
            fields: [{
                name: "profileName",
                label: t.PROFILE_NAME_LABEL,
                placeholder: t.PROFILE_NAME_PLACEHOLDER,
                validate: (value) => {
                    const [isValid, errorMessage] = validateProfileName(value);
                    if (!isValid) {
                        return errorMessage;
                    }
                    return undefined;
                }
            }]
        });

        if (result) {
            await createProfile(result.profileName);
        }
    };

    /**
     * Open the delete dialog.
     */
    const deleteProfileClick = async () => {
        const confirmed = await openConfirm({
            title: t.PROFILE_DELETE_DIALOG_TITLE,
            message: t.PROFILE_DELETE_CONFIRM_MESSAGE
        });

        if (confirmed) {
            await onDeleteConfirm();
        }
    };

    /**************************************************************** set api confirmation ******************************************************************************** */
    /**
     * Confirm the deletion of the default profile.
     */
    const onDeleteConfirm = async () => {
        return await deleteProfile(defaultProfile?.id ?? "");
    }

    const menuItems = [
        {
            value: "create",
            label: t.PROFILE_SELECT_CREATE_BUTTON,
            icon: <LuPlus />,
            onClick: createProfileClick
        },
        {
            value: "rename",
            label: t.PROFILE_SELECT_RENAME_BUTTON,
            icon: <LuPencil />,
            onClick: renameProfileClick
        },
        {
            value: "delete",
            label: t.PROFILE_SELECT_DELETE_BUTTON,
            icon: <LuTrash />,
            onClick: deleteProfileClick
        }
    ]

    return (
        <>
            { // 如果 profileList 存在且 profileList.items 的长度大于 0，则显示选择器
                profileList && profileList.items.length > 0 && (
                    <Stack direction="row" gap={2} alignItems="center">
                        <SelectRoot
                            size="sm"
                            width="200px"
                            collection={profilesCollection}
                            value={[defaultProfile?.id ?? ""]}
                            onValueChange={e => onDefaultProfileChange(e.value[0])}
                        >
                            <SelectTrigger bg="bg.muted"  >
                                <SelectValueText opacity={0.75}  />
                            </SelectTrigger>
                            <SelectContent fontSize="xs" >
                                {profilesCollection.items.map((item) => (
                                    <SelectItem key={item.value} item={item} >
                                        {item.label}
                                    </SelectItem>
                                ))}
                            </SelectContent>
                        </SelectRoot>
                        <MenuRoot>
                            <MenuTrigger asChild>
                                <IconButton
                                    aria-label={t.PROFILE_SELECT_MENU_BUTTON}
                                    variant="ghost"
                                    size="sm"
                                >
                                    <LuMenu />
                                </IconButton>
                            </MenuTrigger>
                            <MenuContent  >
                                {
                                    menuItems.map((item) => (
                                        <MenuItem key={item.value} value={item.value} onClick={item.onClick}>
                                            {item.icon} {item.label}
                                        </MenuItem>
                                    ))
                                }
                            </MenuContent>
                        </MenuRoot>
                    </Stack>
                )
            }
        </>
    )
}