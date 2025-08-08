import { Box } from "@chakra-ui/react";
import { useMemo } from "react";

export default function DistanceBar(
    props: {
        width: number,
        height: number,
        maxDistance: number,
        currentDistance: number,
        pressStartDistance: number,
        pressTriggerDistance: number,
        releaseStartDistance: number,
        releaseTriggerDistance: number,
    }
) {
    const { 
        width,
        height,
        maxDistance, 
        currentDistance,
        pressStartDistance, 
        pressTriggerDistance, 
        releaseStartDistance, 
        releaseTriggerDistance 
    } = props;

    const baseTop = useMemo(() => {
        return height - (currentDistance / maxDistance) * height;
    }, [height, maxDistance, currentDistance]);

    const baseHeight = useMemo(() => {
        return height - baseTop;
    }, [height, baseTop]);

    const pressBoxTop = useMemo(() => {
        return height - (pressStartDistance / maxDistance) * height;
    }, [height, maxDistance, pressStartDistance]);

    const pressBoxHeight = useMemo(() => {
        return (Math.abs(pressTriggerDistance - pressStartDistance) / maxDistance) * height;
    }, [height, maxDistance, pressTriggerDistance, pressStartDistance]);

    const releaseBoxTop = useMemo(() => {
        return height - (releaseTriggerDistance / maxDistance) * height;
    }, [height, maxDistance, releaseTriggerDistance]);

    const releaseBoxHeight = useMemo(() => {
        return (Math.abs(releaseStartDistance - releaseTriggerDistance) / maxDistance) * height;
    }, [height, maxDistance, releaseStartDistance, releaseTriggerDistance]);

    return (
        <Box width={`${width}px`} height={`${height}px`} position="relative" >
            <Box width="100%" height={`${baseHeight}px`} position="absolute" top={`${baseTop}px`} left="0" bg="gray.400" zIndex={1} />
            <Box width="100%" height={`${pressBoxHeight}px`} position="absolute" top={`${pressBoxTop}px`} left="0" bg="blue" zIndex={2} opacity={0.7} />
            <Box width="100%" height={`${releaseBoxHeight}px`} position="absolute" top={`${releaseBoxTop}px`} left="0" bg="yellow" zIndex={3} opacity={0.7} />
        </Box>
    )
}