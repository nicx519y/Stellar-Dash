import { Box, Button, HStack, Spinner, Text } from "@chakra-ui/react";
import { openConfirm as openFinishConfirmDialog } from "@/components/dialog-confirm";
import { closeDialog as closeFinishDialog, openDialog as openFinishDialog, updateDialogMessage as updateFinishDialogMessage } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadFields } from "@/contexts/gamepad-config-context";
import { configuredTransportMode } from "@/lib/device-transport";
import { useState } from "react";
import { toaster } from "@/components/ui/toaster";
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
        setError,
    } = useGamepadFields("exitWebConfig", "setUserRebooting", "flushDeferredConfig", "terminateWebConfigActivities", "setError");
    const [closing, setClosing] = useState(false);
    const [hovered, setHovered] = useState(false);
    const { status, text, background, foreground, Icon, busy, zh } = useConfigSyncStatus();
    const actionText = zh ? '点击结束配置并开始游戏' : 'Click to finish configuration';
    const label = hovered && !props.disabled && !closing ? actionText : text;

    return (
        <Button
            data-testid="config-sync-finish-button"
            data-sync-state={status}
            disabled={props.disabled || closing}
            loading={closing}
            variant="solid"
            size="xs"
            minW={0}
            width="full"
            h={{ base: "48px", sm: "36px" }}
            px={3}
            bg={background}
            color={foreground}
            borderRadius="md"
            overflow="hidden"
            whiteSpace="nowrap"
            transition="background-color 180ms ease"
            _hover={{ bg: background, filter: 'brightness(1.08)' }}
            _disabled={{ opacity: 1 }}
            _motionReduce={{ transition: 'none' }}
            onMouseEnter={() => setHovered(true)}
            onMouseLeave={() => setHovered(false)}
            onFocus={() => setHovered(true)}
            onBlur={() => setHovered(false)}
            aria-label={label}
            title={label}
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

                        toaster.success({
                            title: t.DIALOG_FINISH_SUCCESS_TITLE,
                            description: t.DIALOG_FINISH_SUCCESS_MESSAGE,
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
            <HStack gap={2} minW={0} w="full" justify="center">
                <Box w={4} h={4} display="grid" placeItems="center" flexShrink={0}>
                    {busy ? <Spinner size="xs" /> : <Box as={Icon} />}
                </Box>
                <Text as="span" fontSize="xs" fontWeight="medium" whiteSpace="normal" lineHeight="14px" aria-live="polite">{label}</Text>
            </HStack>
        </Button>
    )
}
