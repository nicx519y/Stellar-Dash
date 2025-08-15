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
        topDeadzone: number,
        bottomDeadzone: number,
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
        releaseTriggerDistance,
        topDeadzone,
        bottomDeadzone
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

    const topDeadzoneHeight = useMemo(() => {
        return (topDeadzone / maxDistance) * height;
    }, [height, maxDistance, topDeadzone]);

    const bottomDeadzoneHeight = useMemo(() => {
        return (bottomDeadzone / maxDistance) * height;
    }, [height, maxDistance, bottomDeadzone]);

    return (
        <Box width={`${width}px`} height={`${height}px`} position="relative" >
            <Box width={`${width/3}px`} height={`${baseHeight}px`} position="absolute" left={`${0}px`} top={`${baseTop}px`} bg="gray.400" zIndex={1} />
            <Box width={`${width/3}px`} height={`${pressBoxHeight}px`} position="absolute" left={`${width/3}px`} top={`${pressBoxTop}px`} bg="blue" zIndex={2} opacity={1} />
            <Box width={`${width/3}px`} height={`${releaseBoxHeight}px`} position="absolute" left={`${width/3*2}px`} top={`${releaseBoxTop}px`} bg="yellow" zIndex={3} opacity={1} />
            <Box width={`${width/3}px`} height={`${topDeadzoneHeight}px`} position="absolute" left={`${0}px`} top={`${0}px`} bg="purple" zIndex={4} opacity={0.5} />
            <Box width={`${width/3}px`} height={`${bottomDeadzoneHeight}px`} position="absolute" left={`${0}px`} top={`${height - bottomDeadzoneHeight}px`} bg="purple" zIndex={5} opacity={0.5} />
        </Box>
    )
}