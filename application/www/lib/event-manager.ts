type EventCallback = (e: { path: string }) => boolean | Promise<boolean>;

class NavigationEventManager {
  private listeners: EventCallback[] = [];

  addListener(callback: EventCallback) {
    this.listeners.push(callback);
  }

  removeListener(callback: EventCallback) {
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

export const navigationEvents = new NavigationEventManager(); 