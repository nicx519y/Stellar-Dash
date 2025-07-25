'use client';

import { PlatformLabelMap, Platform } from '@/types/gamepad-config';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { Card, Center, Icon, RadioCard, VStack } from '@chakra-ui/react';
import { BsXbox } from "react-icons/bs";
import { SiNintendoswitch, SiPlaystation4, SiPlaystation5   } from "react-icons/si";
import { useLanguage } from '@/contexts/language-context';


export function InputModeSettingContent(props: {
        disabled?: boolean,
    }) {
    const { globalConfig, updateGlobalConfig } = useGamepadConfig();
    const { t } = useLanguage();

    const platformIcons = new Map<Platform, { icon: React.ReactNode, size: string }>([
        [Platform.XINPUT, { icon: <BsXbox />, size: "2xl" }],
        [Platform.PS4, { icon: <SiPlaystation4 />, size: "3xl" }],
        [Platform.PS5, { icon: <SiPlaystation5 />, size: "3xl" }],
        [Platform.SWITCH, { icon: <SiNintendoswitch />, size: "2xl" }],
    ]);

    const onInputModeChange = (detail: { value: Platform }) => {
        updateGlobalConfig({ inputMode: detail.value as Platform });
    }

    return (
        <Card.Root w="100%" h="100%" >
            <Card.Header >
                <Card.Title fontSize={"md"} >{t.INPUT_MODE_TITLE}</Card.Title>
            </Card.Header>
            <Card.Body>
            <RadioCard.Root 
                value={globalConfig.inputMode} 
                orientation="horizontal"
                align="center"
                variant={"solid"}
                colorPalette={"green"}
                onValueChange={(detail) => onInputModeChange(detail as { value: Platform })}
            >
                <VStack w="180px" justifyContent="start" gap={2} >
                    {Array.from(PlatformLabelMap.entries()).map(([platform, { label }]) => (
                        <RadioCard.Item key={platform} value={platform} w="100%" disabled={props.disabled}   >
                            <RadioCard.ItemHiddenInput />
                            <RadioCard.ItemControl>
                                <Center w="35px" h="35px" >
                                    <Icon fontSize={platformIcons.get(platform as Platform)?.size} color={globalConfig.inputMode === platform ? "white" : "fg.muted"}>
                                        {platformIcons.get(platform as Platform)?.icon}
                                    </Icon>
                                </Center>
                                <RadioCard.ItemText fontSize={"xs"} textAlign={"left"} >{label}</RadioCard.ItemText>
                            </RadioCard.ItemControl>
                        </RadioCard.Item>
                    ))}
                </VStack>
            </RadioCard.Root>
            </Card.Body>
        </Card.Root>
    );
}

