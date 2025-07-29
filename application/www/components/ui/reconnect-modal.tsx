'use client'

import {
  Portal,
  Button,
  Dialog,
  Box,
  Text,
} from "@chakra-ui/react";
import * as React from "react";

import { BeatLoader } from "react-spinners"
import { PiPlugsConnectedFill } from "react-icons/pi";

interface ReconnectModalProps {
  title: string;
  message: string;
  buttonText: string;
  isOpen: boolean;
  onReconnect: () => void;
  isLoading?: boolean;
}

export function ReconnectModal({ title, message, buttonText, isOpen, onReconnect, isLoading = false }: ReconnectModalProps) {
  if (!isOpen) return null;


  return (
    <>
      <Box
        position="fixed"
        top={0}
        left={0}
        right={0}
        bottom={0}
        zIndex={1}
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
      </Box>
      <Portal>
        <Dialog.Root open={isOpen} >
          <Dialog.Positioner>
            <Dialog.Content>
              <Dialog.Header>
                <Dialog.Title>{title}</Dialog.Title>
              </Dialog.Header>
              <Dialog.Body>
                <Text whiteSpace="pre-wrap" lineHeight="1.5">
                  {message}
                </Text>
              </Dialog.Body>
              <Dialog.Footer>
                <Button
                  w="200px"
                  colorPalette="blue"
                  onClick={onReconnect}
                  loading={isLoading}
                  spinner={<BeatLoader size={8} color="white" />}
                >
                  <PiPlugsConnectedFill />
                  {buttonText}
                </Button>
              </Dialog.Footer>
            </Dialog.Content>
          </Dialog.Positioner>
        </Dialog.Root>

      </Portal>
    </>
  );
} 