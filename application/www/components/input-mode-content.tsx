'use client';

import { PlatformLabelMap, Platform, ConnectionMode, platformForDisplay } from '@/types/gamepad-config';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { Center, Icon, RadioCard, SimpleGrid, Text, VStack } from '@chakra-ui/react';
import { useEffect } from 'react';
import { BsXbox } from "react-icons/bs";
import { FaWindows } from "react-icons/fa";
import { SiNintendoswitch, SiPlaystation4, SiPlaystation5   } from "react-icons/si";
import { useLanguage } from '@/contexts/language-context';
import { TitleLabel } from './ui/title-label';


export function InputModeSettingContent(props: {
        disabled?: boolean,
    }) {
    const { globalConfig, stageDeferredGlobalConfig } = useGamepadConfig();
    const { t } = useLanguage();
    const physicalRf = (globalConfig.physicalConnectionMode ?? globalConfig.connectionMode) === ConnectionMode.RF24G;
    const displayedInputMode = physicalRf
        ? Platform.XINPUT
        : globalConfig.inputMode
            ? platformForDisplay(globalConfig.inputMode)
            : Platform.XINPUT;

    const platformIcons = new Map<Platform, React.ReactNode>([
        [Platform.XINPUT, <FaWindows key={Platform.XINPUT} />],
        [Platform.PS4, <SiPlaystation4 key={Platform.PS4} />],
        [Platform.PS5, <SiPlaystation5 key={Platform.PS5} />],
        [Platform.XBOX, <BsXbox key={Platform.XBOX} />],
        [Platform.SWITCH, <SiNintendoswitch key={Platform.SWITCH} />],
    ]);

    useEffect(() => {
        if (physicalRf && globalConfig.inputMode !== Platform.XINPUT) {
            stageDeferredGlobalConfig({ inputMode: Platform.XINPUT });
        }
    }, [physicalRf, globalConfig.inputMode, stageDeferredGlobalConfig]);

    const onInputModeChange = (detail: { value: Platform }) => {
        if (physicalRf && detail.value !== Platform.XINPUT) {
            stageDeferredGlobalConfig({ ...globalConfig, inputMode: Platform.XINPUT });
            return;
        }
        stageDeferredGlobalConfig({ inputMode: detail.value as Platform });
    }

    return (
        <VStack as="section" aria-label={t.INPUT_MODE_TITLE} align="stretch" gap={3}>
            <VStack align="stretch" gap={1}>
                <TitleLabel title={t.INPUT_MODE_TITLE} />
                <Text fontSize="xs" color="fg.muted">{t.INPUT_MODE_HELPER}</Text>
            </VStack>
            <RadioCard.Root 
                value={displayedInputMode}
                orientation="horizontal"
                align="center"
                size="sm"
                variant={"solid"}
                colorPalette={"green"}
                width="100%"
                onValueChange={(detail) => onInputModeChange(detail as { value: Platform })}
            >
                <SimpleGrid columns={2} gap={2} width="100%">
                    {Array.from(PlatformLabelMap.entries()).map(([platform, { label }]) => (
                        <RadioCard.Item
                            key={platform}
                            value={platform}
                            w="100%"
                            disabled={props.disabled || (physicalRf && platform !== Platform.XINPUT)}
                        >
                            <RadioCard.ItemHiddenInput />
                            <RadioCard.ItemControl minH="50px" px={3} py={1} gap={3}>
                                <Center flexShrink={0} w="32px" h="32px">
                                    <Icon fontSize="28px" color={displayedInputMode === platform ? "white" : "fg.muted"}>
                                        {platformIcons.get(platform)}
                                    </Icon>
                                </Center>
                                <RadioCard.ItemText fontSize="sm" textAlign="left" letterSpacing="0.04em">
                                    {label}
                                </RadioCard.ItemText>
                            </RadioCard.ItemControl>
                        </RadioCard.Item>
                    ))}
                </SimpleGrid>
            </RadioCard.Root>
        </VStack>
    );
}

