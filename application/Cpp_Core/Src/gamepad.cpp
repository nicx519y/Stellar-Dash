#include "gamepad.hpp"
#include "storagemanager.hpp"
#include "drivermanager.hpp"
#include "micro_timer.hpp"

// 防抖算法时间常量定义
#define DEBOUNCE_T1_US 0  // T1时间：10毫秒 (p->r延时)
#define DEBOUNCE_T2_US 0   // T2时间：5毫秒 (r->p延时)
#define USE_DEBOUNCE_FILTER 1

Gamepad::Gamepad()
{
	options = Storage::getInstance().getDefaultGamepadProfile();
	// 初始化所有按键的防抖状态
	for (int i = 0; i < 32; i++) {
		buttonDebounceStates[i] = ButtonDebounceState();
	}
}

void Gamepad::setup()
{
    mapDpadUp    = new GamepadButtonMapping(options->keysConfig.keyDpadUp, GAMEPAD_MASK_UP);
	mapDpadDown  = new GamepadButtonMapping(options->keysConfig.keyDpadDown, GAMEPAD_MASK_DOWN);
	mapDpadLeft  = new GamepadButtonMapping(options->keysConfig.keyDpadLeft, GAMEPAD_MASK_LEFT);
	mapDpadRight = new GamepadButtonMapping(options->keysConfig.keyDpadRight, GAMEPAD_MASK_RIGHT);
	mapButtonB1  = new GamepadButtonMapping(options->keysConfig.keyButtonB1, GAMEPAD_MASK_B1);
	mapButtonB2  = new GamepadButtonMapping(options->keysConfig.keyButtonB2, GAMEPAD_MASK_B2);
	mapButtonB3  = new GamepadButtonMapping(options->keysConfig.keyButtonB3, GAMEPAD_MASK_B3);
	mapButtonB4  = new GamepadButtonMapping(options->keysConfig.keyButtonB4, GAMEPAD_MASK_B4);
	mapButtonL1  = new GamepadButtonMapping(options->keysConfig.keyButtonL1, GAMEPAD_MASK_L1);
	mapButtonR1  = new GamepadButtonMapping(options->keysConfig.keyButtonR1, GAMEPAD_MASK_R1);
	mapButtonL2  = new GamepadButtonMapping(options->keysConfig.keyButtonL2, GAMEPAD_MASK_L2);
	mapButtonR2  = new GamepadButtonMapping(options->keysConfig.keyButtonR2, GAMEPAD_MASK_R2);
	mapButtonS1  = new GamepadButtonMapping(options->keysConfig.keyButtonS1, GAMEPAD_MASK_S1);
	mapButtonS2  = new GamepadButtonMapping(options->keysConfig.keyButtonS2, GAMEPAD_MASK_S2);
	mapButtonL3  = new GamepadButtonMapping(options->keysConfig.keyButtonL3, GAMEPAD_MASK_L3);
	mapButtonR3  = new GamepadButtonMapping(options->keysConfig.keyButtonR3, GAMEPAD_MASK_R3);
	mapButtonA1  = new GamepadButtonMapping(options->keysConfig.keyButtonA1, GAMEPAD_MASK_A1);
	mapButtonA2  = new GamepadButtonMapping(options->keysConfig.keyButtonA2, GAMEPAD_MASK_A2);
	mapButtonFn  = new GamepadButtonMapping(options->keysConfig.keyButtonFn, AUX_MASK_FUNCTION);

}

void Gamepad::process()
{
	memcpy(&rawState, &state, sizeof(GamepadState));

	// NOTE: Inverted X/Y-axis must run before SOCD and Dpad processing
	if (options->keysConfig.invertXAxis) {
		bool left = (state.dpad & mapDpadLeft->buttonMask) != 0;
		bool right = (state.dpad & mapDpadRight->buttonMask) != 0;
		state.dpad &= ~(mapDpadLeft->buttonMask | mapDpadRight->buttonMask);
		if (left)
			state.dpad |= mapDpadRight->buttonMask;
		if (right)
			state.dpad |= mapDpadLeft->buttonMask;
	}

	if (options->keysConfig.invertYAxis) {
		bool up = (state.dpad & mapDpadUp->buttonMask) != 0;
		bool down = (state.dpad & mapDpadDown->buttonMask) != 0;
		state.dpad &= ~(mapDpadUp->buttonMask | mapDpadDown->buttonMask);
		if (up)
			state.dpad |= mapDpadDown->buttonMask;
		if (down)
			state.dpad |= mapDpadUp->buttonMask;
	}

	// 4-way before SOCD, might have better history without losing any coherent functionality
	if (options->keysConfig.fourWayMode) {
		state.dpad = filterToFourWayMode(state.dpad);
	}

	state.dpad = runSOCDCleaner(resolveSOCDMode(*options), state.dpad);
}

