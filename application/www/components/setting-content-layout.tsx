'use client';

import { Flex, Center, Box, Card, Button, HStack, Popover, Portal, Text, Icon } from '@chakra-ui/react';
import React, { useEffect, useRef, useState, ReactNode, createContext, useContext } from 'react';
import { ContentActionButtons } from './content-action-buttons';
import { IconType } from 'react-icons/lib';

// 按键选项接口
interface ButtonOption {
    icon?: IconType;
    /** 按钮文本 */
    text: string;
    /** 按钮颜色 */
    color?: 'green' | 'blue' | 'red' | 'gray';
    /** 按钮大小 */
    size?: 'xs' | 'sm' | 'md' | 'lg';
    /** 按钮变体 */
    variant?: 'solid' | 'outline' | 'ghost';
    /** 按钮宽度 */
    width?: string;
    /** 是否禁用 */
    disabled?: boolean;
    /** 点击回调 */
    onClick?: () => void;
    /** 鼠标进入回调 */
    onMouseEnter?: () => void;
    /** 鼠标离开回调 */
    onMouseLeave?: () => void;
    /** 是否有提示 */
    hasTip?: boolean;
    /** 提示消息 */
    tipMessage?: string;
}

// 上方按键配置接口
interface TopButtonsConfig {
    /** 是否显示上方按键区域 */
    show?: boolean;
    /** 按键选项数组 */
    buttons?: ButtonOption[];
    /** 自定义样式 */
    style?: React.CSSProperties;
    /** 容器宽度，默认348px */
    containerWidth?: string;
    /** 按钮间距 */
    gap?: number;
    /** 对齐方式 */
    justifyContent?: 'flex-start' | 'flex-end' | 'center' | 'space-between';
}

// 下方按键配置接口
interface BottomButtonsConfig {
    /** 是否显示下方按键区域 */
    show?: boolean;
    /** 按键选项数组 */
    buttons?: ButtonOption[];
    /** 自定义样式 */
    style?: React.CSSProperties;
    /** 容器宽度 */
    containerWidth?: string;
    /** 按钮间距 */
    gap?: number;
    /** 对齐方式 */
    justifyContent?: 'flex-start' | 'flex-end' | 'center' | 'space-between';
}

// 布局上下文
interface LayoutContextType {
    containerWidth: number;
    scale: number;
    dynamicOffset: number;
}

const LayoutContext = createContext<LayoutContextType>({
    containerWidth: 0,
    scale: 1,
    dynamicOffset: 280
});

// 使用布局上下文的 Hook
export function useLayoutContext() {
    return useContext(LayoutContext);
}

interface SettingContentLayoutProps {
    /** 子组件 */
    children: ReactNode;
    /** 是否禁用操作按钮 */
    disabled?: boolean;
    /** 自定义容器宽度 */
    containerWidth?: number;
    /** 是否显示操作按钮 */
    showActionButtons?: boolean;
    /** 自定义样式 */
    className?: string;
}

/**
 * 计算hitbox的缩放比例
 * @param containerWidth 容器宽度
 * @param hitboxWidth hitbox原始宽度，默认829
 * @param margin 边距，默认80
 * @param maxScale 最大缩放比例，默认1.3
 * @returns 缩放比例
 */
export function calculateHitboxScale(
    containerWidth: number,
    hitboxWidth: number = 829,
    margin: number = 80,
    maxScale: number = 1.3
): number {
    if (!containerWidth) return 1;
    
    const availableWidth = containerWidth - (margin * 2);
    
    if (availableWidth <= 0) return 0.1; // 最小缩放比例
    
    const scale = availableWidth / hitboxWidth;
    return Math.min(scale, maxScale); // 最大不超过maxScale，避免过度放大
}

/**
 * 计算动态位置偏移
 * @param scale 缩放比例
 * @param baseOffset 基础偏移量，默认280
 * @param margin 边距，默认80
 * @returns 动态偏移量
 */
export function calculateDynamicOffset(
    scale: number,
    baseOffset: number = 280,
    margin: number = 80
): number {
    return (baseOffset * scale) + margin;
}

