import { Text, TextProps } from '@chakra-ui/react';

export const IconUnicode = {
  clear: '\ue900',
  circle: '\ue901',
  square: '\ue902',
  triangle: '\ue903'
} as const;

type IconName = keyof typeof IconUnicode;

interface IconProps extends Omit<TextProps, 'children'> {
  name: IconName;
}

export const Icon = ({ name, ...props }: IconProps) => {
  return (
    <Text
      as="span"
      fontFamily="icomoon"
      {...props}
    >
      {IconUnicode[name]}
    </Text>
  );
}; 