import { Flex, Box, VStack,  Text } from "@chakra-ui/react";
import DistanceBar from "./distance-bar";
import { useEffect } from "react";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";

export default function ButtonsPerformanceTestField(
    props: {
        index: number,
        maxDistance: number,
        currentDistance: number,
        pressStartDistance: number,
        pressTriggerDistance: number,
        releaseStartDistance: number,
        releaseTriggerDistance: number,
    }
) {

    const { index, maxDistance, currentDistance, pressStartDistance, pressTriggerDistance, releaseStartDistance, releaseTriggerDistance } = props;
    const { defaultProfile } = useGamepadConfig();

    const { topDeadzone, bottomDeadzone } = defaultProfile.triggerConfigs?.triggerConfigs?.[index] || { topDeadzone: 0, bottomDeadzone: 0 };

    const labelWidth = 50;

    useEffect(() => {
        if(index == 0) {
            console.log(
                'pressStartDistance:', pressStartDistance, 
                'pressTriggerDistance:', pressTriggerDistance, 
                'releaseStartDistance:', releaseStartDistance, 
                'releaseTriggerDistance:', releaseTriggerDistance,
                'topDeadzone:', topDeadzone,
                'bottomDeadzone:', bottomDeadzone
            );
        }


    }, [pressStartDistance, pressTriggerDistance, releaseStartDistance, releaseTriggerDistance]);

    const getPressTravel = () => {
        return Math.abs(pressTriggerDistance - pressStartDistance);
    }
    const getReleaseTravel = () => {
        return Math.abs(releaseTriggerDistance - releaseStartDistance);
    }

    // 计算 press travel 在 topDeadzone 之外的长度
    const getPressOutDeadzone = () => {
        const td = maxDistance - topDeadzone;
        if(pressStartDistance <= td) {
            return pressStartDistance - pressTriggerDistance;
        } else {
            return Math.max(0, td - pressTriggerDistance);
        }
    }

    // 计算 release travel 在 bottomDeadzone 之外的长度
    const getReleaseOutDeadzone = () => {
        if(releaseStartDistance >= bottomDeadzone) {
            return releaseTriggerDistance - releaseStartDistance;
        } else {
            return Math.max(0, releaseTriggerDistance - bottomDeadzone);
        }
    }

    return (
        <Flex w="100%" h="100%" justifyContent="start"  gap={2} flexDirection={"row"} >
            <Box flex={0} width="20px"  >
                <DistanceBar 
                    width={20} height={100} 
                    maxDistance={maxDistance} 
                    currentDistance={currentDistance} 
                    pressStartDistance={pressStartDistance} 
                    pressTriggerDistance={pressTriggerDistance} 
                    releaseStartDistance={releaseStartDistance} 
                    releaseTriggerDistance={releaseTriggerDistance} />
            </Box>
            <VStack gap={2} flex={1} alignItems="start" justifyContent={"flex-end"} lineHeight={".85em"} >
                <Flex gap={2} >
                    <Flex w={labelWidth} minW={labelWidth} flex={0} >
                        <Text fontSize="xs" >key: </Text>
                    </Flex>
                    <Flex flex={1} >
                        <Text fontSize="xs" >{index + 1}</Text>
                    </Flex>
                </Flex>
                <Flex gap={2} >
                    <Flex w={labelWidth} minW={labelWidth} flex={0} >
                        <Text fontSize="xs" >Press: </Text>
                    </Flex>
                    <Flex flex={1} >
                        <Text fontSize="xs" >{getPressTravel().toFixed(2)} mm</Text>
                    </Flex>
                </Flex>
                <Flex gap={2} >
                    <Flex w={labelWidth} minW={labelWidth} flex={0} >
                        <Text fontSize="xs" >PoD:</Text>
                    </Flex>
                    <Flex flex={1} >
                        {/* 计算press travel 和 topDeadzone 的重合区域长度 */}
                        <Text fontSize="xs" >{getPressOutDeadzone().toFixed(2)} mm</Text>
                    </Flex>
                </Flex>
                <Flex gap={2} >
                    <Flex w={labelWidth} minW={labelWidth} flex={0} >
                        <Text fontSize="xs" >Release: </Text>
                    </Flex>
                    <Flex flex={1} >
                        <Text fontSize="xs" >{getReleaseTravel().toFixed(2)} mm</Text>
                    </Flex>
                </Flex>
                <Flex gap={2} >
                    <Flex w={labelWidth} minW={labelWidth} flex={0} >
                        <Text fontSize="xs" >RoD:</Text>
                    </Flex>
                    <Flex flex={1} >
                        {/* 计算release travel 和 bottomDeadzone 的重合区域长度 */}
                        <Text fontSize="xs" >{getReleaseOutDeadzone().toFixed(2)} mm</Text>
                    </Flex>
                </Flex>
            </VStack>
        </Flex>
    );
}