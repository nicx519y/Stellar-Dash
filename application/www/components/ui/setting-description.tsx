import { Box, List, Text, VStack } from '@chakra-ui/react';

type DescriptionSegment =
    | { type: 'text'; value: string }
    | { type: 'list'; items: string[] };

function parseDescription(text: string): DescriptionSegment[] {
    const segments: DescriptionSegment[] = [];
    let listItems: string[] = [];

    const flushList = () => {
        if (listItems.length > 0) {
            segments.push({ type: 'list', items: listItems });
            listItems = [];
        }
    };

    for (const rawLine of text.split(/\r?\n/)) {
        const line = rawLine.trim();
        const listItem = line.match(/^-\s+(.+)$/);
        if (listItem) {
            listItems.push(listItem[1]);
            continue;
        }

        flushList();
        if (line.length > 0) {
            segments.push({ type: 'text', value: line });
        }
    }

    flushList();
    return segments;
}

interface SettingDescriptionProps {
    text: string;
    fontSize?: string;
    color?: string;
    pt?: number | string;
    pb?: number | string;
    mb?: number | string;
}

export function SettingDescription({
    text,
    fontSize = 'sm',
    color = 'gray.400',
    pt,
    pb,
    mb,
}: SettingDescriptionProps) {
    const segments = parseDescription(text);

    return (
        <Box fontSize={fontSize} color={color} pt={pt} pb={pb} mb={mb}>
            <VStack align="stretch" gap={2}>
                {segments.map((segment, index) => (
                    segment.type === 'text' ? (
                        <Text key={index} fontSize="inherit" color="inherit" lineHeight="1.5">
                            {segment.value}
                        </Text>
                    ) : (
                        <List.Root
                            key={index}
                            as="ul"
                            variant="marker"
                            listStyle="disc"
                            ps={5}
                            gap={1}
                            fontSize="inherit"
                            color="inherit"
                            lineHeight="1.5"
                        >
                            {segment.items.map((item, itemIndex) => (
                                <List.Item as="li" key={itemIndex}>
                                    {item}
                                </List.Item>
                            ))}
                        </List.Root>
                    )
                ))}
            </VStack>
        </Box>
    );
}
