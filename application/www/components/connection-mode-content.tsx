'use client';

import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { Card, HStack, Slider, Text, VStack } from '@chakra-ui/react';
import { ConnectionMode, Platform, WirelessReportRate } from '@/types/gamepad-config';
import { SegmentedControl } from './ui/segmented-control';
import { useLanguage } from '@/contexts/language-context';

const rateOptions: WirelessReportRate[] = [
    WirelessReportRate.RATE_1K,
    WirelessReportRate.RATE_2K,
    WirelessReportRate.RATE_4K,
    WirelessReportRate.RATE_8K,
];

export function ConnectionModeSettingContent(props: { disabled?: boolean }) {
    const { globalConfig, updateGlobalConfig } = useGamepadConfig();
    const { t } = useLanguage();
    const mode = globalConfig.connectionMode ?? ConnectionMode.USB;
    const rate = globalConfig.wirelessReportRate ?? WirelessReportRate.RATE_1K;
    const rateIndex = Math.max(rateOptions.indexOf(rate), 0);

    const onModeChange = async (value: ConnectionMode) => {
        if (value === ConnectionMode.RF24G) {
            await updateGlobalConfig({
                ...globalConfig,
                connectionMode: ConnectionMode.RF24G,
                wirelessReportRate: rate,
                inputMode: Platform.XINPUT,
            });
            return;
        }
        await updateGlobalConfig({
            ...globalConfig,
            connectionMode: ConnectionMode.USB,
        });
    };

    const onRateChange = async (value: number) => {
        const nextRate = rateOptions[value] ?? WirelessReportRate.RATE_1K;
        await updateGlobalConfig({
            ...globalConfig,
            connectionMode: ConnectionMode.RF24G,
            wirelessReportRate: nextRate,
            inputMode: Platform.XINPUT,
        });
    };

    return (
        <Card.Root w="100%" size="sm">
            <Card.Header>
                <Card.Title fontSize="md">{t.CONNECTION_MODE_TITLE}</Card.Title>
            </Card.Header>
            <Card.Body>
                <VStack align="stretch" gap={3}>
                    <SegmentedControl
                        size="sm"
                        width="100%"
                        css={{
                            '& [data-part="indicator"]': {
                                bg: 'green.solid',
                            },
                            '& [data-part="item"]': {
                                flex: 1,
                                justifyContent: 'center',
                            },
                            '& [data-part="item"][data-state="checked"]': {
                                color: 'green.contrast',
                            },
                        }}
                        value={mode}
                        colorPalette="green"
                        disabled={props.disabled}
                        items={[
                            { value: ConnectionMode.USB, label: 'USB' },
                            { value: ConnectionMode.RF24G, label: '2.4G' },
                        ]}
                        onValueChange={(detail) => void onModeChange((detail.value ?? ConnectionMode.USB) as ConnectionMode)}
                    />

                    <VStack align="stretch" gap={3} pt={1}>
                        <HStack justifyContent="space-between">
                            <Text fontSize="xs" color="fg.muted">{t.CONNECTION_MODE_REPORT_RATE_LABEL}</Text>
                            <Text fontSize="xs" color="fg.muted">{rate}</Text>
                        </HStack>
                        <Slider.Root
                            size="sm"
                            min={0}
                            max={rateOptions.length - 1}
                            step={1}
                            colorPalette="green"
                            disabled={props.disabled || mode === ConnectionMode.USB}
                            value={[rateIndex]}
                            onValueChange={(detail) => void onRateChange(detail.value[0])}
                        >
                            <Slider.Control>
                                <Slider.Track>
                                    <Slider.Range />
                                </Slider.Track>
                                <Slider.Thumb index={0}>
                                    <Slider.HiddenInput />
                                </Slider.Thumb>
                                <Slider.Marks
                                    marks={rateOptions.map((item, index) => ({
                                        value: index,
                                        label: item,
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
