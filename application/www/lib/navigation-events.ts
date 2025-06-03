type NavigationHandler = (event: { path: string }) => Promise<boolean>;

class NavigationEvents {
    private handlers: NavigationHandler[] = [];

    addListener(handler: NavigationHandler) {
        this.handlers.push(handler);
    }

    removeListener(handler: NavigationHandler) {
        const index = this.handlers.indexOf(handler);
        if (index !== -1) {
            this.handlers.splice(index, 1);
        }
    }

    async notify(event: { path: string }): Promise<boolean> {
        for (const handler of this.handlers) {
            try {
                const result = await handler(event);
                if (!result) {
                    return false;
                }
            } catch {
                return false;
            }
        }
        return true;
    }
}

export const navigationEvents = new NavigationEvents(); 