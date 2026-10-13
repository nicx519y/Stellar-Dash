const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
exports.readGamepadImplementation = () => [
  'contexts/gamepad-config-context.tsx',
  'contexts/gamepad-config/contract.ts',
  'contexts/gamepad-config/session-actions.ts',
  'contexts/gamepad-config/session-lifecycle.ts',
  'contexts/gamepad-config/session-reset.ts',
  'contexts/gamepad-config/configuration-actions.ts',
  'contexts/gamepad-config/configuration-transactions.ts',
  'contexts/gamepad-config/mapping-actions.ts',
  'contexts/gamepad-config/feedback-actions.ts',
  'contexts/gamepad-config/firmware-actions.ts',
].map(read).join('\n');
exports.readSwitchMarkingImplementation = () => [
  'components/switch-marking-content.tsx',
  'components/switch-marking/shared.ts',
  'components/switch-marking/use-switch-marking-controller.ts',
  'components/switch-marking/switch-marking-view.tsx',
  'components/switch-marking/switch-mapping-catalog.tsx',
  'components/switch-marking/switch-mapping-editor.tsx',
  'components/switch-marking/switch-mapping-curve-editor.tsx',
].map(read).join('\n');
