'use client';
    
import { Hotkey, HotkeyAction, HotkeyActionList, UI_TEXT } from "@/types/gamepad-config";
import { 
    createListCollection, 
    Flex, 
    HStack, 
    Text, 
} from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";

import {
    SelectContent,
    SelectItem,
    SelectRoot,
    SelectTrigger,
    SelectValueText,
  } from "@/components/ui/select"

import { Tag } from "@/components/ui/tag"
import { useMemo } from "react";
import { useColorMode } from "./ui/color-mode";



export default function HotkeysField(
    props: {
        index: number,
        value: Hotkey,
        onValueChange: (value: Hotkey) => void,
        isActive: boolean,
        disabled?: boolean,
        onFieldClick?: (index: number) => void,
    }
) {
    const { t } = useLanguage();
    const { index, value, onValueChange, isActive, disabled, onFieldClick } = props;
    const { colorMode } = useColorMode();

    const getHotkeyActionLabel = (action: HotkeyAction): string => {
        switch (action) {
            case HotkeyAction.None:
                return t.HOTKEY_ACTION_NONE;
            case HotkeyAction.WebConfigMode:
                return t.HOTKEY_ACTION_WEB_CONFIG;
            case HotkeyAction.LedsEnableSwitch:
                return t.HOTKEY_ACTION_LEDS_ENABLE;
            case HotkeyAction.LedsEffectStyleNext:
                return t.HOTKEY_ACTION_LEDS_EFFECT_NEXT;
            case HotkeyAction.LedsEffectStylePrev:
                return t.HOTKEY_ACTION_LEDS_EFFECT_PREV;
            case HotkeyAction.LedsBrightnessUp:
                return t.HOTKEY_ACTION_LEDS_BRIGHTNESS_UP;
            case HotkeyAction.LedsBrightnessDown:
                return t.HOTKEY_ACTION_LEDS_BRIGHTNESS_DOWN;
            case HotkeyAction.CalibrationMode:
                return t.HOTKEY_ACTION_CALIBRATION_MODE;
            case HotkeyAction.XInputMode:
                return t.HOTKEY_ACTION_XINPUT_MODE;
            case HotkeyAction.PS4Mode:
                return t.HOTKEY_ACTION_PS4_MODE;
            case HotkeyAction.NSwitchMode:
                return t.HOTKEY_ACTION_NSWITCH_MODE;
            case HotkeyAction.SystemReboot:
                return t.HOTKEY_ACTION_SYSTEM_REBOOT;
            default:
                return action;
        }
    };

    /**
     * 创建热键选择列表
     */
    const hotkeyCollection = useMemo(() => {
        return createListCollection({
            items: HotkeyActionList.map(action => ({
                value: action,
                label: getHotkeyActionLabel(action)
            }))
        });
    }, [t]);

    /**
     * 点击关闭键 将键值设置为-1 表示没有绑定按键
     */
    const tagCloseClick = () => {
        onValueChange({ ...value, key: -1 });
    }

    return (
        <Flex padding={"2px"} width={"450px"} >
            <HStack 
                width="130px" 
                pl="2" 
                pr="2" 
                flex={1}  
                border={".5px solid"}
                borderColor={isActive ? "green.500" : colorMode === "dark" ? "gray.800" : "gray.400"} 
                borderRadius="sm" 
                mr="1" 
                cursor={disabled ? "not-allowed" : "pointer"}
                onClick={() => (!disabled) && onFieldClick?.(index)}
                opacity={!isActive ? (disabled ? 0.55 : 1) : 1}
                bg={!isActive ? "bg.muted" : ""} 
                boxShadow={isActive ? "0 0 8px rgba(154, 205, 50, 0.8)" : "none"}
            >
                <Tag colorPalette={isActive ? "green" : "gray"} >{`Fn`}</Tag>
                <Text>{` + `}</Text>
                {value.key !== undefined && value.key >= 0 &&
                    <Tag 
                        closable={isActive} 
                        colorPalette={isActive ? "green" : "gray"} 
                        onClick={ () => (!disabled) && isActive && tagCloseClick() }
                    >{`KEY-${value.key + 1}`}</Tag>
                }
            </HStack>
            <SelectRoot
                variant="outline"
                size="sm"
                collection={hotkeyCollection}
                value={[value.action ?? HotkeyAction.None]}
                onValueChange={e => onValueChange({ ...value, action: e.value[0] as HotkeyAction })}
                width="240px"
                disabled={disabled}
            >

                <SelectTrigger bg="bg.muted" opacity={0.75} >
                    < SelectValueText placeholder={UI_TEXT.SELECT_VALUE_TEXT_PLACEHOLDER} />
                </SelectTrigger>
                <SelectContent fontSize="xs"  >
                    {hotkeyCollection.items.map((item) => (
                        <SelectItem key={item.value} item={item}  >
                            {item.label}
                        </SelectItem>
                    ))}
                </SelectContent>
            </SelectRoot>
        </Flex>
    )
}

