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
	btnLen: number;
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
	out.setChannelValue('red', colorA.getChannelValue('red') * (1 - t) + colorB.getChannelValue('red') * t);
	out.setChannelValue('green', colorA.getChannelValue('green') * (1 - t) + colorB.getChannelValue('green') * t);
	out.setChannelValue('blue', colorA.getChannelValue('blue') * (1 - t) + colorB.getChannelValue('blue') * t);
	out.setChannelValue('alpha', colorA.getChannelValue('alpha') * (1 - t) + colorB.getChannelValue('alpha') * t);
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

// 占位动画算法，后续可替换为实际实现
export const starAnimation: LedAnimationAlgorithm = staticAnimation;
export const layoutAnimation: LedAnimationAlgorithm = staticAnimation;

export const ledAnimations: Record<LedsEffectStyle, LedAnimationAlgorithm> = {
	[LedsEffectStyle.STATIC]: staticAnimation,
	[LedsEffectStyle.BREATHING]: breathingAnimation,
	[LedsEffectStyle.STAR]: starAnimation,
	[LedsEffectStyle.LAYOUT]: layoutAnimation,
}; 