/**
 * 渲染按键组 - 包含完整的布局逻辑
 */
function renderButtonGroup(
    buttons: ButtonOption[] = [], 
    config: TopButtonsConfig | BottomButtonsConfig = {}
) {
    const {
        // containerWidth,
        justifyContent = "flex-end",
        gap = 8,
        style
    } = config;

    return (
        <Box style={style}>
            <Flex 
                direction="row" 
                justifyContent="flex-end"
            >
                <HStack flex={1} justifyContent={justifyContent} gap={gap} >
                    {buttons.map((button, index) => {
                        const [isPopoverOpen, setIsPopoverOpen] = useState(false);
                        
                        const buttonElement = (
                            <Button
                                key={index}
                                size={button.size || "sm"}
                                variant={button.variant || "solid"}
                                colorPalette={button.color || "green"}
                                width={button.width || "170px"}
                                disabled={button.disabled}
                                onClick={button.onClick}
                                onMouseEnter={button.hasTip ? () => setIsPopoverOpen(true) : button.onMouseEnter}
                                onMouseLeave={button.hasTip ? () => setIsPopoverOpen(false) : button.onMouseLeave}
                            >
                                {button.icon && <Icon as={button.icon} />}
                                {button.text}
                            </Button>
                        );

                        // 如果按钮有提示，则包装在 Popover 中
                        if (button.hasTip && button.tipMessage) {
                            return (
                                <Popover.Root 
                                    key={index}
                                    open={isPopoverOpen}
                                    onOpenChange={(details) => setIsPopoverOpen(details.open)}
                                >
                                    <Popover.Trigger asChild>
                                        {buttonElement}
                                    </Popover.Trigger>
                                    <Portal>
                                        <Popover.Positioner>
                                            <Popover.Content>
                                                <Popover.Arrow />
                                                <Popover.Body>
                                                    <Text fontSize={"xs"} whiteSpace="pre-wrap">
                                                        {button.tipMessage}
                                                    </Text>
                                                </Popover.Body>
                                            </Popover.Content>
                                        </Popover.Positioner>
                                    </Portal>
                                </Popover.Root>
                            );
                        }

                        return buttonElement;
                    })}
                </HStack>
            </Flex>
        </Box>
    );
}

/**
 * 通用的设置内容布局组件 - 使用插槽方式
 * 
 * 使用方式：
 * <SettingContentLayout>
 *   <SideContent>左侧内容</SideContent>
 *   <HitboxContent>中间内容</HitboxContent>
 *   <MainContent>右侧内容</MainContent>
 *   <TopButtons config={topButtonsConfig} />
 *   <BottomButtons config={bottomButtonsConfig} />
 * </SettingContentLayout>
 */
