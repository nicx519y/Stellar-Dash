import { GamePadColor } from "@/types/gamepad-color";
import { LedsEffectStyle } from "@/types/gamepad-config";

export type LedAnimationParams = {
	index: number;
	progress: number;
	pressed: boolean;
	colorEnabled: boolean;
	frontColor: GamePadColor;
	backColor1: GamePadColor;
	backColor2: GamePadColor;
	defaultBackColor: GamePadColor;
	effectStyle: LedsEffectStyle;
	brightness: number;
	layout: { x: number, y: number, r: number }[];
	// 全局动画参数
	global?: {
		rippleActive?: boolean;
		rippleCenterIndex?: number | null;
		rippleProgress?: number;
		ripples?: Array<{ centerIndex: number, progress: number }>;
		[key: string]: unknown;
	};
	// 预留：按钮位置等
};

export type LedAnimationAlgorithm = (params: LedAnimationParams) => GamePadColor;

// 线性插值颜色
function lerpColor(
	out: GamePadColor,
	colorA: GamePadColor,
	colorB: GamePadColor,
	t: number
) {
	// 确保 t 在 0-1 范围内
	t = Math.max(0, Math.min(1, t));
	
	// 克隆输入颜色，避免修改原始颜色
	const a = colorA.clone();
	const b = colorB.clone();
	
	// 计算每个通道的插值
	out.setChannelValue('red', Math.round(a.getChannelValue('red') * (1 - t) + b.getChannelValue('red') * t));
	out.setChannelValue('green', Math.round(a.getChannelValue('green') * (1 - t) + b.getChannelValue('green') * t));
	out.setChannelValue('blue', Math.round(a.getChannelValue('blue') * (1 - t) + b.getChannelValue('blue') * t));
	out.setChannelValue('alpha', Math.round(a.getChannelValue('alpha') * (1 - t) + b.getChannelValue('alpha') * t));
}

export const staticAnimation: LedAnimationAlgorithm = ({
	pressed, colorEnabled, frontColor, backColor1, defaultBackColor, brightness
}) => {
	let color = colorEnabled
		? (pressed ? frontColor : backColor1)
		: defaultBackColor;
	color = color.clone();
	color.setChannelValue('alpha', brightness / 100 * color.getChannelValue('alpha'));
	return color;
};

export const breathingAnimation: LedAnimationAlgorithm = ({
	pressed, colorEnabled, frontColor, backColor1, backColor2, defaultBackColor, progress, brightness
}) => {
	let color: GamePadColor;
	if (colorEnabled) {
		if (pressed) {
			color = frontColor.clone();
		} else {
			const t = Math.sin(progress * Math.PI);
			color = backColor1.clone();
			lerpColor(color, backColor1, backColor2, t);
		}
	} else {
		color = defaultBackColor.clone();
	}
	color.setChannelValue('alpha', brightness / 100 * color.getChannelValue('alpha'));
	return color;
};

// 用于存储当前闪烁的按钮
let currentStarButtons1: number[] = [];
let currentStarButtons2: number[] = [];
let isFirstHalf = true; // 用于跟踪是否在周期的前半段

// 随机选择不重复的按钮
function selectRandomButtons(total: number, count: number, exclude: number[]): number[] {
	const available = Array.from({length: total}, (_, i) => i)
		.filter(i => !exclude.includes(i));
	
	const result: number[] = [];
	for (let i = 0; i < count && available.length > 0; i++) {
		const randomIndex = Math.floor(Math.random() * available.length);
		result.push(available[randomIndex]);
		available.splice(randomIndex, 1);
	}
	return result;
}

