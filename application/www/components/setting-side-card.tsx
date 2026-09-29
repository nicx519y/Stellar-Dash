'use client';

import { Card } from '@chakra-ui/react';
import type { ReactNode } from 'react';

export function SettingSideCard({ title, disabled = false, children, footer }: {
    title: string;
    disabled?: boolean;
    children: ReactNode;
    footer?: ReactNode;
}) {
    return (
        <Card.Root w="100%" minH="450px" size="md">
            <Card.Header>
                <Card.Title fontSize="md" color={disabled ? 'fg.muted' : 'fg'}>{title}</Card.Title>
            </Card.Header>
            <Card.Body>{children}</Card.Body>
            {footer && <Card.Footer>{footer}</Card.Footer>}
        </Card.Root>
    );
}
