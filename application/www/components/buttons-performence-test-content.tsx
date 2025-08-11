import { Card, Text, SimpleGrid } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";
import {type ButtonPerformanceData } from '@/hooks/use-button-performance-monitor';

import ButtonsPerformanceTestField from "./buttons-performance-test-field";

export function ButtonsPerformenceTestContent(
    props: {
        maxTravelDistance: number,
        allButtonsData: ButtonPerformanceData[],
    }
) {
    const { t } = useLanguage();
    const { allButtonsData, maxTravelDistance } = props;

    return (
        <Card.Root w="778px" h="100%" >
            <Card.Header>
                <Card.Title fontSize={"md"}  >
                    <Text fontSize={"32px"} fontWeight={"bold"} color={"green.600"} >{t.SETTINGS_BUTTONS_PERFORMANCE_TEST_TITLE}</Text>
                </Card.Title>
                <Card.Description fontSize={"sm"} pt={4} pb={4} whiteSpace="pre-wrap"  >
                    {t.SETTINGS_BUTTONS_PERFORMANCE_TEST_HELPER_TEXT}
                </Card.Description>
            </Card.Header>
            <Card.Body>
                <SimpleGrid columns={4} gap={8} >
                    {allButtonsData.map((buttonData, index) => (
                    <ButtonsPerformanceTestField 
                        key={index}
                        index={buttonData.buttonIndex} 
                        maxDistance={maxTravelDistance} 
                        currentDistance={buttonData.currentDistance} 
                        pressStartDistance={buttonData.pressStartDistance} 
                        pressTriggerDistance={buttonData.pressTriggerDistance} 
                        releaseStartDistance={buttonData.releaseStartDistance} 
                        releaseTriggerDistance={buttonData.releaseTriggerDistance} />
                    ))}
                </SimpleGrid>
            </Card.Body>
        </Card.Root>
    );
}