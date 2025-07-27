// 通用事件回调类型
type EventCallback = (data: unknown) => void | Promise<void>;

// 导航事件回调类型
type NavigationEventCallback = (event: { path: string }) => Promise<boolean>;

class NavigationEventManager {
  private listeners: NavigationEventCallback[] = [];

  addListener(callback: NavigationEventCallback) {
    this.listeners.push(callback);
  }

  removeListener(callback: NavigationEventCallback) {
    this.listeners = this.listeners.filter(listener => listener !== callback);
  }

  async emit(path: string): Promise<boolean> {
    for (const listener of this.listeners) {
      const shouldContinue = await listener({ path });
      if (!shouldContinue) return false;
    }
    return true;
  }
}

// 通用事件总线
class EventBus {
  private listeners: Map<string, Set<EventCallback>> = new Map();

  /**
   * 订阅事件
   * @param event 事件名称
   * @param callback 回调函数
   * @returns 取消订阅的函数
   */
  on(event: string, callback: EventCallback): () => void {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, new Set());
    }
    this.listeners.get(event)!.add(callback);
    
    return () => {
      this.listeners.get(event)?.delete(callback);
    };
  }

  /**
   * 发布事件
   * @param event 事件名称
   * @param data 事件数据
   */
  emit(event: string, data?: unknown): void {
    const eventListeners = this.listeners.get(event);
    if (eventListeners) {
      eventListeners.forEach(callback => {
        try {
          callback(data);
        } catch (error) {
          console.error(`Error in event listener for ${event}:`, error);
        }
      });
    }
  }

  /**
   * 取消事件的所有监听器
   * @param event 事件名称
   */
  off(event: string): void {
    this.listeners.delete(event);
  }

  /**
   * 获取事件的监听器数量
   * @param event 事件名称
   */
  listenerCount(event: string): number {
    return this.listeners.get(event)?.size || 0;
  }
}

export const navigationEvents = new NavigationEventManager();
export const eventBus = new EventBus();

// 导出事件名称常量
export const EVENTS = {
  MARKING_STATUS_UPDATE: 'marking_status_update',
  CONFIG_CHANGED: 'config_changed',
  CALIBRATION_UPDATE: 'calibration_update',
  BUTTON_STATE_CHANGED: 'button_state_changed',
  WEBSOCKET_DISCONNECTED: 'websocket_disconnected',
} as const; 