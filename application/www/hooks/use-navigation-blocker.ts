import { useEffect } from 'react';
import { navigationEvents } from '@/lib/event-manager';
import { openConfirm } from '@/components/dialog-confirm';

/**
 * 通用页面切换拦截 Hook
 * @param active 是否激活拦截
 * @param title 弹窗标题
 * @param message 弹窗内容
 * @param onConfirm 用户点击确认时的异步操作，返回 true 允许跳转
 */
export function useNavigationBlocker(
  active: boolean,
  title: string,
  message: string,
  onConfirm?: () => Promise<boolean>
) {
  useEffect(() => {
    if (!active) return;

    const handleBeforeNavigate = async ({ path }: { path: string }) => {
      try {
        const confirmed = await openConfirm({ title, message });
        if (confirmed) {
          if (onConfirm) {
            return await onConfirm();
          }
          return true;
        }
        return false;
      } catch {
        return false;
      }
    };

    navigationEvents.addListener(handleBeforeNavigate);
    return () => navigationEvents.removeListener(handleBeforeNavigate);
  }, [active, title, message, onConfirm]);
} 