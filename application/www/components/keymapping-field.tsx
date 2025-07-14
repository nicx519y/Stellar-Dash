"use client";

import { Tag } from "@/components/ui/tag"
import { Box, HStack, Text, VStack } from "@chakra-ui/react"
import { useLanguage } from "@/contexts/language-context";
import { useColorMode } from "./ui/color-mode";
import { useMemo } from "react";

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
    const { t } = useLanguage();
    const { value, changeValue, label, isActive, disabled, onClick } = props;  

    const isDisabled = useMemo(() => {
        return disabled ?? false;
    }, [disabled]);

    const tagClick = (hitboxButton: number) => {
        if(isActive && !isDisabled) {
            changeValue(value.filter(v => v !== hitboxButton));
        }
    }

    return (
        <>
            <VStack onClick={ () => !isDisabled && onClick() } gap={0.5} mt={3} >
                <Text fontFamily={'icomoon'}  fontSize={"xs"} color={isActive ? "green.500" : colorMode === "dark" ? "gray.400" : "gray.600"} fontWeight={"bold"} >{`[ ${label} ]`}</Text>
                <Box width={"240px"} 
                    height={"32px"} 
                    padding={"5px"} 
                    border={".5px solid"} 
                    borderColor={isActive && !isDisabled ? "green.500" : colorMode === "dark" ? "gray.700" : "gray.400"} 
                    borderRadius={"4px"} 
                    cursor={isDisabled ? "not-allowed" : "pointer"} 
                    boxShadow={isActive && !isDisabled ? "0 0 8px rgba(154, 205, 50, 0.8)" : "none"}

                    
                >
                    <HStack gap={1}>
                        {value.map((hitboxButton, index) => (
                            <Tag 
                                key={index} 
                                closable={isActive}
                                colorPalette={isActive ? "green" : "gray"}
                                color={isDisabled ? "gray.500" : "white"}
                                onClick={() => tagClick(hitboxButton)}

                            >{`${t.KEY_MAPPING_KEY_PREFIX}${hitboxButton + 1}`}</Tag>
                        ))}
                    </HStack>
                </Box>
            </VStack>
        </>
    )
}
