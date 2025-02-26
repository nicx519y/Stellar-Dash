import { 
  Box,
  Portal,
  Spinner,
  Center
} from "@chakra-ui/react";
import * as React from "react";

interface LoadingModalProps {
  isOpen: boolean;
}

export function LoadingModal({ isOpen }: LoadingModalProps) {
  if (!isOpen) return null;

  return (
    <Portal>
      <Box
        position="fixed"
        top={0}
        left={0}
        right={0}
        bottom={0}
        zIndex={9999}
        display="flex"
        alignItems="center"
        justifyContent="center"
      >
        <Box
          position="absolute"
          top={0}
          left={0}
          right={0}
          bottom={0}
          bg="blackAlpha.100"
          backdropFilter="blur(4px)"
        />
        <Box
          position="relative"
          zIndex={1}
          bg="transparent"
        >
          <Center p={8}>
            <Spinner
              color="green.500"
              size="xl"
            />
          </Center>
        </Box>
      </Box>
    </Portal>
  );
} 