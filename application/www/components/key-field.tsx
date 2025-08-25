import { Text, Box, Tag } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";
import { Fragment, useState } from "react";

export interface KeyFieldProps {
    prefix?: string;
    joiner?: string;
    value: string[];
    changeValue?: (value: string[]) => void;
    isActive?: boolean;
    disabled?: boolean;
    onClick?: () => void;
    width?: number;
    showTags?: boolean;
    maxKeys?: number;
}

export const KeyField = ({
    prefix = "",
    joiner = "",
    value = [],
    changeValue = () => {},
    isActive = false,
    disabled = false,
    onClick,
    width = 170,
    showTags = true,
    maxKeys = 4
}: KeyFieldProps) => {
    const { t } = useLanguage();



    const KeyBox = ({ isActive, isDisabled, width }: { isActive: boolean, isDisabled: boolean, width: number }) => {
        return (
            <Box 
                width={`${width}px`}
                height="30px"
                padding="4px"
                border=".5px solid"
                borderColor={isActive && !isDisabled ? "green.500" : "gray.600"}
                borderRadius="4px"
                cursor={isDisabled ? "not-allowed" : "pointer"}
                boxShadow={isActive && !isDisabled ? "0 0 8px rgba(154, 205, 50, 0.8)" : "none"}
                {...(!isDisabled && { onClick })}
                position="relative"
            >
                {showTags && value && value.length > 0 && (
                    <div style={{ position: 'absolute', top: '2px', left: '2px', display: 'flex', gap: '2px' }}>
                        {value.slice(0, maxKeys).map((keyValue, index) => (
                            <Fragment key={index}>
                                <KeyTag 
                                    key={index}
                                    isActive={isActive}
                                    isDisabled={isDisabled}
                                    value={keyValue.toString()}
                                    index={index}
                                />
                                {index < value.length - 1 && joiner && (
                                    <Text fontSize="12px" color="gray.500">
                                        {joiner}
                                    </Text>
                                )}
                            </Fragment>
                        ))}
                        {value.length > maxKeys && (
                            <Text fontSize="8px" color="gray.500">
                                +{value.length - maxKeys}
                            </Text>
                        )}
                    </div>
                )}
            </Box>
        );
    };

    const KeyTag = ({ isActive, isDisabled, value: keyValue, index }: { 
        isActive: boolean, 
        isDisabled: boolean, 
        value: string,
        index: number 
    }) => {
        const [_, setIsHovered] = useState(false);

        const handleTagClick = (e: React.MouseEvent) => {
            if (!isDisabled && onClick) {
                if (isActive) {
                    handleTagRemove(e);
                }
                onClick();
            }
        };

        const handleTagRemove = (e: React.MouseEvent) => {
            e.stopPropagation();
            if (!isDisabled) {
                const newValue = value.filter((_, i) => i !== index);
                changeValue?.(newValue);
            }
        };

        return (
            <Tag.Root
                variant={isActive && !isDisabled ? "solid" : "surface"}
                colorPalette={isActive && !isDisabled ? "green" : "gray"}
                color={isDisabled ? "gray.500" : isActive ? "white" : "gray.300"}
                onClick={handleTagClick}
                onMouseEnter={() => setIsHovered(true)}
                onMouseLeave={() => setIsHovered(false)}
                _hover={{
                    bg: isActive && !isDisabled ? "red.500" : "gray.800"
                }}
                cursor={isDisabled ? "not-allowed" : "pointer"}
                size="sm"
                height="24px"
            >
                <Tag.Label fontSize="10px">
                    {`${ prefix ? prefix : t?.KEY_MAPPING_KEY_PREFIX }${keyValue}`}
                </Tag.Label>

            </Tag.Root>
        );
    };

    return (
        <KeyBox 
            isActive={isActive} 
            isDisabled={disabled} 
            width={width} 
        />
    );
};

KeyField.displayName = 'KeyField'; 