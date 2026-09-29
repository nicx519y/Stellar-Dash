import { Box, Button, HStack, Spinner, Text } from "@chakra-ui/react";
import { openConfirm as openFinishConfirmDialog } from "@/components/dialog-confirm";
import { closeDialog as closeFinishDialog, openDialog as openFinishDialog, updateDialogMessage as updateFinishDialogMessage } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { configuredTransportMode } from "@/lib/device-transport";
import { useEffect, useLayoutEffect, useRef, useState } from "react";
import { useConfigSyncStatus } from "@/components/config-sync-status";


export function FinishConfigButton(
    props: {
        disabled?: boolean,
    }
) {
    const { t } = useLanguage();
    const {
        exitWebConfig,
        setUserRebooting,
        flushDeferredConfig,
        terminateWebConfigActivities,
        deviceConnected,
        setError,
    } = useGamepadConfig();
    const [closing, setClosing] = useState(false);
    const [hovered, setHovered] = useState(false);
    const [labelWidth, setLabelWidth] = useState<number | null>(null);
    const measureRef = useRef<HTMLSpanElement>(null);
    const { status, text, background, foreground, Icon, busy, zh } = useConfigSyncStatus();
    const actionText = zh ? '点击结束配置并开始游戏' : 'Click to finish configuration';
    const label = hovered && !props.disabled && !closing ? actionText : text;

    useLayoutEffect(() => {
        const measure = measureRef.current;
        if (!measure) return;
        const updateWidth = () => setLabelWidth(Math.ceil(measure.getBoundingClientRect().width));
        updateWidth();
        const observer = new ResizeObserver(updateWidth);
        observer.observe(measure);
        return () => observer.disconnect();
    }, [label]);

    useEffect(() => {
        if (!deviceConnected) return;
        closeFinishDialog('finish-config-success');
    }, [deviceConnected]);

    return (
        <Button
            data-testid="config-sync-finish-button"
            data-sync-state={status}
            disabled={props.disabled || closing}
            loading={closing}
            variant="solid"
            size="xs"
            minW={0}
            width={labelWidth === null ? 'max-content' : `${labelWidth + 48}px`}
            minH="36px"
            px={3}
            bg={background}
            color={foreground}
            borderRadius="md"
            overflow="hidden"
            whiteSpace="nowrap"
            transition="width 240ms ease, background-color 180ms ease"
            _hover={{ bg: background, filter: 'brightness(1.08)' }}
            _disabled={{ opacity: 1 }}
            _motionReduce={{ transition: 'none' }}
            onMouseEnter={() => setHovered(true)}
            onMouseLeave={() => setHovered(false)}
            onFocus={() => setHovered(true)}
            onBlur={() => setHovered(false)}
            aria-label={label}
            onClick={async () => {
                const confirmed = await openFinishConfirmDialog({
                    title: t.DIALOG_FINISH_CONFIRM_TITLE,
                    message: t.DIALOG_FINISH_CONFIRM_MESSAGE,
                });

                if (confirmed) {
                    setClosing(true);
                    const savingDialogId = openFinishDialog({
                        id: 'config-saving',
                        title: t.DIALOG_CONFIG_SAVING_TITLE,
                        status: "info",
                        message: t.DIALOG_CONFIG_SAVING_MESSAGE,
                        loading: true,
                    });
                    try {
                        await flushDeferredConfig(
                            async () => {
                                console.log('配置与设备请求队列已保存，正在退出 WebConfig 模式');
                                updateFinishDialogMessage(savingDialogId, t.DIALOG_CONFIG_EXITING_MESSAGE);
                                const expectsDeviceDisconnect = configuredTransportMode() !== 'mock';
                                if (expectsDeviceDisconnect) setUserRebooting(true);
                                try {
                                    await exitWebConfig();
                                } catch (error) {
                                    if (expectsDeviceDisconnect) setUserRebooting(false);
                                    throw error;
                                }
                            },
                            false,
                            terminateWebConfigActivities,
                        );
                        closeFinishDialog(savingDialogId);

                        if (configuredTransportMode() === 'mock') {
                            const dialogId = openFinishDialog({
                                id: 'finish-config-success',
                                title: t.DIALOG_FINISH_SUCCESS_TITLE,
                                status: "success",
                                message: t.DIALOG_FINISH_SUCCESS_MESSAGE,
                                buttons: [{
                                    text: t.BUTTON_CONFIRM,
                                    colorPalette: "green",
                                    onClick: () => closeFinishDialog(dialogId),
                                }],
                            });
                            return;
                        }

                        openFinishDialog({
                            id: 'finish-config-success',
                            title: t.DIALOG_FINISH_SUCCESS_TITLE,
                            status: "warning",
                            message: t.DIALOG_FINISH_SUCCESS_MESSAGE,
                        });
                    } catch (error) {
                        closeFinishDialog(savingDialogId);
                        setError(error instanceof Error ? error.message : '保存配置或退出 WebConfig 失败');
                    } finally {
                        setClosing(false);
                    }
                }

            }}
        >
            <HStack gap={2} flexShrink={0}>
                <Box w={4} h={4} display="grid" placeItems="center" flexShrink={0}>
                    {busy ? <Spinner size="xs" /> : <Box as={Icon} />}
                </Box>
                <Text as="span" fontSize="xs" fontWeight="medium" whiteSpace="nowrap" aria-live="polite">{label}</Text>
            </HStack>
            <Box as="span" ref={measureRef} position="absolute" visibility="hidden" pointerEvents="none"
                aria-hidden="true" fontSize="xs" fontWeight="medium" whiteSpace="nowrap">{label}</Box>
        </Button>
    )
}
