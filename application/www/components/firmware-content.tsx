import { useLanguage } from "@/contexts/language-context";
import { AbsoluteCenter, Badge, Box, Card, Center, Icon, List, ProgressCircle, Text, VStack } from "@chakra-ui/react";
import { useEffect, useMemo, useState } from "react";
import { CiCircleCheck, CiCircleRemove, CiSaveUp1 } from "react-icons/ci";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";

enum UpdateStatus {
    NoUpdate = 0,
    UpdateAvailable = 1,
    Updating = 2,
    UpdateFailed = 3,
    UpdateSuccess = 4,
}

export function FirmwareContent() {
    const { t } = useLanguage();
    const [_updateProgress, _setUpdateProgress] = useState(0);
    const [updateStatus, setUpdateStatus] = useState(UpdateStatus.NoUpdate);
    const { firmwareInfo, fetchFirmwareMetadata, firmwareUpdateInfo, checkFirmwareUpdate, downloadFirmwarePackage } = useGamepadConfig();
    const currentVersion = useMemo(() => firmwareInfo?.firmware?.version || "0.0.0", [firmwareInfo]);
    const latestVersion = useMemo(() => firmwareUpdateInfo?.latestVersion? firmwareUpdateInfo.latestVersion : firmwareInfo?.firmware?.version || "0.0.0", [firmwareUpdateInfo, firmwareInfo]);
    const latestFirmwareUpdateLog = useMemo(() => firmwareUpdateInfo?.latestFirmware?.desc.split("\n") || [], [firmwareUpdateInfo]);

    useEffect(() => {
        fetchFirmwareMetadata();
    }, []);

    useEffect(() => {
        if (firmwareInfo && firmwareInfo.firmware && firmwareInfo.firmware.version != '') {
            checkFirmwareUpdate(firmwareInfo.firmware.version);
        } else {
            checkFirmwareUpdate("0.0.0");
        }
    }, [firmwareInfo]);

    useEffect(() => {
        if (firmwareUpdateInfo && firmwareUpdateInfo.updateAvailable) {
            setUpdateStatus(UpdateStatus.UpdateAvailable);
        } else if (firmwareUpdateInfo && !firmwareUpdateInfo.updateAvailable) {
            setUpdateStatus(UpdateStatus.NoUpdate);
        }
    }, [firmwareUpdateInfo]);

    const downloadFirmware = () => {
        if (firmwareUpdateInfo && firmwareUpdateInfo.updateAvailable) {

            const slot = firmwareInfo?.firmware?.slot == 'A' ? 'slotB' : 'slotA';

            console.log('Begin to downloadFirmware url: ', firmwareUpdateInfo?.latestFirmware?.[slot]?.downloadUrl);

            setUpdateStatus(UpdateStatus.Updating);

            downloadFirmwarePackage(firmwareUpdateInfo?.latestFirmware?.[slot]?.downloadUrl, (progress) => {
                console.log('progress: ', progress);
                _setUpdateProgress(progress.progress);
            });
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
            <VStack>
                <Center width="140px" height="140px"  >
                {updateStatus === UpdateStatus.NoUpdate && (
                    <Icon color="green.600" >
                        <CiCircleCheck size="140px"  />
                    </Icon>
                )}
                {updateStatus === UpdateStatus.UpdateAvailable && (
                    <Icon color="yellow" cursor="pointer" className="bounce-icon" onClick={downloadFirmware}>
                        <CiSaveUp1 size="150px"  />
                    </Icon>
                )}
                {updateStatus === UpdateStatus.UpdateFailed && (
                    <Icon color="red" >
                        <CiCircleRemove size="140px" className="bounce-icon"  />
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
                    <Text fontSize=".9rem" textAlign="center">
                        { updateStatus === UpdateStatus.NoUpdate ? t.SETTINGS_FIRMWARE_NO_UPDATE_MESSAGE
                        : updateStatus === UpdateStatus.Updating ? t.SETTINGS_FIRMWARE_UPDATING_MESSAGE 
                        : updateStatus === UpdateStatus.UpdateFailed ? t.SETTINGS_FIRMWARE_UPDATE_FAILED_MESSAGE
                        : updateStatus === UpdateStatus.UpdateSuccess ? t.SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE
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