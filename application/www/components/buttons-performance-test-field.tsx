import { useLanguage } from "@/contexts/language-context";
import { Flex, Box, VStack,  Text } from "@chakra-ui/react";
import DistanceBar from "./distance-bar";
import { useEffect } from "react";

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

    const { t } = useLanguage();
    const { index, maxDistance, currentDistance, pressStartDistance, pressTriggerDistance, releaseStartDistance, releaseTriggerDistance } = props;

    const labelWidth = 50;

    useEffect(() => {
        if(index == 0) {
            console.log('pressStartDistance:', pressStartDistance, 'pressTriggerDistance:', pressTriggerDistance, 'releaseStartDistance:', releaseStartDistance, 'releaseTriggerDistance:', releaseTriggerDistance);
        }
    }, [pressStartDistance, pressTriggerDistance, releaseStartDistance, releaseTriggerDistance]);

    return (
        <Flex w="100%" h="100%" justifyContent="start"  gap={2} flexDirection={"row"} >
            <Box flex={0} width="20px"  >
                <DistanceBar 
                    width={20} height={70} 
                    maxDistance={maxDistance} 
                    currentDistance={currentDistance} 
                    pressStartDistance={pressStartDistance} 
                    pressTriggerDistance={pressTriggerDistance} 
                    releaseStartDistance={releaseStartDistance} 
                    releaseTriggerDistance={releaseTriggerDistance} />
            </Box>
            <VStack gap={2} flex={1} alignItems="start" justifyContent={"flex-end"} >
                <Flex gap={2} >
                    <Flex w={labelWidth} minW={labelWidth} flex={0} >
                        <Text fontSize="xs" >{t.TEST_MODE_FIELD_KEY_LABEL}</Text>
                    </Flex>
                    <Flex flex={1} >
                        <Text fontSize="xs" >{index + 1}</Text>
                    </Flex>
                </Flex>
                <Flex gap={2} >
                    <Flex w={labelWidth} minW={labelWidth} flex={0} >
                        <Text fontSize="xs" >{t.TEST_MODE_FIELD_PRESS_TRIGGER_POINT_LABEL}</Text>
                    </Flex>
                    <Flex flex={1} >
                        <Text fontSize="xs" >{Math.abs(pressTriggerDistance - pressStartDistance).toFixed(2)} mm</Text>
                    </Flex>
                </Flex>
                <Flex gap={2} >
                    <Flex w={labelWidth} minW={labelWidth} flex={0} >
                        <Text fontSize="xs" >{t.TEST_MODE_FIELD_RELEASE_START_POINT_LABEL}</Text>
                    </Flex>
                    <Flex flex={1} >
                        <Text fontSize="xs" >{Math.abs(releaseTriggerDistance - releaseStartDistance).toFixed(2)} mm</Text>
                    </Flex>
                </Flex>
            </VStack>
        </Flex>
    );
}