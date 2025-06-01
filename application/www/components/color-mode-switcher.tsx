'use client';

import { Icon, Switch } from "@chakra-ui/react";
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
        <Switch.Root
            colorPalette="green"
            size={"lg"}
            checked={myColorMode === 'dark' ? true : false}
            onCheckedChange={() => toggleColorMode()}
        >
            <Switch.HiddenInput />
            <Switch.Control>
                <Switch.Thumb />
                <Switch.Indicator fallback={<Icon as={FaMoon} color="gray.400" />}>
                    <Icon as={FaSun} color="yellow.400" />
                </Switch.Indicator>
            </Switch.Control>
        </Switch.Root>
    );
} 