'use client';

import { useGamepadFields } from '@/contexts/gamepad-config-context';
import { HStack, Slider, Switch, Text, VStack } from '@chakra-ui/react';
import { GlobalConfig, WirelessReportRate } from '@/types/gamepad-config';
import { useLanguage } from '@/contexts/language-context';
import { TitleLabel } from './ui/title-label';
import { SettingDescription } from './ui/setting-description';
import { RxReceiverSlot } from './rx-receiver-slot';
import { InputModeSettingContent } from './input-mode-content';
import { connectionModeLabel } from '@/lib/connection-mode';

const rateOptions: WirelessReportRate[] = [
    WirelessReportRate.RATE_1K,
    WirelessReportRate.RATE_2K,
    WirelessReportRate.RATE_4K,
    WirelessReportRate.RATE_8K,
];

const autoSleepOptions = [10000, 30000, 60000, 120000, 300000];
const defaultPower: NonNullable<GlobalConfig['power']> = {
    autoSleepEnabled: false,
    autoSleepSupported: false,
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

export function ConnectionAndPowerBasicSettingContent(props: { disabled?: boolean }) {
    const { globalConfig, stageDeferredGlobalConfig } = useGamepadFields("globalConfig", "stageDeferredGlobalConfig");
    const { t, currentLanguage } = useLanguage();
    const rate = globalConfig.wirelessReportRate ?? WirelessReportRate.RATE_1K;
    const rateIndex = Math.max(rateOptions.indexOf(rate), 0);
    const power = globalConfig.power ?? defaultPower;
    const sleepSupported = power.autoSleepSupported === true;
    const sleepEnabled = power.autoSleepEnabled === true;
    const autoSleepIndex = optionIndex(
        autoSleepOptions,
        power.autoStandbyMs,
        defaultPower.autoStandbyMs,
    );
    const onRateChange = async (value: number) => {
        const nextRate = rateOptions[value] ?? WirelessReportRate.RATE_1K;
        stageDeferredGlobalConfig({
            ...globalConfig,
            wirelessReportRate: nextRate,
        });
    };

    const onAutoSleepChange = async (value: number) => {
        stageDeferredGlobalConfig({
            ...globalConfig,
            power: {
                ...power,
                autoStandbyMs: autoSleepOptions[value] ?? defaultPower.autoStandbyMs,
            },
        });
    };

    return (
        <VStack align="stretch" gap={7} width="100%">
            <SettingDescription text={t.SETTINGS_BASIC_HELPER_TEXT} fontSize="14px" />

            <InputModeSettingContent disabled={props.disabled} />

            <VStack as="section" aria-label={t.CONNECTION_MODE_TITLE} align="stretch" gap={6}>
                <TitleLabel title={t.CONNECTION_MODE_TITLE} />
                <VStack align="stretch" gap={2}>
                    <Text role="status" fontSize="sm">{connectionModeLabel(globalConfig, currentLanguage)}</Text>
                    <Text fontSize="xs" color="fg.muted">
                        {currentLanguage === 'zh'
                            ? '在设备屏幕的 Connection 菜单选择 USB 或 2.4G RF，保存后重启生效。退出网页配置后恢复记忆的模式。'
                            : 'Choose USB or 2.4G RF in the device screen’s Connection menu. The device saves and restarts. Exiting WebConfig restores the saved mode.'}
                    </Text>
                </VStack>

                <RxReceiverSlot disabled={props.disabled} />

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
                        width="100%"
                        minW={0}
                        min={0}
                        max={rateOptions.length - 1}
                        step={1}
                        colorPalette="green"
                        disabled={props.disabled}
                        value={[rateIndex]}
                        onValueChange={(detail) => {
                            void onRateChange(detail.value[0]).catch(() => undefined);
                        }}
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

            <TitleLabel title={t.POWER_TITLE} />

            <VStack align="stretch" gap={3}>
                <HStack justifyContent="space-between">
                    <Switch.Root colorPalette="green"
                        checked={sleepEnabled}
                        disabled={props.disabled || !sleepSupported}
                        onCheckedChange={({ checked }) => stageDeferredGlobalConfig({
                            ...globalConfig,
                            power: { ...power, autoSleepEnabled: checked },
                        })}
                    >
                        <Switch.HiddenInput />
                        <Switch.Control><Switch.Thumb /></Switch.Control>
                        <Switch.Label>{t.POWER_AUTO_STANDBY_LABEL}</Switch.Label>
                    </Switch.Root>
                    <Text fontSize="xs" color="fg.muted">
                        {formatAutoSleep(autoSleepOptions[autoSleepIndex])}
                    </Text>
                </HStack>
                <Text fontSize="2xs" lineHeight="1.2" color="fg.subtle">
                    {sleepSupported ? t.POWER_AUTO_STANDBY_HELPER : t.POWER_AUTO_SLEEP_UNSUPPORTED}
                </Text>
                <Slider.Root
                    size="sm"
                    width="100%"
                    minW={0}
                    min={0}
                    max={autoSleepOptions.length - 1}
                    step={1}
                    colorPalette="green"
                    disabled={props.disabled || !sleepSupported || !sleepEnabled}
                    value={[autoSleepIndex]}
                    onValueChange={(detail) => {
                        void onAutoSleepChange(detail.value[0]).catch(() => undefined);
                    }}
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
        </VStack>
    );
}
