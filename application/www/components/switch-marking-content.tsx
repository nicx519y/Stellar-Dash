'use client';
import { AdminAccessGuard } from './admin-access-guard';
import { useSwitchMarkingController } from './switch-marking/use-switch-marking-controller';
import { SwitchMarkingView } from './switch-marking/switch-marking-view';

/** Do not mount device or catalog effects before administrator access is granted. */
export function SwitchMarkingContent() {
    return <AdminAccessGuard><AdminSwitchMarkingContent /></AdminAccessGuard>;
}

function AdminSwitchMarkingContent() {
    const model = useSwitchMarkingController();
    return <SwitchMarkingView model={model} />;
}
