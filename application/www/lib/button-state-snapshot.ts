export interface ButtonStateTransition {
  buttonId: number;
  pressed: boolean;
}

export interface ReconciledButtonState {
  /** 仅保留当前页面可交互且未禁用的按键位。 */
  mask: number;
  buttonStates: number[];
  transitions: ButtonStateTransition[];
}

function normalizeButtonIds(
  buttonCount: number,
  interactiveIds: readonly number[],
  disabledIds: ReadonlySet<number>,
): number[] {
  const seen = new Set<number>();
  const ids: number[] = [];

  for (const rawId of interactiveIds) {
    const buttonId = Number(rawId);
    if (
      !Number.isInteger(buttonId)
      || buttonId < 0
      || buttonId >= buttonCount
      || buttonId >= 32
      || disabledIds.has(buttonId)
      || seen.has(buttonId)
    ) {
      continue;
    }
    seen.add(buttonId);
    ids.push(buttonId);
  }

  return ids;
}

/**
 * 将设备发送的完整按键掩码转换为页面显示状态和明确的按下/抬起边沿。
 *
 * previousMask 必须由调用方跨 React render 保存；返回的 mask 用作下一次
 * 调用的 previousMask。这样 setState 引发重渲染后也不会丢失抬起边沿。
 */
export function reconcileButtonStateSnapshot(
  previousMask: number,
  nextMask: number,
  buttonCount: number,
  interactiveIds: readonly number[],
  disabledIds: readonly number[] = [],
): ReconciledButtonState {
  const safeButtonCount = Math.max(0, Math.trunc(buttonCount));
  const disabledSet = new Set(disabledIds);
  const eligibleIds = normalizeButtonIds(
    safeButtonCount,
    interactiveIds,
    disabledSet,
  );

  let eligibleMask = 0;
  for (const buttonId of eligibleIds) {
    eligibleMask = (eligibleMask | (1 << buttonId)) >>> 0;
  }

  const previous = (Number(previousMask) >>> 0) & eligibleMask;
  const next = (Number(nextMask) >>> 0) & eligibleMask;
  const changed = (previous ^ next) >>> 0;
  const buttonStates = Array(safeButtonCount).fill(-1);
  const transitions: ButtonStateTransition[] = [];

  for (const buttonId of eligibleIds) {
    const bit = (1 << buttonId) >>> 0;
    const pressed = (next & bit) !== 0;
    buttonStates[buttonId] = pressed ? 1 : -1;
    if ((changed & bit) !== 0) {
      transitions.push({ buttonId, pressed });
    }
  }

  return {
    mask: next >>> 0,
    buttonStates,
    transitions,
  };
}
