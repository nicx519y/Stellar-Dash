#include "gamepad.hpp"
#include "storagemanager.hpp"
#include "drivermanager.hpp"

Gamepad::Gamepad()
{
	options = Storage::getInstance().getDefaultGamepadProfile();
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

}

void Gamepad::read(Mask_t values)
{
	state.aux = 0
		| (values & mapButtonFn->virtualPinMask)   ? mapButtonFn->buttonMask : 0;

	state.dpad = 0
		| ((values & mapDpadUp->virtualPinMask)    ? mapDpadUp->buttonMask : 0)
		| ((values & mapDpadDown->virtualPinMask)  ? mapDpadDown->buttonMask : 0)
		| ((values & mapDpadLeft->virtualPinMask)  ? mapDpadLeft->buttonMask  : 0)
		| ((values & mapDpadRight->virtualPinMask) ? mapDpadRight->buttonMask : 0)
	;

	state.buttons = 0
		| ((values & mapButtonB1->virtualPinMask)  ? mapButtonB1->buttonMask  : 0)
		| ((values & mapButtonB2->virtualPinMask)  ? mapButtonB2->buttonMask  : 0)
		| ((values & mapButtonB3->virtualPinMask)  ? mapButtonB3->buttonMask  : 0)
		| ((values & mapButtonB4->virtualPinMask)  ? mapButtonB4->buttonMask  : 0)
		| ((values & mapButtonL1->virtualPinMask)  ? mapButtonL1->buttonMask  : 0)
		| ((values & mapButtonR1->virtualPinMask)  ? mapButtonR1->buttonMask  : 0)
		| ((values & mapButtonL2->virtualPinMask)  ? mapButtonL2->buttonMask  : 0)
		| ((values & mapButtonR2->virtualPinMask)  ? mapButtonR2->buttonMask  : 0)
		| ((values & mapButtonS1->virtualPinMask)  ? mapButtonS1->buttonMask  : 0)
		| ((values & mapButtonS2->virtualPinMask)  ? mapButtonS2->buttonMask  : 0)
		| ((values & mapButtonL3->virtualPinMask)  ? mapButtonL3->buttonMask  : 0)
		| ((values & mapButtonR3->virtualPinMask)  ? mapButtonR3->buttonMask  : 0)
		| ((values & mapButtonA1->virtualPinMask)  ? mapButtonA1->buttonMask  : 0)
		| ((values & mapButtonA2->virtualPinMask)  ? mapButtonA2->buttonMask  : 0)
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



