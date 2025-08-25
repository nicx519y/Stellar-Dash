import { useLanguage } from "@/contexts/language-context";
import { AbsoluteCenter, Alert, Badge, Box, Card, Center, Icon, List, ProgressCircle, Spinner, Text, Button, VStack } from "@chakra-ui/react";
import { useEffect, useMemo, useState } from "react";
import { CiCircleCheck, CiCircleRemove, CiSaveUp1 } from "react-icons/ci";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { FirmwarePackage } from "@/types/types";
import { openDialog as openSuccessDialog, updateDialogMessage } from "./dialog-cannot-close";

enum UpdateStatus {
    Idle = 0,
    NoUpdate = 1,
    UpdateAvailable = 2,
    Updating = 3,
    UpdateFailed = 4,
    UpdateSuccess = 5,
}

enum ProgressPercent {
    Downloading = 0.4,
    Uploading = 0.58,
}

const REFRESH_PAGE_DELAY_SECONDS = 5; // 固件更新成功后，延迟5秒自动刷新页面
const RECONNECT_DELAY_SECONDS = 3; // 固件更新失败后，延迟3秒重新连接websocket

export function FirmwareContent() {
    const { t } = useLanguage();
    const [_updateProgress, _setUpdateProgress] = useState(0);
    const [updateStatus, setUpdateStatus] = useState(UpdateStatus.Idle);
    const [countdownSeconds, setCountdownSeconds] = useState(REFRESH_PAGE_DELAY_SECONDS);
    const [successDialogId, setSuccessDialogId] = useState<string | null>(null);
    const {
        firmwareInfo,
        fetchFirmwareMetadata,
        firmwareUpdateInfo,
        checkFirmwareUpdate,
        downloadFirmwarePackage,
        uploadFirmwareToDevice,
        dataIsReady,
        setFirmwareUpdating,
        firmwareUpdating,
        wsConnected,
        connectWebSocket,
        setFinishConfigDisabled,
    } = useGamepadConfig();
    const currentVersion = useMemo(() => firmwareInfo?.firmware?.version || "0.0.0", [firmwareInfo]);
    const latestVersion = useMemo(() => firmwareUpdateInfo?.latestVersion ? firmwareUpdateInfo.latestVersion : firmwareInfo?.firmware?.version || "0.0.0", [firmwareUpdateInfo, firmwareInfo]);
    const latestFirmwareUpdateLog = useMemo(() => firmwareUpdateInfo?.latestFirmware?.desc.split(/\s{2,}/) || [], [firmwareUpdateInfo]);

    // useEffect(() => {
    //     if(wsConnected) {
    //         fetchFirmwareMetadata();
    //     }
    // }, [wsConnected]);

    const firmwrareCurrentVersion = useMemo(() => {
        return firmwareInfo?.firmware?.version || "";
    }, [firmwareInfo?.firmware.version]);

    useEffect(() => {
        if (dataIsReady && firmwrareCurrentVersion != "") {
            checkFirmwareUpdate(firmwrareCurrentVersion);
        } else {
            console.log('firmwareInfo is null');
        }
    }, [firmwrareCurrentVersion, dataIsReady]);

    useEffect(() => {
        if (firmwareUpdateInfo) {

            // 如果正在更新，则根据固件更新信息判断更新状态
            if (updateStatus === UpdateStatus.Updating) {
                if (firmwareUpdateInfo.updateAvailable) {
                    setUpdateStatus(UpdateStatus.UpdateFailed);
                } else {
                    setUpdateStatus(UpdateStatus.UpdateSuccess);
                }
            }

            // 如果处于空闲状态，则根据固件更新信息判断更新状态
            if (updateStatus === UpdateStatus.Idle) {
                if (firmwareUpdateInfo.updateAvailable) {
                    setUpdateStatus(UpdateStatus.UpdateAvailable);
                } else {
                    setUpdateStatus(UpdateStatus.NoUpdate);
                }
            }
        }
    }, [firmwareUpdateInfo]);

    // 倒计时效果和对话框显示
    useEffect(() => {
        if (updateStatus === UpdateStatus.UpdateSuccess) {
            // 重置倒计时
            setCountdownSeconds(REFRESH_PAGE_DELAY_SECONDS);

            // 显示成功对话框（只显示一次）
            const dialogId = openSuccessDialog({
                title: t.SETTINGS_FIRMWARE_UPDATE_SUCCESS_TITLE,
                message: t.SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE.replace("{seconds}", REFRESH_PAGE_DELAY_SECONDS.toString()),
                buttons: [
                    {
                        text: t.SETTINGS_FIRMWARE_UPDATE_SUCCESS_BUTTON,
                        onClick: refreshBrowser,
                    }
                ]
            });
            setSuccessDialogId(dialogId);

            // 开始倒计时
            const countdownInterval = setInterval(() => {
                setCountdownSeconds(prev => {
                    if (prev <= 1) {
                        clearInterval(countdownInterval);
                        refreshBrowser(); // 倒计时结束自动刷新
                        return 0;
                    }
                    return prev - 1;
                });
            }, 1000);

            // 清理函数
            return () => clearInterval(countdownInterval);
        }
    }, [updateStatus]);

    // 更新对话框中的倒计时消息
    useEffect(() => {
        if (successDialogId && countdownSeconds > 0) {
            updateDialogMessage(successDialogId, t.SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE.replace("{seconds}", countdownSeconds.toString()));
        }
    }, [countdownSeconds, successDialogId]);

    // webcosket断开，如果是在固件更新中，则重新连接websocket，并延迟检查固件更新状态
    useEffect(() => {
        if (!wsConnected && firmwareUpdating) {
            console.log('firmware: reconnect websocket.');
            setTimeout(connectWebSocket, RECONNECT_DELAY_SECONDS);
        } else if (wsConnected && firmwareUpdating) {
            console.log('firmware: check update status.');
            checkUpdateStatusLoop();
        }

    }, [wsConnected, firmwareUpdating]);

    // 当固件更新状态改变时，更新完成配置按钮的禁用状态
    useEffect(() => {
        if (updateStatus === UpdateStatus.Updating) {
            setFinishConfigDisabled(true);
        } else {
            setFinishConfigDisabled(false);
        }
        return () => {
            setFinishConfigDisabled(false);
        }

    }, [updateStatus]);

    const refreshBrowser = () => {
        window.location.reload();
    };

    const upgradeFirmware = async () => {
        if (firmwareUpdateInfo && firmwareUpdateInfo.updateAvailable) {
            // 获取当前固件的槽位，如果当前固件是A槽位，则下载B槽位的固件，反之亦然
            const slot = firmwareInfo?.firmware?.currentSlot == 'A' ? 'slotB' : 'slotA';
            console.log('Begin to downloadFirmware url: ', firmwareUpdateInfo?.latestFirmware?.[slot]?.downloadUrl);
            setUpdateStatus(UpdateStatus.Updating);

            try {
                // 下载并解压固件包
                const firmwarePackage: FirmwarePackage = await downloadFirmwarePackage(firmwareUpdateInfo?.latestFirmware?.[slot]?.downloadUrl, (progress) => {
                    console.log('progress: ', progress);
                    if (progress.stage === 'downloading') {
                        _setUpdateProgress(progress.progress * ProgressPercent.Downloading);
                    } else if (progress.stage === 'extracting') {
                        _setUpdateProgress(progress.progress * ProgressPercent.Downloading);
                    } else if (progress.stage === 'downcompleted') {
                        _setUpdateProgress(progress.progress * ProgressPercent.Downloading);
                    } else if (progress.stage === 'failed') {
                        setUpdateStatus(UpdateStatus.UpdateFailed);
                    }
                });

                const nowProgress = ProgressPercent.Downloading * 100;

                // 上传固件包到设备
                await uploadFirmwareToDevice(firmwarePackage, (progress) => {
                    console.log('progress: ', progress);
                    if (progress.stage === 'uploading') {
                        _setUpdateProgress(nowProgress + progress.progress * ProgressPercent.Uploading);
                    } else if (progress.stage === 'uploadcompleted') { // 上传完成，等待设备重启，重启完成后，设置固件更新状态为成功
                        _setUpdateProgress(nowProgress + progress.progress * ProgressPercent.Uploading);
                        setFirmwareUpdating(true); // 标识状态，不展示重连提示，用于等待固件上传完成做状态检查

                    } else if (progress.stage === 'failed') {
                        setUpdateStatus(UpdateStatus.UpdateFailed);
                    }
                });

            } catch {

                setUpdateStatus(UpdateStatus.UpdateFailed);

            }

        }
    }

    const checkUpdateStatusLoop = async () => {
        try {
            await fetchFirmwareMetadata();

        } catch {
            // 如果请求失败，则2秒后重试
            setTimeout(() => {
                checkUpdateStatusLoop();
            }, 2000);
        }
    }



    return (
        <Center p="18px">
            <style>
                {`
                    @keyframes bounce {
                        0%, 20%, 50%, 80%, 100% {
                            transform: translateY(0);
                        }
                        40% {
                            transform: translateY(-15px);
                        }
                        60% {
                            transform: translateY(-7px);
                        }
                    }
                    
                    .bounce-icon {
                        animation: bounce 1.5s ease-in-out infinite;
                    }
                `}
            </style>



            <Center display={(dataIsReady && !firmwareUpdateInfo) ? "block" : "none"} >
                <Spinner
                    color="green.500"
                    size="xl"
                />
            </Center>

            <Center display={firmwareUpdateInfo ? "block" : "none"} w="700px" >
                <VStack gap="10px" >
                    <Center w="full" height="160px"  >
                        {/* 没有更新 */}
                        {updateStatus === UpdateStatus.NoUpdate && (
                            <Icon color="green.600" >
                                <CiCircleCheck size="140px" />
                            </Icon>
                        )}
                        {/* 有更新 */}
                        {updateStatus === UpdateStatus.UpdateAvailable && (
                            <Icon color="yellow" cursor="pointer" className="bounce-icon" onClick={upgradeFirmware}>
                                <CiSaveUp1 size="150px" />
                            </Icon>
                        )}
                        {/* 更新失败 */}
                        {updateStatus === UpdateStatus.UpdateFailed && (
                            <Icon color="red" cursor="pointer" className="bounce-icon" onClick={upgradeFirmware}>
                                <CiCircleRemove size="140px" />
                            </Icon>
                        )}
                        {/* 更新成功 */}
                        {updateStatus === UpdateStatus.UpdateSuccess && (
                            <Icon color="green.600" >
                                <CiCircleCheck size="140px" />
                            </Icon>
                        )}
                        {/* 更新中 */}
                        {updateStatus === UpdateStatus.Updating && (
                            <ProgressCircle.Root size="xl" colorPalette="green" value={_updateProgress} css={{ transform: "scale(1.9)" }}>
                                <ProgressCircle.Circle css={{ "--thickness": "2px" }} >
                                    <ProgressCircle.Track />
                                    <ProgressCircle.Range />
                                </ProgressCircle.Circle>
                                <AbsoluteCenter>
                                    <ProgressCircle.ValueText />
                                </AbsoluteCenter>
                            </ProgressCircle.Root>
                        )}

                    </Center>
                    <Text fontSize="2rem" color="green.600" textAlign="center">
                        {t.SETTINGS_FIRMWARE_TITLE}
                    </Text>
                    <Box w="full" >
                        <Text fontSize=".9rem" textAlign="center">
                            {t.SETTINGS_FIRMWARE_CURRENT_VERSION_LABEL}<Badge colorPalette="green" fontSize=".9rem" >v{currentVersion}</Badge><br />
                            {t.SETTINGS_FIRMWARE_LATEST_VERSION_LABEL}<Badge colorPalette="yellow" fontSize=".9rem" >v{latestVersion}</Badge><br />
                        </Text>
                    </Box>

                    {updateStatus === UpdateStatus.NoUpdate && (
                        <Center w="full" >
                            <Text fontSize=".9rem" color="green.600" textAlign="center">
                                {t.SETTINGS_FIRMWARE_NO_UPDATE_MESSAGE}
                            </Text>
                        </Center>
                    )}
                    {updateStatus !== UpdateStatus.NoUpdate && (
                        <Box w="full" >
                            <Alert.Root w="full" status={updateStatus === UpdateStatus.UpdateSuccess ? "success" : updateStatus === UpdateStatus.UpdateFailed ? "error" : "info"} >
                                <Alert.Indicator />
                                <Alert.Content>
                                    <Alert.Title fontSize=".9rem" lineHeight="1.4em"  >
                                        {updateStatus === UpdateStatus.Updating ? t.SETTINGS_FIRMWARE_UPDATING_MESSAGE
                                            : updateStatus === UpdateStatus.UpdateFailed ? t.SETTINGS_FIRMWARE_UPDATE_FAILED_MESSAGE
                                                : updateStatus === UpdateStatus.UpdateSuccess ? t.SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE
                                                    : updateStatus === UpdateStatus.UpdateAvailable ? t.SETTINGS_FIRMWARE_UPDATE_TODO_MESSAGE : ""}
                                    </Alert.Title>
                                </Alert.Content>
                            </Alert.Root>
                            <Card.Root w="full" height="200px" mt="10px"  >
                                <Card.Body overflowY="auto" >
                                    <List.Root
                                        gap=".5rem"
                                        align="start"
                                        variant="marker"
                                        fontSize=".8rem"
                                        listStyle="disc"
                                        color="GrayText"
                                        lineHeight={"1.2em"}
                                    >
                                        {latestFirmwareUpdateLog.map((log: string, index: number) => (
                                            <List.Item as="li" key={index} >
                                                <Text >
                                                    {log}
                                                </Text>
                                            </List.Item>
                                        ))}
                                    </List.Root>
                                </Card.Body>
                            </Card.Root>
                        </Box>
                    )}

                    {(updateStatus === UpdateStatus.UpdateAvailable || updateStatus === UpdateStatus.UpdateFailed || updateStatus === UpdateStatus.Updating) && (
                        <Button width="300px" size="lg" mt="30px" onClick={upgradeFirmware}
                            variant="solid"
                            colorPalette="green"
                            disabled={updateStatus === UpdateStatus.Updating} >
                            {updateStatus === UpdateStatus.UpdateAvailable ? t.SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON
                                : updateStatus === UpdateStatus.UpdateFailed ? t.SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON_RETRY
                                    : updateStatus === UpdateStatus.Updating ? t.SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON_PROGRESS : ""}
                        </Button>
                    )}
                </VStack>
            </Center>
        </Center>
    );
}