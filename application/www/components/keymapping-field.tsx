"use client";

import { Text, VStack } from "@chakra-ui/react"
import { useColorMode } from "./ui/color-mode";
import { useMemo } from "react";
import { KeyField } from "./key-field";

export default function KeymappingField(
    props: {
        value: number[],
        changeValue: (value: number[]) => void,
        label: string,
        isActive: boolean,
        disabled?: boolean,
        onClick: () => void,
    }
) {
    const { colorMode } = useColorMode();
    const { value, changeValue, label, isActive, disabled, onClick } = props;  

    const isDisabled = useMemo(() => {
        return disabled ?? false;
    }, [disabled]);

    const valueString = useMemo(() => {
        return value.map(v => (v + 1).toString());
    }, [value]);

    return (
        <>
            <VStack onClick={ () => !isDisabled && onClick() } gap="2px" >
                <Text fontSize={"10px"} 
                    color={isActive ? "green.500" : colorMode === "dark" ? "gray.400" : "gray.600"} 
                    fontWeight={"bold"} 
                    lineHeight={"1rem"}
                >{`[ ${label} ]`}</Text>
                <KeyField
                    value={valueString}
                    changeValue={(v: string[]) => changeValue(v.map(Number).map(v => v - 1))}
                    isActive={isActive}
                    disabled={isDisabled}
                    onClick={onClick}
                    width={170}
                    showTags={true}
                    maxKeys={4}
                />
            </VStack>
        </>
    )
}
