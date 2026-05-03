'use client';

import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { Card, RadioCard, VStack, HStack, Text } from '@chakra-ui/react';
import { ConnectionMode, Platform, WirelessReportRate } from '@/types/gamepad-config';

const rateOptions: WirelessReportRate[] = [
    WirelessReportRate.RATE_1K,
    WirelessReportRate.RATE_2K,
    WirelessReportRate.RATE_4K,
    WirelessReportRate.RATE_8K,
];

export function ConnectionModeSettingContent(props: { disabled?: boolean }) {
    const { globalConfig, updateGlobalConfig } = useGamepadConfig();
    const mode = globalConfig.connectionMode ?? ConnectionMode.USB;
    const rate = globalConfig.wirelessReportRate ?? WirelessReportRate.RATE_1K;

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

    const onRateChange = async (value: WirelessReportRate) => {
        await updateGlobalConfig({
            ...globalConfig,
            connectionMode: ConnectionMode.RF24G,
            wirelessReportRate: value,
            inputMode: Platform.XINPUT,
        });
    };

    return (
        <Card.Root w="100%">
            <Card.Header>
                <Card.Title fontSize="md">Connection Mode</Card.Title>
            </Card.Header>
            <Card.Body>
                <VStack align="stretch" gap={3}>
                    <RadioCard.Root
                        value={mode}
                        orientation="horizontal"
                        variant="solid"
                        colorPalette="green"
                        onValueChange={(d) => void onModeChange((d as { value: ConnectionMode }).value)}
                    >
                        <HStack w="100%">
                            <RadioCard.Item value={ConnectionMode.USB} w="100%" disabled={props.disabled}>
                                <RadioCard.ItemHiddenInput />
                                <RadioCard.ItemControl>
                                    <RadioCard.ItemText>USB</RadioCard.ItemText>
                                </RadioCard.ItemControl>
                            </RadioCard.Item>
                            <RadioCard.Item value={ConnectionMode.RF24G} w="100%" disabled={props.disabled}>
                                <RadioCard.ItemHiddenInput />
                                <RadioCard.ItemControl>
                                    <RadioCard.ItemText>Wireless 2.4G</RadioCard.ItemText>
                                </RadioCard.ItemControl>
                            </RadioCard.Item>
                        </HStack>
                    </RadioCard.Root>

                    {mode === ConnectionMode.RF24G && (
                        <VStack align="stretch" gap={2}>
                            <Text fontSize="xs" color="fg.muted">Report Rate</Text>
                            <RadioCard.Root
                                value={rate}
                                orientation="horizontal"
                                variant="subtle"
                                colorPalette="green"
                                onValueChange={(d) => void onRateChange((d as { value: WirelessReportRate }).value)}
                            >
                                <HStack w="100%" wrap="wrap">
                                    {rateOptions.map((item) => (
                                        <RadioCard.Item key={item} value={item} w="70px" disabled={props.disabled}>
                                            <RadioCard.ItemHiddenInput />
                                            <RadioCard.ItemControl>
                                                <RadioCard.ItemText>{item}</RadioCard.ItemText>
                                            </RadioCard.ItemControl>
                                        </RadioCard.Item>
                                    ))}
                                </HStack>
                            </RadioCard.Root>
                        </VStack>
                    )}
                </VStack>
            </Card.Body>
        </Card.Root>
    );
}
