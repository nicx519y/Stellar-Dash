'use client';

import { create } from 'zustand';
import { Button } from "@/components/ui/button"
import { Field } from "@/components/ui/field"
import { Input, Dialog, Portal } from "@chakra-ui/react"
import { NumberInputField, NumberInputRoot } from "@/components/ui/number-input"
import { useState } from 'react';
import { useLanguage } from "@/contexts/language-context";

interface FormField {
    name: string;
    label: string;
    defaultValue?: string | number;
    placeholder?: string;
    type?: string;
    min?: number;
    max?: number;
    step?: number;
    validate?: (value: string) => string | undefined;
}

interface FormState {
    isOpen: boolean;
    title?: string;
    fields: FormField[];
    resolve?: (value: { [key: string]: string } | null) => void;
}

const useFormStore = create<FormState>(() => ({
    isOpen: false,
    fields: [],
}));

/**
 * 表单对话框
 * @returns 
 */
export function DialogForm() {
    const { isOpen, title, fields, resolve } = useFormStore();
    const [errors, setErrors] = useState<{ [key: string]: string }>({});
    const { t } = useLanguage();

    const handleClose = () => {
        setErrors({});
        useFormStore.setState({ isOpen: false });
        resolve?.(null);
    };

    const handleSubmit = (e: React.FormEvent<HTMLFormElement>) => {
        e.preventDefault();
        const formData = new FormData(e.currentTarget);
        const values: { [key: string]: string } = {};
        const newErrors: { [key: string]: string } = {};

        fields.forEach(field => {
            const value = formData.get(field.name)?.toString() || '';
            values[field.name] = value;

            if (field.validate) {
                const error = field.validate(value);
                if (error) {
                    newErrors[field.name] = error;
                }
            }
        });

        if (Object.keys(newErrors).length > 0) {
            setErrors(newErrors);
            return;
        }

        setErrors({});
        useFormStore.setState({ isOpen: false });
        resolve?.(values);
    };

    return (
        <Portal>
            <Dialog.Root open={isOpen} onOpenChange={handleClose}>
                <Dialog.Positioner>
                    <Dialog.Content>
                        <form onSubmit={handleSubmit}>
                            <Dialog.Header>
                                <Dialog.Title fontSize="sm" opacity={0.75}>{title}</Dialog.Title>
                            </Dialog.Header>
                            <Dialog.Body>
                                {fields.map((field, index) => (
                                    <Field
                                        key={index}
                                        label={field.label}
                                        errorText={errors[field.name] ?? ""}
                                        invalid={!!errors[field.name]}
                                        paddingBottom={index === fields.length - 1 ? "0" : "16px"}
                                    >
                                        {field.type === "number" ? (
                                            <NumberInputRoot
                                                name={field.name}
                                                bg="bg.muted"
                                                defaultValue={field.defaultValue?.toString() ?? undefined}
                                                min={field.min ?? undefined}
                                                max={field.max ?? undefined}
                                                step={field.step ?? undefined}
                                            >
                                                <NumberInputField
                                                    placeholder={field.placeholder}
                                                />
                                            </NumberInputRoot>
                                        ) : (
                                            <Input
                                                name={field.name}
                                                defaultValue={field.defaultValue?.toString() ?? undefined}
                                                placeholder={field.placeholder}
                                                type={field.type || "text"}
                                                autoComplete="off"
                                                bg="bg.muted"
                                            />
                                        )}
                                    </Field>
                                ))}
                            </Dialog.Body>
                            <Dialog.Footer>
                                <Button
                                    width="100px"
                                    size="sm"
                                    colorPalette="teal"
                                    variant="surface"
                                    onClick={handleClose}
                                >
                                    {t.BUTTON_CANCEL}
                                </Button>
                                <Button
                                    type="submit"
                                    width="100px"
                                    size="sm"
                                    colorPalette="green"
                                >
                                    {t.BUTTON_SUBMIT}
                                </Button>
                            </Dialog.Footer>
                        </form>
                    </Dialog.Content>
                </Dialog.Positioner>
            </Dialog.Root>
        </Portal>
    );
}

export function openForm(options: {
    title?: string;
    fields: FormField[];
}): Promise<{ [key: string]: string } | null> {
    return new Promise((resolve) => {
        useFormStore.setState({
            isOpen: true,
            title: options.title,
            fields: options.fields,
            resolve,
        });
    });
} 