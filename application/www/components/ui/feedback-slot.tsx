import { Box, type BoxProps } from '@chakra-ui/react';

/** Reserve space for asynchronous feedback without moving adjacent controls.
 * Long messages remain readable in a keyboard-accessible scroll region. */
export function FeedbackSlot({ children, ...props }: BoxProps) {
  return <Box data-feedback-slot="" h="40px" flexShrink={0} minW={0}
    fontSize="sm" lineHeight="20px" overflowWrap="anywhere" overflowY="auto"
    css={{ scrollbarGutter: 'stable' }} {...props}>{children}</Box>;
}