void Gamepad::deinit()
{
    delete mapDpadUp;
	delete mapDpadDown;
	delete mapDpadLeft;
	delete mapDpadRight;
	delete mapButtonB1;
	delete mapButtonB2;
	delete mapButtonB3;
	delete mapButtonB4;
	delete mapButtonL1;
	delete mapButtonR1;
	delete mapButtonL2;
	delete mapButtonR2;
	delete mapButtonS1;
	delete mapButtonS2;
	delete mapButtonL3;
	delete mapButtonR3;
	delete mapButtonA1;
	delete mapButtonA2;
	delete mapButtonFn;
	
	this->clearState();

	// 重置所有按键的防抖状态
	for (int i = 0; i < 32; i++) {
		buttonDebounceStates[i] = ButtonDebounceState();
	}
}

/**
 * @brief 单个按键的防抖状态机算法
 * 
 * 状态机工作原理：
 * 1. IDLE状态：检测到任何状态变化时，启动T1计时器，进入T1_WAITING状态
 * 2. T1_WAITING状态：
 *    - 如果T1时间内状态再次变化，刷新计时器，进入T2_WAITING状态
 *    - 如果T1时间到达且状态稳定，确认当前状态，回到IDLE状态
 * 3. T2_WAITING状态：
 *    - 如果T2时间内状态再次变化，刷新计时器，继续T2_WAITING状态
 *    - 如果T2时间到达且状态稳定，确认当前状态，回到IDLE状态
 * 
 * @param bitPosition 按键在32位掩码中的位置 (0-31)
 * @param currentValue 当前按键的采样值 (true=按下, false=释放)
 * @return 经过防抖处理的按键状态
 */
bool Gamepad::debounceButton(uint8_t bitPosition, bool currentValue)
{
	if (bitPosition >= 32) return currentValue; // 边界检查
	
	ButtonDebounceState& debounce = buttonDebounceStates[bitPosition];
	uint32_t currentTime = MICROS_TIMER.micros();
	
	switch (debounce.state) {
		case DEBOUNCE_IDLE:
			// 空闲状态，检测任何状态变化
			if (currentValue != debounce.lastStableValue) {
				// 状态发生变化，启动T1计时器，进入T1等待状态
				debounce.currentSampleValue = currentValue;
				debounce.timerStartTime = currentTime;
				debounce.state = DEBOUNCE_T1_WAITING;
			}
			return debounce.lastStableValue;
			
		case DEBOUNCE_T1_WAITING:
			// T1延时等待状态
			if (currentValue != debounce.currentSampleValue) {
				// 状态再次变化，刷新计时器，进入T2状态
				debounce.currentSampleValue = currentValue;
				debounce.timerStartTime = currentTime;
				debounce.state = DEBOUNCE_T2_WAITING;
			} else {
				// 状态没有变化，检查T1时间是否到达
				if ((currentTime - debounce.timerStartTime) >= DEBOUNCE_T1_US) {
					// T1时间到达，确认当前状态
					debounce.lastStableValue = debounce.currentSampleValue;
					debounce.state = DEBOUNCE_IDLE;
				}
			}
			return debounce.lastStableValue;
			
		case DEBOUNCE_T2_WAITING:
			// T2延时等待状态
			if (currentValue != debounce.currentSampleValue) {
				// 状态再次变化，刷新计时器，继续T2状态
				debounce.currentSampleValue = currentValue;
				debounce.timerStartTime = currentTime;
				// 保持在T2_WAITING状态
			} else {
				// 状态没有变化，检查T2时间是否到达
				if ((currentTime - debounce.timerStartTime) >= DEBOUNCE_T2_US) {
					// T2时间到达，确认当前状态
					debounce.lastStableValue = debounce.currentSampleValue;
					debounce.state = DEBOUNCE_IDLE;
				}
			}
			return debounce.lastStableValue;
			
		default:
			// 异常状态，重置为IDLE
			debounce.state = DEBOUNCE_IDLE;
			return debounce.lastStableValue;
	}
}

/**
 * @brief 对整个32位输入掩码进行防抖处理
 * @param currentValues 当前输入值掩码
 * @return 经过防抖处理的输入掩码
 */
