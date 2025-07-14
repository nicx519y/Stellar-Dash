# Hitbox 组件重构说明

## 概述

原始的 `hitbox.tsx` 组件承担了所有页面的功能需求，导致代码复杂且难以维护。现在已重构为基类 + 专门衍生类的架构，并且所有hitbox相关文件都统一放在 `@/components/hitbox/` 目录中。

## 组件结构

### 基类组件

- **HitboxBase** (`hitbox-base.tsx`)
  - 提供基础的SVG渲染和交互逻辑
  - 包含通用的样式组件（StyledSvg, StyledCircle, StyledFrame, StyledText）
  - 实现基本的点击处理和鼠标事件
  - 可被其他组件继承或直接使用

### 专门衍生类

- **HitboxKeys** (`hitbox-keys.tsx`)
  - 专用于按键设置页面 (`keys-setting-content.tsx`)
  - 提供基本的点击交互功能
  - 继承自 HitboxBase，无额外逻辑

- **HitboxEnableSetting** (`hitbox-enableSetting.tsx`)
  - 专用于按键启用设置模式 (`keys-setting-content.tsx`)
  - 根据按键启用状态显示不同颜色（启用绿色，禁用灰色）
  - 支持点击切换按键启用状态

- **HitboxHotkey** (`hitbox-hotkey.tsx`)
  - 专用于热键设置页面 (`global-setting-content.tsx` 非校准状态)
  - 提供热键设置的点击交互功能
  - 继承自 HitboxBase，无额外逻辑

- **HitboxCalibration** (`hitbox-calibration.tsx`)
  - 专用于全局设置页面 (`global-setting-content.tsx` 校准状态)
  - 支持校准状态的颜色显示 (`buttonsColorList` prop)
  - 实现自定义颜色动画渲染
  - 用于显示校准进度和状态

- **HitboxPerformance** (`hitbox-performance.tsx`)
  - 专用于按钮性能设置页面 (`buttons-performance-content.tsx`)
  - 提供按钮选择和高亮显示功能
  - 继承自 HitboxBase，支持 `highlightIds` 属性

- **HitboxLeds** (`hitbox-leds.tsx`)
  - 专用于LED设置页面 (`leds-setting-content.tsx`)
  - 支持完整的LED动画预览功能
  - 实现复杂的动画算法和颜色管理
  - 包含涟漪效果、呼吸效果等动画逻辑

### 辅助文件

- **hitbox-animation.ts** - LED动画算法实现
- **index.ts** - 统一导出接口
- **README.md** - 文档说明

## 使用方式

### 导入组件

```typescript
// 导入特定组件
import HitboxKeys from "@/components/hitbox/hitbox-keys";
import HitboxEnableSetting from "@/components/hitbox/hitbox-enableSetting";
import HitboxHotkey from "@/components/hitbox/hitbox-hotkey";
import HitboxCalibration from "@/components/hitbox/hitbox-calibration";
import HitboxPerformance from "@/components/hitbox/hitbox-performance";
import HitboxLeds from "@/components/hitbox/hitbox-leds";

// 或者从索引文件导入
import { HitboxKeys, HitboxEnableSetting, HitboxHotkey, HitboxCalibration, HitboxPerformance, HitboxLeds } from "@/components/hitbox";
```

### 页面使用示例

```typescript
// 按键设置页面
<HitboxKeys
    onClick={hitboxButtonClick}
    interactiveIds={KEYS_SETTINGS_INTERACTIVE_IDS}
/>

// 按键启用设置模式
<HitboxEnableSetting
    onClick={hitboxEnableSettingClick}
    interactiveIds={KEYS_SETTINGS_INTERACTIVE_IDS}
    buttonsEnableConfig={keysEnableConfig}
/>

// 热键设置页面
<HitboxHotkey
    interactiveIds={HOTKEYS_SETTINGS_INTERACTIVE_IDS}
    onClick={handleExternalClick}
/>

// 校准页面
<HitboxCalibration
    hasText={false}
    buttonsColorList={calibrationButtonColors}
/>

// 性能设置页面
<HitboxPerformance
    onClick={handleButtonClick}
    highlightIds={selectedButtons}
    interactiveIds={allKeys}
/>

// LED设置页面
<HitboxLeds
    hasText={false}
    ledsConfig={ledsConfiguration}
    interactiveIds={LEDS_SETTINGS_INTERACTIVE_IDS}
/>
```

## Props 接口

### HitboxBaseProps（基础属性）
```typescript
interface HitboxBaseProps {
    onClick?: (id: number) => void;
    hasText?: boolean;
    interactiveIds?: number[];
    highlightIds?: number[];
    className?: string;
}
```

### 专门组件的额外属性

- **HitboxCalibration**: `buttonsColorList?: GamePadColor[]`
- **HitboxLeds**: `ledsConfig?: LedsEffectStyleConfig`
- **HitboxEnableSetting**: `buttonsEnableConfig?: boolean[]`

## 目录结构

```
@/components/hitbox/
├── hitbox-base.tsx          # 基类组件
├── hitbox-keys.tsx          # 按键设置专用组件
├── hitbox-enableSetting.tsx # 按键启用设置专用组件
├── hitbox-hotkey.tsx        # 热键设置专用组件
├── hitbox-calibration.tsx   # 校准专用组件
├── hitbox-performance.tsx   # 性能设置专用组件
├── hitbox-leds.tsx          # LED设置专用组件
├── hitbox-animation.ts      # LED动画算法
├── index.ts                 # 统一导出
└── README.md               # 说明文档
```

## 重构优势

1. **代码分离**: 每个页面的特定逻辑被分离到专门的组件中
2. **易于维护**: 修改特定页面的功能不会影响其他页面
3. **性能优化**: 各页面只加载需要的功能代码
4. **类型安全**: 每个组件有明确的Props接口
5. **向后兼容**: 保持了原有的API接口
6. **目录整洁**: 所有相关文件统一管理在hitbox目录中

## 迁移指南

如果你正在使用旧的 `Hitbox` 组件：

1. 确定你的使用场景属于哪个页面类型
2. 替换为对应的专门组件
3. 更新导入路径到 `@/components/hitbox/` 目录
4. 检查Props是否需要调整
5. 测试功能是否正常

旧的 `Hitbox` 组件仍然可用（作为 `HitboxBase` 的别名），但建议迁移到专门组件以获得更好的性能和维护性。 