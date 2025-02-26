'use client';

import {  Platform } from "@/types/gamepad-config";
import { 
    GameControllerButton, 
    GameControllerButtonList,
    XInputButtonMap,
    PS4ButtonMap,
    SwitchButtonMap,    
    NUM_BIND_KEY_PER_BUTTON_MAX,
} from "@/types/gamepad-config";
import KeymappingField from "@/components/keymapping-field";
import { toaster } from "@/components/ui/toaster";
import { useEffect, useMemo, useState } from "react";
import { SimpleGrid } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";

export default function KeymappingFieldset(
    props: {
        inputMode: Platform,
        inputKey: number,
        keyMapping: { [key in GameControllerButton]?: number[] },
        autoSwitch: boolean,
        changeKeyMappingHandler: (key: GameControllerButton, value: number[]) => void,
    }
) {

    const { inputMode, inputKey, keyMapping, autoSwitch, changeKeyMappingHandler } = props;
    const { t } = useLanguage();

    const [activeButton, setActiveButton] = useState<GameControllerButton>(GameControllerButtonList[0]);
    /**
     * change key mapping when input key is changed
     */
    useEffect(() => {
        
        // input key is valid
        if(inputKey >= 0) {
            
            const activeKeyMapping = keyMapping[activeButton] ?? [];
            if(activeKeyMapping.indexOf(inputKey) !== -1) { // key already binded
                toaster.create({
                    title: t.KEY_MAPPING_ERROR_ALREADY_BINDED_TITLE,
                    description: t.KEY_MAPPING_ERROR_ALREADY_BINDED_DESC,
                    type: "warning",
                });
                return;
            } else if(activeKeyMapping.length >= NUM_BIND_KEY_PER_BUTTON_MAX) { // key not binded, and reach max number of key binding per button
                toaster.create({
                    title: t.KEY_MAPPING_ERROR_MAX_KEYS_TITLE,
                    description: t.KEY_MAPPING_ERROR_MAX_KEYS_DESC,
                    type: "warning",
                });
                return;
            } else { // key not binded, and not reach max number of key binding per button

                // remove input key from other button
                Object.entries(keyMapping).forEach(([key, value]) => {
                    if(key !== activeButton && value.indexOf(inputKey) !== -1) {
                        value.splice(value.indexOf(inputKey), 1);
                        changeKeyMappingHandler(key as GameControllerButton, value);
                        toaster.create({
                            title: t.KEY_MAPPING_INFO_UNBIND_FROM_OTHER_TITLE,
                            description: t.KEY_MAPPING_INFO_UNBIND_FROM_OTHER_DESC
                                .replace("{0}", buttonLabelMap.get(key as GameControllerButton) ?? "")
                                .replace("{1}", buttonLabelMap.get(activeButton) ?? ""),
                            type: "warning",
                        });
                    }
                });

                // add input key to active button
                activeKeyMapping.push(inputKey);
                changeKeyMappingHandler(activeButton, activeKeyMapping);

                if(autoSwitch) {
                    const nextButton = GameControllerButtonList[GameControllerButtonList.indexOf(activeButton) + 1] ?? GameControllerButtonList[0];
                    setActiveButton(nextButton);
                }
                
            }
        }
    }, [inputKey]);

    /**
     * get button label map by input mode
     */
    const buttonLabelMap = useMemo(() => {
        switch(inputMode) {
            case Platform.XINPUT: return XInputButtonMap;
            case Platform.PS4: return PS4ButtonMap;
            case Platform.SWITCH: return SwitchButtonMap;
            default: return new Map<GameControllerButton, string>();
        }
    }, [inputMode]);

    return (
        <>
            <SimpleGrid gap={1} columns={3} >
                {GameControllerButtonList.map((gameControllerButton, index) => (
                    <KeymappingField
                        key={ index }  
                        onClick={() => setActiveButton(gameControllerButton)}
                        label={buttonLabelMap.get(gameControllerButton) ?? ""} 
                        value={keyMapping[gameControllerButton] ?? []}
                        changeValue={(v: number[]) => changeKeyMappingHandler(gameControllerButton, v)} 
                        isActive={activeButton === gameControllerButton}
                    />
                ))}
            </SimpleGrid>
        </>
    )
}