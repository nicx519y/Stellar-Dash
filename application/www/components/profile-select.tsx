"use client"

import { PROFILE_NAME_MAX_LENGTH } from "@/types/gamepad-config";
import { useMemo } from "react";
import {  Button, Card, HStack, VStack } from "@chakra-ui/react";
import { Tooltip } from "@/components/ui/tooltip";
import {
    IconButton,
    createListCollection,
} from "@chakra-ui/react"


import { LuTrash, LuPlus, LuPencil } from "react-icons/lu"
import { openConfirm } from '@/components/dialog-confirm';
import { openForm } from '@/components/dialog-form';
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from '@/contexts/language-context';
import { useColorMode } from "./ui/color-mode";

export function ProfileSelect(
    props: {
        disabled?: boolean,
    }
) {

    const { profileList, switchProfile, createProfile, deleteProfile, updateProfileDetails } = useGamepadConfig();
    const { t } = useLanguage();
    const { disabled } = props;
    const { colorMode } = useColorMode();
    const isDisabled = useMemo(() => {
        return disabled ?? false;
    }, [disabled]);

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
            value: "delete",
            label: t.PROFILE_SELECT_DELETE_BUTTON,
            icon: <LuTrash />,
            onClick: deleteProfileClick
        },

        {
            value: "rename",
            label: t.PROFILE_SELECT_RENAME_BUTTON,
            icon: <LuPencil />,
            onClick: renameProfileClick
        },
    ]


    return (
        <Card.Root w="100%" minH="450px" >
            <Card.Header >
                <Card.Title fontSize={"md"} color={isDisabled ? "gray.500" :  colorMode === "dark" ? "white" : "black"} >{t.PROFILE_SELECT_TITLE}</Card.Title>
            </Card.Header>
            <Card.Body>
                <VStack  gap={2} >
                    
                    {
                        profilesCollection.items.map((item) => (
                            <Button 
                                key={item.value} 
                                w="180px" 
                                size="sm" 
                                variant={defaultProfile?.id === item.value ? "solid" : "ghost" } 
                                colorPalette={defaultProfile?.id === item.value ? "green" : "gray"} 
                                onClick={() => defaultProfile?.id !== item.value && onDefaultProfileChange(item.value)}
                                justifyContent="flex-start" 
                                disabled={isDisabled}
                            >
                                {item.label}
                            </Button>
                        ))
                    }
                </VStack>
            </Card.Body>
            <Card.Footer >
                <HStack w="100%" gap={1} justifyContent={"flex-end"} >
                    {
                        menuItems.map((item) => (
                            <Tooltip key={item.value} content={item.label} >
                                <IconButton key={item.value} w="32px" size="xs" variant="ghost" colorPalette="green" onClick={item.onClick} disabled={isDisabled}  >
                                    {item.icon}
                                </IconButton>
                            </Tooltip>
                        ))
                    }
                </HStack>
            </Card.Footer>
        </Card.Root>
        
    )
}