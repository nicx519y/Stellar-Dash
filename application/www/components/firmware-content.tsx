import { useLanguage } from "@/contexts/language-context";
import { AbsoluteCenter, Badge, Box, Card, Center, Icon, List, ProgressCircle, Text, VStack } from "@chakra-ui/react";
import { useMemo, useState } from "react";
import { CiCircleCheck, CiCircleRemove, CiSaveUp1 } from "react-icons/ci";

enum UpdateStatus {
    NoUpdate = 0,
    HasUpdate = 1,
    Updating = 2,
    UpdateFailed = 3,
    UpdateSuccess = 4,
}

export function FirmwareContent() {
    const { t } = useLanguage();
    const [currentVersion, setCurrentVersion] = useState("v1.0.0");
    const [latestVersion, setLatestVersion] = useState("v1.0.1");
    const [updateProgress, setUpdateProgress] = useState(0);
    const [updateStatus, setUpdateStatus] = useState(UpdateStatus.UpdateSuccess);

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
                {updateStatus === UpdateStatus.HasUpdate && (
                    <Icon color="yellow" cursor="pointer" className="bounce-icon">
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
                    <ProgressCircle.Root size="xl" colorPalette="green" value={updateProgress} css={{transform:"scale(1.9)"}}>
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
                        {t.SETTINGS_FIRMWARE_CURRENT_VERSION_LABEL}<Badge colorPalette="green" fontSize=".9rem" >v1.0.0</Badge><br />
                        {t.SETTINGS_FIRMWARE_LATEST_VERSION_LABEL}<Badge colorPalette="yellow" fontSize=".9rem" >v1.0.1</Badge><br />
                    </Text>
                </Box>
                <Box pt=".5rem" pb="1rem" width="300px" height="90px" >
                    <Text fontSize=".9rem" textAlign="center">
                        { updateStatus === UpdateStatus.Updating ? t.SETTINGS_FIRMWARE_UPDATING_MESSAGE 
                        : updateStatus === UpdateStatus.UpdateFailed ? t.SETTINGS_FIRMWARE_UPDATE_FAILED_MESSAGE
                        : updateStatus === UpdateStatus.UpdateSuccess ? t.SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE
                        : updateStatus === UpdateStatus.HasUpdate ? t.SETTINGS_FIRMWARE_UPDATE_TODO_MESSAGE : "" }
                    </Text>
                </Box>
                <Card.Root width="400px" height="200px" display={updateStatus === UpdateStatus.NoUpdate ? "none" : "block"} >
                    <Card.Body overflowY="auto" pl="2rem" pr="2rem" >
                        <List.Root
                            gap=".5rem"
                            align="start"
                            variant="marker" 
                            fontSize=".8rem"
                            listStyle="decimal" 
                            color="GrayText"
                        >
                        <List.Item as="li"  >
                            <Text >
                                Fixed bug, LED light is not working when the device is connected to the power supply.
                            </Text>
                        </List.Item>
                        <List.Item>
                            <Text >
                                Performance improvement, the device is more stable.
                            </Text>
                        </List.Item>
                        <List.Item>
                            <Text >
                                LEDs Effect is more beautiful.
                            </Text>
                        </List.Item>
                        <List.Item>
                            <Text  >
                                Fixed bug, LED light is not working when the device is connected to the power supply.
                            </Text>
                        </List.Item>
                    </List.Root>
                </Card.Body>
                </Card.Root>
            </VStack>
        </Center>
    );
}