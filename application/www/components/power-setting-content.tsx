'use client';

import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { useLanguage } from '@/contexts/language-context';
import { GlobalConfig } from '@/types/gamepad-config';
import { Card, HStack, Slider, Text, VStack } from '@chakra-ui/react';

const wakeHoldOptions = [1000, 2000, 3000, 4000, 5000];
const autoStandbyOptions = [30000, 60000, 120000, 300000, 0];

const defaultPower = {
    wakeHoldMs: 3000,
    autoStandbyMs: 0,
};

function optionIndex(options: number[], value: number | undefined, fallback: number) {
    const index = options.indexOf(value ?? fallback);
    return index >= 0 ? index : Math.max(options.indexOf(fallback), 0);
}

function formatWakeHold(ms: number) {
    return `${ms / 1000}s`;
}

function formatAutoStandby(ms: number, noneLabel: string) {
    if (ms === 0) return noneLabel;
    if (ms < 120000) return `${ms / 1000}s`;
    return `${ms / 60000}min`;
}

export function PowerSettingContent(props: { disabled?: boolean }) {
    const { globalConfig, updateGlobalConfig } = useGamepadConfig();
    const { t } = useLanguage();
    const power = globalConfig.power ?? defaultPower;
    const wakeIndex = optionIndex(wakeHoldOptions, power.wakeHoldMs, defaultPower.wakeHoldMs);
    const autoIndex = optionIndex(autoStandbyOptions, power.autoStandbyMs, defaultPower.autoStandbyMs);

    const updatePower = async (nextPower: Partial<NonNullable<GlobalConfig['power']>>) => {
        await updateGlobalConfig({
            ...globalConfig,
            power: {
                ...power,
                ...nextPower,
            },
        });
    };

    const onWakeHoldChange = async (value: number) => {
        await updatePower({ wakeHoldMs: wakeHoldOptions[value] ?? defaultPower.wakeHoldMs });
    };

    const onAutoStandbyChange = async (value: number) => {
        await updatePower({ autoStandbyMs: autoStandbyOptions[value] ?? defaultPower.autoStandbyMs });
    };

    return (
        <Card.Root w="100%" size="sm">
            <Card.Header>
                <Card.Title fontSize="md">{t.POWER_TITLE}</Card.Title>
            </Card.Header>
            <Card.Body>
                <VStack align="stretch" gap={6}>
                    <HStack justifyContent="space-between">
                        <VStack align="start" gap={0}>
                            <Text fontSize="xs" color="fg.muted">Battery topology</Text>
                            <Text fontSize="xs">Single 1S2P pack</Text>
                        </VStack>
                        <VStack align="end" gap={0}>
                            <Text fontSize="xs" color="fg.muted">Board</Text>
                            <Text fontSize="xs">{globalConfig.hardware?.hardwareVersion ?? '2.0.0'}</Text>
                        </VStack>
                    </HStack>
                    <VStack align="stretch" gap={3}>
                        <HStack justifyContent="space-between">
                            <Text fontSize="xs" color="fg.muted">{t.POWER_WAKE_HOLD_LABEL}</Text>
                            <Text fontSize="xs" color="fg.muted">{formatWakeHold(wakeHoldOptions[wakeIndex])}</Text>
                        </HStack>
                        <Slider.Root
                            size="sm"
                            min={0}
                            max={wakeHoldOptions.length - 1}
                            step={1}
                            colorPalette="green"
                            disabled={props.disabled}
                            value={[wakeIndex]}
                            onValueChange={(detail) => void onWakeHoldChange(detail.value[0])}
                        >
                            <Slider.Control>
                                <Slider.Track>
                                    <Slider.Range />
                                </Slider.Track>
                                <Slider.Thumb index={0}>
                                    <Slider.HiddenInput />
                                </Slider.Thumb>
                                <Slider.Marks
                                    marks={wakeHoldOptions.map((item, index) => ({
                                        value: index,
                                        label: formatWakeHold(item),
                                    }))}
                                />
                            </Slider.Control>
                        </Slider.Root>
                    </VStack>

                    <VStack align="stretch" gap={3}>
                        <HStack justifyContent="space-between">
                            <Text fontSize="xs" color="fg.muted">{t.POWER_AUTO_STANDBY_LABEL}</Text>
                            <Text fontSize="xs" color="fg.muted">{formatAutoStandby(autoStandbyOptions[autoIndex], t.POWER_AUTO_STANDBY_NONE)}</Text>
                        </HStack>
                        <Slider.Root
                            size="sm"
                            min={0}
                            max={autoStandbyOptions.length - 1}
                            step={1}
                            colorPalette="green"
                            disabled={props.disabled}
                            value={[autoIndex]}
                            onValueChange={(detail) => void onAutoStandbyChange(detail.value[0])}
                        >
                            <Slider.Control>
                                <Slider.Track>
                                    <Slider.Range />
                                </Slider.Track>
                                <Slider.Thumb index={0}>
                                    <Slider.HiddenInput />
                                </Slider.Thumb>
                                <Slider.Marks
                                    marks={autoStandbyOptions.map((item, index) => ({
                                        value: index,
                                        label: formatAutoStandby(item, t.POWER_AUTO_STANDBY_NONE),
                                    }))}
                                />
                            </Slider.Control>
                        </Slider.Root>
                    </VStack>
                </VStack>
            </Card.Body>
        </Card.Root>
    );
}
