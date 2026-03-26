'use client';

import { Flex, Box, Text } from '@chakra-ui/react';
import React, { ReactNode, createContext, useContext } from 'react';

export type SettingMainContentLayoutSize = number | string;

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
    width?: SettingMainContentLayoutSize;
    /** 自定义样式 */
    className?: string;
    /** 是否显示边框 */
    showBorder?: boolean;
    /** 自定义高度 */
    height?: SettingMainContentLayoutSize;
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
    const numericWidth = typeof width === "number"
        ? width
        : (() => {
            const m = String(width).trim().match(/^(\d+(?:\.\d+)?)(px)?$/i);
            if (!m) return 0;
            const v = Number(m[1]);
            return Number.isFinite(v) ? v : 0;
        })();

    // 创建上下文值
    const contextValue: MainContentLayoutContextType = {
        width: numericWidth,
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
                borderLeftWidth="1px"
                borderColor="grey.500"
                pl={6}
            >
                <Flex direction="column" w="100%" h="100%" gap={6}>
                    {/* 头部区域 */}
                    {headerContent.length > 0 && (
                        <Box>
                            {headerContent}
                        </Box>
                    )}
                    
                    {/* 主体内容区域 */}
                    {bodyContent.length > 0 && (
                        <Box flex="1">
                            {bodyContent}
                        </Box>
                    )}
                    
                    {/* 底部区域 */}
                    {footerContent.length > 0 && (
                        <Box mt="auto">
                            {footerContent}
                        </Box>
                    )}
                </Flex>
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
                <Box mb={description ? 2 : 4}>
                    <Text 
                        fontSize="24px" 
                        fontWeight="normal" 
                        color="green.400" 
                        letterSpacing="0.06em" 
                        lineHeight="3rem"
                    >
                        {title}
                    </Text>
                </Box>
            )}
            
            {description && (
                <Text fontSize="sm" pt={2} pb={4} whiteSpace="pre-wrap" color="gray.400">
                    {description}
                </Text>
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
