import { useLanguage } from "@/contexts/language-context";
import { AbsoluteCenter, Badge, Box, Card, Center, Icon, List, ProgressCircle, Text, VStack } from "@chakra-ui/react";
import { useEffect, useMemo, useState } from "react";
import { CiCircleCheck, CiCircleRemove, CiSaveUp1 } from "react-icons/ci";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { FirmwarePackage } from "@/types/types";

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

export function FirmwareContent() {
    const { t } = useLanguage();
    const [ isLoading, setIsLoading ] = useState(true);
    const [ _updateProgress, _setUpdateProgress ] = useState(0);
    const [ updateStatus, setUpdateStatus ] = useState(UpdateStatus.Idle);
    const { firmwareInfo, fetchFirmwareMetadata, firmwareUpdateInfo, checkFirmwareUpdate, downloadFirmwarePackage, uploadFirmwareToDevice } = useGamepadConfig();
    const currentVersion = useMemo(() => firmwareInfo?.firmware?.version || "0.0.0", [firmwareInfo]);
    const latestVersion = useMemo(() => firmwareUpdateInfo?.latestVersion? firmwareUpdateInfo.latestVersion : firmwareInfo?.firmware?.version || "0.0.0", [firmwareUpdateInfo, firmwareInfo]);
    const latestFirmwareUpdateLog = useMemo(() => firmwareUpdateInfo?.latestFirmware?.desc.split(/\s+/) || [], [firmwareUpdateInfo]);

    useEffect(() => {
        fetchFirmwareMetadata();
    }, []);

    const firmwrareCurrentVersion = useMemo(() => {
        return firmwareInfo?.firmware?.version || "";
    }, [firmwareInfo?.firmware.version]);

    useEffect(() => {
        if(firmwrareCurrentVersion != "") {
           checkFirmwareUpdate(firmwrareCurrentVersion);
        } else {
            console.log('firmwareInfo is null');
        }
    }, [firmwrareCurrentVersion]);

    useEffect(() => {
        if(firmwareUpdateInfo) {
            setIsLoading(false);

            // 如果正在更新，则根据固件更新信息判断更新状态
            if(updateStatus === UpdateStatus.Updating) {
                if(firmwareUpdateInfo.updateAvailable) {
                    setUpdateStatus(UpdateStatus.UpdateFailed);
                } else {
                    setUpdateStatus(UpdateStatus.UpdateSuccess);
                }
            }

            // 如果处于空闲状态，则根据固件更新信息判断更新状态
            if(updateStatus === UpdateStatus.Idle) {
                if(firmwareUpdateInfo.updateAvailable) {
                    setUpdateStatus(UpdateStatus.UpdateAvailable);
                } else {
                    setUpdateStatus(UpdateStatus.NoUpdate);
                }
            }
        }
    }, [firmwareUpdateInfo]);


    const upgradeFirmware = async () => {
        if (firmwareUpdateInfo && firmwareUpdateInfo.updateAvailable) {
            // 获取当前固件的槽位，如果当前固件是A槽位，则下载B槽位的固件，反之亦然
            const slot = firmwareInfo?.firmware?.currentSlot == 'A' ? 'slotB' : 'slotA';
            console.log('Begin to downloadFirmware url: ', firmwareUpdateInfo?.latestFirmware?.[slot]?.downloadUrl);
            setUpdateStatus(UpdateStatus.Updating);

            // 下载并解压固件包
            const firmwarePackage: FirmwarePackage = await downloadFirmwarePackage(firmwareUpdateInfo?.latestFirmware?.[slot]?.downloadUrl, (progress) => {
                console.log('progress: ', progress);
                if(progress.stage === 'downloading') {
                    _setUpdateProgress(progress.progress * ProgressPercent.Downloading);
                } else if(progress.stage === 'extracting') {
                    _setUpdateProgress(progress.progress * ProgressPercent.Downloading);
                } else if(progress.stage === 'downcompleted') {
                    _setUpdateProgress(progress.progress * ProgressPercent.Downloading);
                } else if(progress.stage === 'failed') {
                    setUpdateStatus(UpdateStatus.UpdateFailed);
                }
            });

            const nowProgress = ProgressPercent.Downloading * 100;

            // 上传固件包到设备
            await uploadFirmwareToDevice(firmwarePackage, (progress) => {
                console.log('progress: ', progress);
                if(progress.stage === 'uploading') {
                    _setUpdateProgress(nowProgress + progress.progress * ProgressPercent.Uploading);
                } else if(progress.stage === 'uploadcompleted') { // 上传完成，等待设备重启，重启完成后，设置固件更新状态为成功
                    _setUpdateProgress(nowProgress + progress.progress * ProgressPercent.Uploading);
                    setTimeout(() => { // 等待2秒后，检查固件更新状态, 此时固件已经上传到设备，设备正在重启
                        checkUpdateStatusLoop();
                    }, 2000);
                } else if(progress.stage === 'failed') {
                    setUpdateStatus(UpdateStatus.UpdateFailed);
                }
            });

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

            <VStack visibility={isLoading ? "hidden" : "visible"} >
                <Center width="140px" height="140px"  >
                {updateStatus === UpdateStatus.NoUpdate && (
                    <Icon color="green.600" >
                        <CiCircleCheck size="140px"  />
                    </Icon>
                )}
                {updateStatus === UpdateStatus.UpdateAvailable && (
                    <Icon color="yellow" cursor="pointer" className="bounce-icon" onClick={upgradeFirmware}>
                        <CiSaveUp1 size="150px"  />
                    </Icon>
                )}
                {updateStatus === UpdateStatus.UpdateFailed && (
                    <Icon color="red" cursor="pointer" className="bounce-icon" onClick={upgradeFirmware}>
                        <CiCircleRemove size="140px"  />
                    </Icon>
                )}
                {updateStatus === UpdateStatus.UpdateSuccess && (
                    <Icon color="green.600" >
                        <CiCircleCheck size="140px"  />
                    </Icon>
                )}
                {updateStatus === UpdateStatus.Updating && (
                    <ProgressCircle.Root size="xl" colorPalette="green" value={_updateProgress} css={{transform:"scale(1.9)"}}>
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
                <Box pt=".5rem" width="300px" >
                    <Text fontSize=".9rem" textAlign="center">
                        {t.SETTINGS_FIRMWARE_CURRENT_VERSION_LABEL}<Badge colorPalette="green" fontSize=".9rem" >v{currentVersion}</Badge><br />
                        {t.SETTINGS_FIRMWARE_LATEST_VERSION_LABEL}<Badge colorPalette="yellow" fontSize=".9rem" >v{latestVersion}</Badge><br />
                    </Text>
                </Box>
                <Box pt=".5rem" pb="1rem" width="300px" height="90px" >

                    <Text fontSize=".9rem" textAlign="center"
                        onClick={updateStatus === UpdateStatus.UpdateSuccess ? () => { window.location.reload(); } : undefined}
                        _hover={updateStatus === UpdateStatus.UpdateSuccess ? {
                            color: "blue.500",
                            textDecoration: "underline",
                            cursor: "pointer",
                        } : {}}
                    >
                        { updateStatus === UpdateStatus.NoUpdate ? t.SETTINGS_FIRMWARE_NO_UPDATE_MESSAGE
                        : updateStatus === UpdateStatus.Updating ? t.SETTINGS_FIRMWARE_UPDATING_MESSAGE 
                        : updateStatus === UpdateStatus.UpdateFailed ? t.SETTINGS_FIRMWARE_UPDATE_FAILED_MESSAGE
                        : updateStatus === UpdateStatus.UpdateSuccess? t.SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE
                        : updateStatus === UpdateStatus.UpdateAvailable ? t.SETTINGS_FIRMWARE_UPDATE_TODO_MESSAGE : "" }
                    </Text>
                </Box>
                <Card.Root width="400px" height="200px" display={updateStatus === UpdateStatus.NoUpdate ? "none" : "block"} >
                    <Card.Body overflowY="auto" pl="2rem" pr="2rem" pt="1rem" pb="1rem" >
                        <List.Root
                            gap=".5rem"
                            align="start"
                            variant="marker" 
                            fontSize=".8rem"
                            listStyle="disc" 
                            color="GrayText"
                        >
                            {latestFirmwareUpdateLog.map((log: string, index: number) => (
                                <List.Item as="li"  key={index} >
                                    <Text >
                                        {log}
                                    </Text>
                                </List.Item>
                            ))}
                        </List.Root>
                    </Card.Body>
                </Card.Root>
            </VStack>
        </Center>
    );
}