Mask_t Gamepad::debounceFilter(Mask_t currentValues)
{
	Mask_t result = 0;
	
	// 对每一位进行防抖处理
	for (uint8_t i = 0; i < 32; i++) {
		bool currentBit = (currentValues & (1U << i)) != 0;
		bool debouncedBit = debounceButton(i, currentBit);
		
		if (debouncedBit) {
			result |= (1U << i);
		}
	}
	
	return result;
}

void Gamepad::read(Mask_t values)
{
	// 应用防抖过滤
	#if USE_DEBOUNCE_FILTER == 1
		Mask_t filteredValues = debounceFilter(values);
	#else
		Mask_t filteredValues = values;
	#endif
	
	state.aux = 0
		| (filteredValues & mapButtonFn->virtualPinMask)   ? mapButtonFn->buttonMask : 0;

	state.dpad = 0
		| ((filteredValues & mapDpadUp->virtualPinMask)    ? mapDpadUp->buttonMask : 0)
		| ((filteredValues & mapDpadDown->virtualPinMask)  ? mapDpadDown->buttonMask : 0)
		| ((filteredValues & mapDpadLeft->virtualPinMask)  ? mapDpadLeft->buttonMask  : 0)
		| ((filteredValues & mapDpadRight->virtualPinMask) ? mapDpadRight->buttonMask : 0)
	;

	state.buttons = 0
		| ((filteredValues & mapButtonB1->virtualPinMask)  ? mapButtonB1->buttonMask  : 0)
		| ((filteredValues & mapButtonB2->virtualPinMask)  ? mapButtonB2->buttonMask  : 0)
		| ((filteredValues & mapButtonB3->virtualPinMask)  ? mapButtonB3->buttonMask  : 0)
		| ((filteredValues & mapButtonB4->virtualPinMask)  ? mapButtonB4->buttonMask  : 0)
		| ((filteredValues & mapButtonL1->virtualPinMask)  ? mapButtonL1->buttonMask  : 0)
		| ((filteredValues & mapButtonR1->virtualPinMask)  ? mapButtonR1->buttonMask  : 0)
		| ((filteredValues & mapButtonL2->virtualPinMask)  ? mapButtonL2->buttonMask  : 0)
		| ((filteredValues & mapButtonR2->virtualPinMask)  ? mapButtonR2->buttonMask  : 0)
		| ((filteredValues & mapButtonS1->virtualPinMask)  ? mapButtonS1->buttonMask  : 0)
		| ((filteredValues & mapButtonS2->virtualPinMask)  ? mapButtonS2->buttonMask  : 0)
		| ((filteredValues & mapButtonL3->virtualPinMask)  ? mapButtonL3->buttonMask  : 0)
		| ((filteredValues & mapButtonR3->virtualPinMask)  ? mapButtonR3->buttonMask  : 0)
		| ((filteredValues & mapButtonA1->virtualPinMask)  ? mapButtonA1->buttonMask  : 0)
		| ((filteredValues & mapButtonA2->virtualPinMask)  ? mapButtonA2->buttonMask  : 0)
	;

	state.lx = GAMEPAD_JOYSTICK_MID;
	state.ly = GAMEPAD_JOYSTICK_MID;
	state.rx = GAMEPAD_JOYSTICK_MID;
	state.ry = GAMEPAD_JOYSTICK_MID;
	state.lt = 0;
	state.rt = 0;

	process();
}

void Gamepad::clearState()
{
	state.dpad = 0;
	state.buttons = 0;
	state.aux = 0;
	state.lx = GAMEPAD_JOYSTICK_MID;
	state.ly = GAMEPAD_JOYSTICK_MID;
	state.rx = GAMEPAD_JOYSTICK_MID;
	state.ry = GAMEPAD_JOYSTICK_MID;
	state.lt = 0;
	state.rt = 0;
}

void Gamepad::setSOCDMode(SOCDMode socdMode) {
    options->keysConfig.socdMode = socdMode;
}

void Gamepad::resetDebounceState() {
	for (int i = 0; i < 32; i++) {
		buttonDebounceStates[i] = ButtonDebounceState();
	}
}

uint8_t Gamepad::getButtonDebounceState(uint8_t bitPosition) const {
	if (bitPosition >= 32) return 0;
	return static_cast<uint8_t>(buttonDebounceStates[bitPosition].state);
}

bool Gamepad::getButtonLastStableValue(uint8_t bitPosition) const {
	if (bitPosition >= 32) return false;
	return buttonDebounceStates[bitPosition].lastStableValue;
}



