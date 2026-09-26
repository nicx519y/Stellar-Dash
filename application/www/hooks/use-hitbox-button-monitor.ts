import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { ButtonStateBinaryData } from '@/lib/button-binary-parser';
import { reconcileButtonStateSnapshot } from '@/lib/button-state-snapshot';
import { useButtonMonitor } from '@/hooks/use-button-monitor';

interface UseHitboxButtonMonitorOptions {
  buttonCount: number;
  interactiveIds: readonly number[];
  disabledIds?: readonly number[];
  enabled: boolean;
  onButtonChange?: (buttonId: number) => void;
  logPrefix: string;
}

function statesEqual(left: readonly number[], right: readonly number[]): boolean {
  return left.length === right.length
    && left.every((value, index) => value === right[index]);
}

/**
 * 普通 WebConfig 配置页使用的硬件按键监听。
 *
 * 设备上报的是完整掩码；previousMaskRef 跨 render 保存上一帧，因此既能
 * 可靠识别按下，也能在 triggerMask 变为 0 时识别抬起。
 */
export function useHitboxButtonMonitor({
  buttonCount,
  interactiveIds,
  disabledIds = [],
  enabled,
  onButtonChange,
  logPrefix,
}: UseHitboxButtonMonitorOptions): number[] {
  const previousMaskRef = useRef(0);
  const latestRawMaskRef = useRef(0);
  const acceptingEventsRef = useRef(false);
  const optionsRef = useRef({
    buttonCount,
    interactiveIds,
    disabledIds,
    onButtonChange,
    logPrefix,
  });
  optionsRef.current = {
    buttonCount,
    interactiveIds,
    disabledIds,
    onButtonChange,
    logPrefix,
  };

  const [hardwareButtonStates, setHardwareButtonStates] = useState<number[]>(
    () => Array(Math.max(0, buttonCount)).fill(-1),
  );

  const interactiveSignature = useMemo(
    () => interactiveIds.join(','),
    [interactiveIds],
  );
  const disabledSignature = useMemo(
    () => disabledIds.join(','),
    [disabledIds],
  );

  const clearState = useCallback(() => {
    latestRawMaskRef.current = 0;
    previousMaskRef.current = 0;
    setHardwareButtonStates((current) => {
      const cleared = Array(Math.max(0, buttonCount)).fill(-1);
      return statesEqual(current, cleared) ? current : cleared;
    });
  }, [buttonCount]);

  const { startMonitoring, stopMonitoring } = useButtonMonitor({
    autoInitialize: false,
    onButtonStatesChange: (data: ButtonStateBinaryData) => {
      if (!acceptingEventsRef.current || !data) return;

      const current = optionsRef.current;
      latestRawMaskRef.current = Number(data.triggerMask) >>> 0;
      const reconciled = reconcileButtonStateSnapshot(
        previousMaskRef.current,
        latestRawMaskRef.current,
        current.buttonCount,
        current.interactiveIds,
        current.disabledIds,
      );
      previousMaskRef.current = reconciled.mask;

      setHardwareButtonStates((previous) => (
        statesEqual(previous, reconciled.buttonStates)
          ? previous
          : reconciled.buttonStates
      ));

      for (const transition of reconciled.transitions) {
        current.onButtonChange?.(
          transition.pressed ? transition.buttonId : -1,
        );
      }
    },
    onError: (error) => {
      console.error(`${optionsRef.current.logPrefix}: 按键监听错误:`, error);
    },
    onMonitoringStateChange: (isActive) => {
      console.log(`${optionsRef.current.logPrefix}: 按键监听状态变化:`, isActive);
    },
  });

  // 布局或可交互范围变化时，用最近一次设备快照重新生成显示状态，但不把
  // 页面配置变化伪装成新的硬件按键边沿。
  useEffect(() => {
    const current = optionsRef.current;
    const reconciled = reconcileButtonStateSnapshot(
      0,
      latestRawMaskRef.current,
      current.buttonCount,
      current.interactiveIds,
      current.disabledIds,
    );
    previousMaskRef.current = reconciled.mask;
    setHardwareButtonStates((previous) => (
      statesEqual(previous, reconciled.buttonStates)
        ? previous
        : reconciled.buttonStates
    ));
  }, [buttonCount, interactiveSignature, disabledSignature]);

  useEffect(() => {
    if (!enabled) {
      acceptingEventsRef.current = false;
      clearState();
      return;
    }

    acceptingEventsRef.current = true;
    void startMonitoring().catch((error) => {
      acceptingEventsRef.current = false;
      clearState();
      console.error(`${optionsRef.current.logPrefix}: 启动按键监听失败:`, error);
    });

    return () => {
      acceptingEventsRef.current = false;
      clearState();
      void stopMonitoring().catch((error) => {
        console.error(`${optionsRef.current.logPrefix}: 停止按键监听失败:`, error);
      });
    };
  }, [clearState, enabled, startMonitoring, stopMonitoring]);

  return hardwareButtonStates;
}