export function SettingContentLayout({
    children,
    disabled = false,
    containerWidth: externalContainerWidth,
    showActionButtons = true,
    className
}: SettingContentLayoutProps) {
    // 添加容器引用和宽度状态
    const containerRef = useRef<HTMLDivElement>(null);
    const [internalContainerWidth, setInternalContainerWidth] = useState<number>(0);

    // 使用外部传入的宽度或内部计算的宽度
    const containerWidth = externalContainerWidth || internalContainerWidth;

    // 监听容器宽度变化
    useEffect(() => {
        if (externalContainerWidth) return; // 如果外部提供了宽度，不需要内部监听

        const updateWidth = () => {
            if (containerRef.current) {
                const width = containerRef.current.offsetWidth;
                console.log("SettingContentLayout containerWidth: ", width);
                setInternalContainerWidth(width);
            }
        };

        // 初始获取宽度
        updateWidth();

        // 监听窗口大小变化
        const resizeObserver = new ResizeObserver(updateWidth);
        if (containerRef.current) {
            resizeObserver.observe(containerRef.current);
        }

        // 清理
        return () => {
            resizeObserver.disconnect();
        };
    }, [externalContainerWidth]);

    // 计算缩放和偏移
    const scale = calculateHitboxScale(containerWidth);
    const dynamicOffset = calculateDynamicOffset(scale);

    // 创建上下文值
    const contextValue: LayoutContextType = {
        containerWidth,
        scale,
        dynamicOffset
    };

    // 遍历子组件并分类
    const sideContent: ReactNode[] = [];
    const hitboxContent: ReactNode[] = [];
    const mainContent: ReactNode[] = [];
    const topButtons: ReactNode[] = [];
    const bottomButtons: ReactNode[] = [];

    React.Children.forEach(children, (child) => {
        if (React.isValidElement(child)) {
            switch (child.type) {
                case SideContent:
                    sideContent.push(child);
                    break;
                case HitboxContent:
                    hitboxContent.push(child);
                    break;
                case MainContent:
                    mainContent.push(child);
                    break;
                case TopButtons:
                    topButtons.push(child);
                    break;
                case BottomButtons:
                    bottomButtons.push(child);
                    break;
                default:
                    // 未知组件，忽略
                    break;
            }
        }
    });

    return (
        <LayoutContext.Provider value={contextValue}>
            <Flex 
                direction="row" 
                width="100%" 
                height="100%" 
                padding="18px"
                className={className}
            >
                {/* 左侧边栏 */}
                <Flex flex={0} justifyContent={"flex-start"} height="fit-content" >
                    {sideContent}
                </Flex>

                {/* 中间hitbox区域 */}
                <Center 
                    ref={containerRef} 
                    flex={1} 
                    justifyContent="center" 
                    flexDirection="column"
                >
                    <Center padding="80px 30px" position="relative" flex={1}>
                        {/* 上方按键区域 */}
                        {topButtons}
                        
                        {/* hitbox内容 */}
                        {hitboxContent}
                        
                        {/* 下方按键区域 */}
                        {bottomButtons}
                    </Center>
                    
                    {/* 底部操作按钮 */}
                    {showActionButtons && (
                        <Center flex={0}>
                            <ContentActionButtons disabled={disabled} />
                        </Center>
                    )}
                </Center>

                {/* 右侧主内容区域 */}
                <Center>
                    {mainContent}
                </Center>
            </Flex>
        </LayoutContext.Provider>
    );
}

// 左侧边栏内容组件
interface SideContentProps {
    children: ReactNode;
}

export function SideContent({ children }: SideContentProps) {
    return <>{children}</>;
}

// 中间hitbox内容组件
interface HitboxContentProps {
    children: ReactNode | ((containerWidth: number) => ReactNode);
}

export function HitboxContent({ children }: HitboxContentProps) {
    const { containerWidth } = useLayoutContext();
    
    if (typeof children === 'function') {
        return <>{children(containerWidth)}</>;
    }
    return <>{children}</>;
}

// 右侧主内容组件
interface MainContentProps {
    children: ReactNode;
}

export function MainContent({ children }: MainContentProps) {
    return <>{children}</>;
}

// 上方按键组件
interface TopButtonsProps {
    config: TopButtonsConfig;
}

export function TopButtons({ config }: TopButtonsProps) {
    const { dynamicOffset } = useLayoutContext();
    
    if (!config.show || !config.buttons?.length) return null;
    
    return (
        <Box 
            position="absolute" 
            top="50%" 
            left="50%" 
            transform={`translateX(-50%) translateY(-${dynamicOffset}px)`} 
            zIndex={10}
        >
            <Card.Root w="100%" h="100%">
                <Card.Body p="10px">
                    {renderButtonGroup(config.buttons, config)}
                </Card.Body>
            </Card.Root>
        </Box>
    );
}

// 下方按键组件
interface BottomButtonsProps {
    config: BottomButtonsConfig;
}

export function BottomButtons({ config }: BottomButtonsProps) {
    if (!config.show || !config.buttons?.length) return null;
    
    return (
        <Box 
            position="absolute" 
            bottom="20px" 
            left="50%" 
            transform="translateX(-50%)" 
            zIndex={10}
        >
            <Card.Root w="100%" h="100%">
                <Card.Body p="10px">
                    {renderButtonGroup(config.buttons, config)}
                </Card.Body>
            </Card.Root>
        </Box>
    );
} 