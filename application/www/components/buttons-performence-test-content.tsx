import { SimpleGrid } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";
import {type ButtonPerformanceData } from '@/hooks/use-button-performance-monitor';
import { 
    SettingMainContentLayout, 
    MainContentHeader, 
    MainContentBody 
} from "@/components/setting-main-content-layout";

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
        <SettingMainContentLayout width={778}>
            <MainContentHeader 
                title={t.SETTINGS_BUTTONS_PERFORMANCE_TEST_TITLE}
                description={t.SETTINGS_BUTTONS_PERFORMANCE_TEST_HELPER_TEXT}
            />
            <MainContentBody>
                <SimpleGrid columns={4} gap={8}>
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
            </MainContentBody>
        </SettingMainContentLayout>
    );
}