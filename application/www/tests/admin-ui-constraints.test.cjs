const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const webRoot = path.resolve(__dirname, '..');
const adminPage = fs.readFileSync(
    path.join(webRoot, 'app', 'admin', 'users', 'page.tsx'),
    'utf8'
);
const rootLayout = fs.readFileSync(
    path.join(webRoot, 'app', 'layout.tsx'),
    'utf8'
);

test('administration page uses Chakra components without custom styling systems', () => {
    assert.match(adminPage, /from '@chakra-ui\/react'/);
    assert.doesNotMatch(adminPage, /className\s*=/);
    assert.doesNotMatch(adminPage, /style\s*=/);
    assert.doesNotMatch(adminPage, /styled-components/);
    assert.doesNotMatch(adminPage, /tailwind/i);
    assert.doesNotMatch(adminPage, /import\s+['"][^'"]+\.css['"]/);
    assert.doesNotMatch(adminPage, /GamepadConfigProvider|useGamepadConfig/);
});

test('administration route is mounted outside the HID provider', () => {
    assert.match(rootLayout, /pathname === '\/admin\/users'/);
    assert.match(
        rootLayout,
        /if \(isEmailVerification \|\| isAdministration\)\s*\{[\s\S]*?<UserAuthProvider>/
    );
});
