'use client';

import { Flex, Box, Card, Text } from '@chakra-ui/react';
import React, { ReactNode, createContext, useContext } from 'react';

// 布局上下文
interface MainContentLayoutContextType {
    width: number;
    scale: number;
}

const MainContentLayoutContext = createContext<MainContentLayoutContextType>({
    width: 0,
    scale: 1,
});

// 使用布局上下文的 Hook
export function useMainContentLayoutContext() {
    return useContext(MainContentLayoutContext);
}

interface SettingMainContentLayoutProps {
    /** 子组件 */
    children: ReactNode;
    /** 自定义容器宽度 */
    width?: number;
    /** 自定义样式 */
    className?: string;
    /** 是否显示边框 */
    showBorder?: boolean;
    /** 自定义高度 */
    height?: string;
}

/**
 * 设置页面主要内容布局组件
 * 
 * 使用方式：
 * <SettingMainContentLayout>
 *   <MainContentHeader>标题内容</MainContentHeader>
 *   <MainContentBody>主体内容</MainContentBody>
 *   <MainContentFooter>底部内容</MainContentFooter>
 * </SettingMainContentLayout>
 */
export function SettingMainContentLayout({
    children,
    className,
    width = 778,
    height = "100%"
}: SettingMainContentLayoutProps) {
    // 创建上下文值
    const contextValue: MainContentLayoutContextType = {
        width,
        scale: 1, // 主内容区域通常不需要缩放
    };

    // 遍历子组件并分类
    const headerContent: ReactNode[] = [];
    const bodyContent: ReactNode[] = [];
    const footerContent: ReactNode[] = [];

    React.Children.forEach(children, (child) => {
        if (React.isValidElement(child)) {
            switch (child.type) {
                case MainContentHeader:
                    headerContent.push(child);
                    break;
                case MainContentBody:
                    bodyContent.push(child);
                    break;
                case MainContentFooter:
                    footerContent.push(child);
                    break;
                default:
                    // 未知组件，忽略
                    break;
            }
        }
    });

    return (
        <MainContentLayoutContext.Provider value={contextValue}>
            <Flex 
                direction="column" 
                width={width}
                height={height}
                className={className}
            >
                <Card.Root w="100%" h="100%">
                    {/* 头部区域 */}
                    {headerContent.length > 0 && (
                        <Card.Header>
                            {headerContent}
                        </Card.Header>
                    )}
                    
                    {/* 主体内容区域 */}
                    {bodyContent.length > 0 && (
                        <Card.Body>
                            {bodyContent}
                        </Card.Body>
                    )}
                    
                    {/* 底部区域 */}
                    {footerContent.length > 0 && (
                        <Card.Footer>
                            {footerContent}
                        </Card.Footer>
                    )}
                </Card.Root>
            </Flex>
        </MainContentLayoutContext.Provider>
    );
}

// 主内容头部组件
interface MainContentHeaderProps {
    children?: ReactNode;
    /** 标题文本 */
    title?: string;
    /** 描述文本 */
    description?: string;
    /** 自定义样式 */
    className?: string;
}

export function MainContentHeader({ 
    children, 
    title, 
    description, 
    className 
}: MainContentHeaderProps) {
    return (
        <Box className={className}>
            {title && (
                <Card.Title fontSize="md">
                    <Text 
                        fontSize="32px" 
                        fontWeight="normal" 
                        color="green.600" 
                        letterSpacing="0.06em" 
                        lineHeight="3rem"
                    >
                        {title}
                    </Text>
                </Card.Title>
            )}
            
            {description && (
                <Card.Description fontSize="sm" pt={4} pb={4} whiteSpace="pre-wrap">
                    {description}
                </Card.Description>
            )}
            
            {children}
        </Box>
    );
}

// 主内容主体组件
interface MainContentBodyProps {
    children: ReactNode;
    /** 自定义样式 */
    className?: string;
    /** 内边距 */
    padding?: string;
}

export function MainContentBody({ 
    children, 
    className, 
    padding = "0" 
}: MainContentBodyProps) {
    return (
        <Box className={className} p={padding}>
            {children}
        </Box>
    );
}

// 主内容底部组件
interface MainContentFooterProps {
    children: ReactNode;
    /** 自定义样式 */
    className?: string;
    /** 对齐方式 */
    justifyContent?: 'flex-start' | 'flex-end' | 'center' | 'space-between';
}

export function MainContentFooter({ 
    children, 
    className, 
    justifyContent = "flex-start" 
}: MainContentFooterProps) {
    return (
        <Box className={className}>
            <Flex justifyContent={justifyContent}>
                {children}
            </Flex>
        </Box>
    );
}

// 使用示例组件
export const MainContentLayoutExample: React.FC = () => {
    return (
        <SettingMainContentLayout width={778}>
            <MainContentHeader 
                title="设置标题"
                description="这是设置的描述文本，可以包含多行内容。"
            />
            
            <MainContentBody>
                <Text>这是主体内容区域，可以放置各种设置组件。</Text>
            </MainContentBody>
            
            <MainContentFooter justifyContent="flex-end">
                <Text>底部内容</Text>
            </MainContentFooter>
        </SettingMainContentLayout>
    );
}; 