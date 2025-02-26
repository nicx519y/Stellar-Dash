"use client";

import { Tag } from "@/components/ui/tag"
import { Box, HStack, Text, VStack } from "@chakra-ui/react"
import { useLanguage } from "@/contexts/language-context";
import { useColorMode } from "./ui/color-mode";

export default function KeymappingField(
    props: {
        value: number[],
        changeValue: (value: number[]) => void,
        label: string,
        isActive: boolean,
        onClick: () => void,
    }
) {
    const { colorMode } = useColorMode();
    const { t } = useLanguage();
    const { value, changeValue, label, isActive, onClick } = props;  

    const tagClick = (hitboxButton: number) => {
        if(isActive) {
            changeValue(value.filter(v => v !== hitboxButton));
        }
    }

    return (
        <>
            <VStack onClick={onClick} gap={0.5} mt={3} >
                <Text fontFamily={'icomoon'}  fontSize={"xs"} color={isActive ? "green.500" : colorMode === "dark" ? "gray.400" : "gray.600"} fontWeight={"bold"} >{`[ ${label} ]`}</Text>
                <Box width={"230px"} 
                    height={"32px"} 
                    padding={"5px"} 
                    border={".5px solid"} 
                    borderColor={isActive ? "green.500" : colorMode === "dark" ? "gray.700" : "gray.400"} 
                    borderRadius={"4px"} 
                    cursor={"pointer"} 
                    boxShadow={isActive ? "0 0 8px rgba(154, 205, 50, 0.8)" : "none"}
                    
                >
                    <HStack gap={1}>
                        {value.map((hitboxButton, index) => (
                            <Tag 
                                key={index} 
                                closable={isActive}
                                colorPalette={isActive ? "green" : "gray"}
                                onClick={() => tagClick(hitboxButton)}
                            >{`${t.KEY_MAPPING_KEY_PREFIX}${hitboxButton + 1}`}</Tag>
                        ))}
                    </HStack>
                </Box>
            </VStack>
        </>
    )
}