export const starAnimation: LedAnimationAlgorithm = ({	
	index,
	pressed, 
	colorEnabled, 
	frontColor, 
	backColor1, 
	backColor2, 
	defaultBackColor, 
	progress, 
	brightness,
	layout,
}) => {
	// 如果颜色未启用，返回默认背景色
	if (!colorEnabled) {
		return defaultBackColor.clone();
	}

	// 如果按钮被按下，返回前景色
	if (pressed) {
		const color = frontColor.clone();
		color.setChannelValue('alpha', brightness / 100 * color.getChannelValue('alpha'));
		return color;
	}

	// 加快动画速度4倍
	const fastProgress = (progress * 2) % 1;
	const btnLen = layout.length;

	// 在周期的中点（0.5）更新闪烁按钮
	const currentHalf = fastProgress < 0.5;
	if (currentHalf !== isFirstHalf) {
		if (currentHalf) { // 开始新的周期
			// 随机选择2-3个按钮，确保与上一组不同
			const numStars = Math.floor(Math.random() * 2) + 2; // 2-3个
			// 更新第一组按钮
			currentStarButtons1 = selectRandomButtons(btnLen, numStars, [...currentStarButtons1, ...currentStarButtons2]);
		} else { // 在周期中点更新第二组按钮
			const numStars = Math.floor(Math.random() * 2) + 2;
			currentStarButtons2 = selectRandomButtons(btnLen, numStars, [...currentStarButtons1, ...currentStarButtons2]);
		}
		isFirstHalf = currentHalf;
	}

	// 如果当前按钮不在任何闪烁列表中，返回backColor1
	if (!currentStarButtons1.includes(index) && !currentStarButtons2.includes(index)) {
		const color = backColor1.clone();
		color.setChannelValue('alpha', brightness / 100 * color.getChannelValue('alpha'));
		return color;
	}

	// 计算渐变进度
	let fadeInOut = 0;
	
	// 第一组按钮的渐变
	if (currentStarButtons1.includes(index)) {
		// 第一组使用0-0.5的周期
		const cycleProgress = fastProgress * 2;
		fadeInOut = Math.sin(cycleProgress * Math.PI / 2);
	}
	
	// 第二组按钮的渐变（错开半个周期）
	if (currentStarButtons2.includes(index)) {
		// 第二组使用0.5-1的周期，确保值在0-1范围内
		const cycleProgress = ((fastProgress + 0.5) % 1) * 2;
		fadeInOut = Math.sin(cycleProgress * Math.PI / 2);
	}

	// 在backColor1和backColor2之间进行渐变
	const result = new GamePadColor();
	lerpColor(result, backColor1.clone(), backColor2.clone(), fadeInOut);
	result.setChannelValue('alpha', brightness / 100 * result.getChannelValue('alpha'));
	
	return result;
};

export const flowingAnimation: LedAnimationAlgorithm = ({
	index,
	progress,
	backColor1,
	backColor2,
	brightness,
	colorEnabled,
	pressed,
	frontColor,
	defaultBackColor,
	layout,
}) => {
	// 颜色未启用，返回默认色
	if (!colorEnabled) {
		return defaultBackColor.clone();
	}
	// 按下时返回前景色
	if (pressed) {
		const color = frontColor.clone();
		color.setChannelValue('alpha', brightness / 100 * color.getChannelValue('alpha'));
		return color;
	}
	// 流光参数
	const minX = Math.min(...layout.map(btn => btn.x)) - 100; // 流光中心范围扩大，保证边缘按钮完整渐变
	const maxX = Math.max(...layout.map(btn => btn.x)) + 100; // 流光中心范围扩大，保证边缘按钮完整渐变
	const bandWidth = 140; // 光带宽度，可调整
	// 当前流光中心位置
	const centerX = minX + (maxX - minX) * progress * 1.6;
	const btnX = layout[index]?.x ?? minX;
	// 计算距离中心的归一化距离
	const dist = Math.abs(btnX - centerX);
	
	// 使用更平滑的渐变算法，避免闪烁
	let t = 0;
	if (dist <= bandWidth) {
		// 使用平滑的渐变函数，确保在边界处没有突变
		const normalizedDist = dist / bandWidth; // 0~1
		// 使用 smoothstep 函数创建更平滑的过渡
		t = 1 - (normalizedDist * normalizedDist * (3 - 2 * normalizedDist));
	}
	
	// 渐变色
	const result = new GamePadColor();
	lerpColor(result, backColor1, backColor2, t);
	result.setChannelValue('alpha', brightness / 100 * result.getChannelValue('alpha'));
	return result;
};

