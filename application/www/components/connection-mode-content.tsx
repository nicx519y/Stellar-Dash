'use client';

import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { Card, HStack, Slider, Switch, Text, VStack } from '@chakra-ui/react';
import { GlobalConfig, WirelessReportRate } from '@/types/gamepad-config';
import { useLanguage } from '@/contexts/language-context';

const rateOptions: WirelessReportRate[] = [
    WirelessReportRate.RATE_1K,
    WirelessReportRate.RATE_2K,
    WirelessReportRate.RATE_4K,
    WirelessReportRate.RATE_8K,
];

const autoSleepOptions = [10000, 30000, 60000, 120000, 300000];
const wakeHoldOptions = [1000, 2000, 3000, 4000, 5000];
const defaultPower: NonNullable<GlobalConfig['power']> = {
    wakeHoldMs: 3000,
    autoStandbyMs: 300000,
};

function optionIndex<T>(options: T[], value: T | undefined, fallback: T) {
    const index = options.indexOf(value ?? fallback);
    return index >= 0 ? index : Math.max(options.indexOf(fallback), 0);
}

function formatAutoSleep(ms: number) {
    if (ms < 120000) return `${ms / 1000}s`;
    return `${ms / 60000}min`;
}

export function ConnectionModeSettingContent(props: { disabled?: boolean }) {
    const { globalConfig, updateGlobalConfig } = useGamepadConfig();
    const { t } = useLanguage();
    const rate = globalConfig.wirelessReportRate ?? WirelessReportRate.RATE_1K;
    const rateIndex = Math.max(rateOptions.indexOf(rate), 0);
    const power = globalConfig.power ?? defaultPower;
    const autoSleepIndex = optionIndex(
        autoSleepOptions,
        power.autoStandbyMs,
        defaultPower.autoStandbyMs,
    );
    const wakeHoldIndex = optionIndex(
        wakeHoldOptions,
        power.wakeHoldMs,
        defaultPower.wakeHoldMs,
    );

    const onRateChange = async (value: number) => {
        const nextRate = rateOptions[value] ?? WirelessReportRate.RATE_1K;
        await updateGlobalConfig({
            ...globalConfig,
            wirelessReportRate: nextRate,
        });
    };

    const onAutoSleepChange = async (value: number) => {
        await updateGlobalConfig({
            ...globalConfig,
            power: {
                ...power,
                autoStandbyMs: autoSleepOptions[value] ?? defaultPower.autoStandbyMs,
            },
        });
    };

    const onWakeHoldChange = async (value: number) => {
        await updateGlobalConfig({
            ...globalConfig,
            power: {
                ...power,
                wakeHoldMs: wakeHoldOptions[value] ?? defaultPower.wakeHoldMs,
            },
        });
    };

    const onAutoCalibrationChange = async (checked: boolean) => {
        await updateGlobalConfig({
            ...globalConfig,
            autoCalibrationEnabled: checked,
        });
    };

    return (
        <Card.Root w="100%" size="sm">
            <Card.Header>
                <Card.Title fontSize="md">{t.CONNECTION_MODE_TITLE}</Card.Title>
            </Card.Header>
            <Card.Body>
                <VStack align="stretch" gap={7}>
                    <VStack align="stretch" gap={3}>
                        <HStack justifyContent="space-between">
                            <Text fontSize="xs" color="fg.muted">{t.CONNECTION_MODE_REPORT_RATE_LABEL}</Text>
                            <Text fontSize="xs" color="fg.muted">{rate}</Text>
                        </HStack>
                        <Text fontSize="2xs" lineHeight="1.2" color="fg.subtle">
                            {t.CONNECTION_MODE_REPORT_RATE_HELPER}
                        </Text>
                        <Slider.Root
                            size="sm"
                            min={0}
                            max={rateOptions.length - 1}
                            step={1}
                            colorPalette="green"
                            disabled={props.disabled}
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

                    <VStack align="stretch" gap={3}>
                        <HStack justifyContent="space-between">
                            <Text fontSize="xs" color="fg.muted">{t.POWER_AUTO_STANDBY_LABEL}</Text>
                            <Text fontSize="xs" color="fg.muted">
                                {formatAutoSleep(autoSleepOptions[autoSleepIndex])}
                            </Text>
                        </HStack>
                        <Slider.Root
                            size="sm"
                            min={0}
                            max={autoSleepOptions.length - 1}
                            step={1}
                            colorPalette="green"
                            disabled={props.disabled}
                            value={[autoSleepIndex]}
                            onValueChange={(detail) => void onAutoSleepChange(detail.value[0])}
                        >
                            <Slider.Control>
                                <Slider.Track>
                                    <Slider.Range />
                                </Slider.Track>
                                <Slider.Thumb index={0}>
                                    <Slider.HiddenInput />
                                </Slider.Thumb>
                                <Slider.Marks
                                    marks={autoSleepOptions.map((item, index) => ({
                                        value: index,
                                        label: formatAutoSleep(item),
                                    }))}
                                />
                            </Slider.Control>
                        </Slider.Root>
                    </VStack>

                    <VStack align="stretch" gap={3}>
                        <HStack justifyContent="space-between">
                            <Text fontSize="xs" color="fg.muted">{t.POWER_WAKE_HOLD_LABEL}</Text>
                            <Text fontSize="xs" color="fg.muted">
                                {wakeHoldOptions[wakeHoldIndex] / 1000}s
                            </Text>
                        </HStack>
                        <Text fontSize="2xs" lineHeight="1.2" color="fg.subtle">
                            {t.POWER_WAKE_HOLD_HELPER}
                        </Text>
                        <Slider.Root
                            size="sm"
                            min={0}
                            max={wakeHoldOptions.length - 1}
                            step={1}
                            colorPalette="green"
                            disabled={props.disabled}
                            value={[wakeHoldIndex]}
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
                                        label: `${item / 1000}s`,
                                    }))}
                                />
                            </Slider.Control>
                        </Slider.Root>
                    </VStack>

                    <HStack justifyContent="space-between" alignItems="center">
                        <VStack align="flex-start" gap={1}>
                            <Text fontSize="xs" color="fg.muted">{t.AUTO_CALIBRATION_TITLE}</Text>
                            <Text fontSize="2xs" lineHeight="1.2" color="fg.subtle">
                                {t.AUTO_CALIBRATION_HELPER}
                            </Text>
                        </VStack>
                        <Switch.Root
                            colorPalette="green"
                            checked={globalConfig.autoCalibrationEnabled ?? false}
                            disabled={props.disabled}
                            onCheckedChange={(detail) => void onAutoCalibrationChange(detail.checked)}
                        >
                            <Switch.HiddenInput />
                            <Switch.Control><Switch.Thumb /></Switch.Control>
                        </Switch.Root>
                    </HStack>
                </VStack>
            </Card.Body>
        </Card.Root>
    );
}
