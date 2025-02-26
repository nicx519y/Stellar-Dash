'use client';

import { Icon } from "@chakra-ui/react";
import { Switch } from "@/components/ui/switch";
import { FaMoon, FaSun } from "react-icons/fa"
import { useColorMode } from "./ui/color-mode";
import { useEffect, useState } from "react";

export function ColorModeSwitcher() {
    const { colorMode, toggleColorMode } = useColorMode();
    const [myColorMode, setMyColorMode] = useState('dark');

    useEffect(() => {
        setMyColorMode(colorMode ?? 'light');
    }, [colorMode]);

    return (
        <Switch
            colorPalette="green"
            size={"lg"}
            checked={myColorMode === 'dark' ? true : false}
            onChange={() => toggleColorMode()}
            trackLabel={{
                on: (
                    <Icon color="yellow.200">
                        <FaSun />
                    </Icon>
                ),
                off: (
                    <Icon color="gray.500">
                        <FaMoon />
                    </Icon>
                ),
            }}
        />
    );
} 