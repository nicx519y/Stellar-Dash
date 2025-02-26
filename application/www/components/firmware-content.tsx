import { Box, Text } from "@chakra-ui/react";

import { Fieldset } from "@chakra-ui/react";

export function FirmwareContent() {
    return (
        <Box width="1700px" padding="18px">
            <Fieldset.Root>
                <Fieldset.Legend fontSize="2rem" color="green.600" textAlign="center">
                    FIRMWARE
                </Fieldset.Legend>
                <Fieldset.Content textAlign="center">
                    <Text color="gray.400">Firmware settings coming soon...</Text>
                </Fieldset.Content>
            </Fieldset.Root>
        </Box>
    );
}