export const rippleAnimation: LedAnimationAlgorithm = (params) => {
	const {
		index, pressed, colorEnabled, frontColor, backColor1, backColor2, defaultBackColor, brightness, global, layout
	} = params;
	if (!colorEnabled) {
		return defaultBackColor.clone();
	}
	if (pressed) {
		const color = frontColor.clone();
		color.setChannelValue('alpha', brightness / 100 * color.getChannelValue('alpha'));
		return color;
	}
	const ripples = global?.ripples as Array<{ centerIndex: number, progress: number }> | undefined;
	let t = 0;
	if (ripples && ripples.length > 0) {
		for (const ripple of ripples) {
			const { centerIndex, progress } = ripple;
			const centerX = layout[centerIndex]?.x ?? 0;
			const centerY = layout[centerIndex]?.y ?? 0;
			const btnX = layout[index]?.x ?? 0;
			const btnY = layout[index]?.y ?? 0;
			const maxDist = Math.max(...layout.map(btn => Math.hypot(btn.x - centerX, btn.y - centerY)));
			const rippleRadius = progress * maxDist * 1.1;
			const rippleWidth = 80;
			const dist = Math.hypot(btnX - centerX, btnY - centerY);
			if (Math.abs(rippleRadius - dist) < rippleWidth) {
				const tt = Math.cos((Math.abs(rippleRadius - dist) / rippleWidth) * Math.PI / 2);
				t = Math.max(t, tt);
			}
		}
	}
	const result = new GamePadColor();
	lerpColor(result, backColor1, backColor2, t);
	result.setChannelValue('alpha', brightness / 100 * result.getChannelValue('alpha'));
	return result;
};

// 用于记录雨暴动画的状态
const transformPassedPositions: Set<number> = new Set();
let transformCycleCount = 0;
let lastTransformProgress = 0;

export const transformAnimation: LedAnimationAlgorithm = ({
	index,
	progress,
	backColor1,
	backColor2,
	brightness,
	colorEnabled,
	pressed,
	frontColor,
	defaultBackColor,
	layout,
}) => {
	// 颜色未启用，返回默认色
	if (!colorEnabled) {
		return defaultBackColor.clone();
	}
	// 按下时返回前景色
	if (pressed) {
		const color = frontColor.clone();
		color.setChannelValue('alpha', brightness / 100 * color.getChannelValue('alpha'));
		return color;
	}
	
	// 检测新的动画周期开始
	if (progress < lastTransformProgress && lastTransformProgress > 0.8) {
		transformCycleCount++;
		transformPassedPositions.clear();
	}
	lastTransformProgress = progress;
	
	// 流光参数
	const minX = Math.min(...layout.map(btn => btn.x)) - 100;
	const maxX = Math.max(...layout.map(btn => btn.x)) + 100;
	const bandWidth = 140;
	const centerX = minX + (maxX - minX) * progress * 1.6;
	const btnX = layout[index]?.x ?? minX;
	
	// 记录流光已经经过的按钮
	if (centerX > btnX + bandWidth / 2) {
		transformPassedPositions.add(index);
	}
	
	// 计算该按钮被经过的总次数
	const hasBeenPassed = transformPassedPositions.has(index);
	const totalPasses = transformCycleCount + (hasBeenPassed ? 1 : 0);
	const isOddPasses = totalPasses % 2 === 1;
	
	// 确定该按钮当前应该显示的基础颜色（经过后永久改变）
	const buttonBaseColor = isOddPasses ? backColor2 : backColor1;
	const buttonAltColor = isOddPasses ? backColor1 : backColor2;
	
	// 计算渐变区域
	const leftEdge = centerX - bandWidth / 2;
	const rightEdge = centerX + bandWidth / 2;
	
	let color: GamePadColor;
	
	if (btnX < leftEdge) {
		// 左侧区域：使用该按钮的基础颜色
		color = buttonBaseColor.clone();
	} else if (btnX > rightEdge) {
		// 右侧区域：使用该按钮的基础颜色
		color = buttonBaseColor.clone();
	} else {
		// 渐变区域：从基础颜色渐变到替代颜色
		const t = (btnX - leftEdge) / bandWidth; // 0~1
		// 使用 smoothstep 函数创建更平滑的过渡
		const smoothT = t * t * (3 - 2 * t);
		color = new GamePadColor();
		lerpColor(color, buttonAltColor, buttonBaseColor, smoothT);
	}
	
	color.setChannelValue('alpha', brightness / 100 * color.getChannelValue('alpha'));
	return color;
};

export const ledAnimations: Record<LedsEffectStyle, LedAnimationAlgorithm> = {
	[LedsEffectStyle.STATIC]: staticAnimation,
	[LedsEffectStyle.BREATHING]: breathingAnimation,
	[LedsEffectStyle.STAR]: starAnimation,
	[LedsEffectStyle.FLOWING]: flowingAnimation,
	[LedsEffectStyle.RIPPLE]: rippleAnimation,
	[LedsEffectStyle.TRANSFORM]: transformAnimation,
}; 