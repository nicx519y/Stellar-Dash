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

    // 获取热键操作的标签
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
            case HotkeyAction.AmbientLightEnableSwitch:
                return t.HOTKEY_ACTION_AMBIENT_LIGHT_ENABLE;
            case HotkeyAction.AmbientLightEffectStyleNext:
                return t.HOTKEY_ACTION_AMBIENT_LIGHT_EFFECT_NEXT;
            case HotkeyAction.AmbientLightEffectStylePrev:
                return t.HOTKEY_ACTION_AMBIENT_LIGHT_EFFECT_PREV;
            case HotkeyAction.AmbientLightBrightnessUp:
                return t.HOTKEY_ACTION_AMBIENT_LIGHT_BRIGHTNESS_UP;
            case HotkeyAction.AmbientLightBrightnessDown:
                return t.HOTKEY_ACTION_AMBIENT_LIGHT_BRIGHTNESS_DOWN;
            case HotkeyAction.CalibrationMode:
                return t.HOTKEY_ACTION_CALIBRATION_MODE;
            case HotkeyAction.XInputMode:
                return t.HOTKEY_ACTION_XINPUT_MODE;
            case HotkeyAction.PS4Mode:
                return t.HOTKEY_ACTION_PS4_MODE;
            case HotkeyAction.PS5Mode:
                return t.HOTKEY_ACTION_PS5_MODE;
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
    const hotkeyActionCollection = useMemo(() => {
        return createListCollection({
            items: HotkeyActionList.map(action => ({
                value: action,
                label: getHotkeyActionLabel(action)
            }))
        });
    }, [t]);

    const hotkeyTriggerCollection = useMemo(() => {
        return createListCollection({
            items: [{ value: "hold", label: t.HOTKEY_TRIGGER_HOLD }, { value: "click", label: t.HOTKEY_TRIGGER_CLICK }]
        });
    }, [t]);

    /**
     * 点击关闭键 将键值设置为-1 表示没有绑定按键
     */
    const tagCloseClick = () => {
        onValueChange({ ...value, key: -1 });
    }

    return (
        <Flex padding={"2px"} width={"730px"} gap={1} >
            <HStack 
                pl="2" 
                pr="2" 
                flex={1}  
                border={".5px solid"}
                borderColor={isActive ? "green.500" : colorMode === "dark" ? "gray.800" : "gray.400"} 
                borderRadius="sm" 
                cursor={disabled ? "not-allowed" : "pointer"}
                onClick={() => (!disabled) && onFieldClick?.(index)}
                opacity={!isActive ? (disabled ? 0.55 : 1) : 1}
                bg={!isActive ? "bg.muted" : ""} 
                boxShadow={isActive ? "0 0 8px rgba(154, 205, 50, 0.8)" : "none"}
            >
                <Tag colorPalette={isActive ? "green" : "gray"} >{`Fn`}</Tag>
                <Text>{` + `}</Text>
                {value.key !== undefined && value.key >= 0 && value.key !== 21 && // 21 是Fn键的虚拟引脚
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
                collection={hotkeyActionCollection}
                value={[value.action ?? HotkeyAction.None]}
                onValueChange={e => onValueChange({ ...value, action: e.value[0] as HotkeyAction })}
                width="320px"
                disabled={disabled}
            >

                <SelectTrigger bg="bg.muted" opacity={0.75} >
                    < SelectValueText placeholder={UI_TEXT.SELECT_VALUE_TEXT_PLACEHOLDER} />
                </SelectTrigger>
                <SelectContent fontSize="xs"  >
                    {hotkeyActionCollection.items.map((item) => (
                        <SelectItem key={item.value} item={item}  >
                            {item.label}
                        </SelectItem>
                    ))}
                </SelectContent>
            </SelectRoot>

            <SelectRoot
                variant="outline"
                size="sm"
                collection={hotkeyTriggerCollection}
                value={[value.isHold ? "hold" : "click"]}
                onValueChange={e => onValueChange({ ...value, isHold: e.value[0] === "hold" })}
                width="150px"
                disabled={disabled}
            >
                <SelectTrigger bg="bg.muted" opacity={0.75} >
                    < SelectValueText placeholder={UI_TEXT.SELECT_VALUE_TEXT_PLACEHOLDER} />
                </SelectTrigger>
                <SelectContent fontSize="xs"  >
                    <SelectItem item={{ value: "click", label: t.HOTKEY_TRIGGER_CLICK }} >{t.HOTKEY_TRIGGER_CLICK}</SelectItem>
                    <SelectItem item={{ value: "hold", label: t.HOTKEY_TRIGGER_HOLD }} >{t.HOTKEY_TRIGGER_HOLD}</SelectItem>
                </SelectContent>
            </SelectRoot>
        </Flex>
    )
}

