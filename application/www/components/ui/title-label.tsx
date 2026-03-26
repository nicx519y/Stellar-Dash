import { HStack, Separator, Text } from "@chakra-ui/react";

export const TitleLabel = ({ title, mt }: { title: string, mt?: string }) => {
    return (
        <HStack w="full" margin="2px 0" marginTop={mt ?? "2px"} >
            <Separator flex="1" />
            <Text flexShrink="0" fontSize="sm" >{title}</Text>
            <Separator flex="1" />
        </HStack>
    )
}
