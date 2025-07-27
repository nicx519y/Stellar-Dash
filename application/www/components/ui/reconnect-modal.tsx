import {
  Box,
  Portal,
  Button,
  Center,
} from "@chakra-ui/react";
import * as React from "react";

import { BeatLoader } from "react-spinners"
import { PiPlugsConnectedFill } from "react-icons/pi";

interface ReconnectModalProps {
  buttonText: string;
  isOpen: boolean;
  onReconnect: () => void;
  isLoading?: boolean;
}

export function ReconnectModal({ buttonText, isOpen, onReconnect, isLoading = false }: ReconnectModalProps) {
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
        <Center
          position="absolute"
          top={0}
          left={0}
          right={0}
          bottom={0}
          bg="blackAlpha.100"
          backdropFilter="blur(4px)"
        >

          <Button
            colorPalette="green"
            size="lg"
            onClick={onReconnect}
            loading={isLoading}
            spinner={<BeatLoader size={8} color="white" />}
            w="300px"
            boxShadow="0 0 10px 10px rgba(0, 0, 0, 0.4)"
          >
            <PiPlugsConnectedFill />
            {buttonText}
          </Button>
        </Center>
      </Box>
    </Portal>
  );
} 