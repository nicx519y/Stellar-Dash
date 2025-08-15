"use client";

import { Box, HStack, Text, VStack, Tag } from "@chakra-ui/react"
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
            <VStack onClick={ () => !isDisabled && onClick() } gap="2px" >
                <Text fontSize={"10px"} 
                    color={isActive ? "green.500" : colorMode === "dark" ? "gray.400" : "gray.600"} 
                    fontWeight={"bold"} 
                    lineHeight={"1rem"}
                >{`[ ${label} ]`}</Text>
                <Box width={"170px"} 
                    height={"30px"} 
                    padding={"4px"} 
                    border={".5px solid"} 
                    borderColor={isActive && !isDisabled ? "green.500" : colorMode === "dark" ? "gray.600" : "gray.400"} 
                    borderRadius={"4px"} 
                    cursor={isDisabled ? "not-allowed" : "pointer"} 
                    boxShadow={isActive && !isDisabled ? "0 0 8px rgba(154, 205, 50, 0.8)" : "none"}

                    
                >
                    <HStack gap="2px">
                        {value.map((hitboxButton, index) => (
                            <Tag.Root
                                key={index} 
                                // closable={isActive}
                                variant={isActive && !isDisabled ? "solid" : "surface"}
                                colorPalette={isActive && !isDisabled ? "green" : "gray"}
                                color={isDisabled ? "gray.500" : colorMode === "dark" || isActive == true ? "white" : "black"}
                                onClick={() => tagClick(hitboxButton)}
                                
                            >
                                <Tag.Label fontSize="10px" >{`${t.KEY_MAPPING_KEY_PREFIX}${hitboxButton + 1}`}</Tag.Label>
                            </Tag.Root>
                        ))}
                    </HStack>
                </Box>
            </VStack>
        </>
    )
}
