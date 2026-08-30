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
const settingsLayout = fs.readFileSync(
    path.join(webRoot, 'components', 'settings-layout.tsx'),
    'utf8'
);
const switchMarking = fs.readFileSync(
    path.join(webRoot, 'components', 'switch-marking-content.tsx'),
    'utf8'
);
const gamepadConfigContext = fs.readFileSync(
    path.join(webRoot, 'contexts', 'gamepad-config-context.tsx'),
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

test('switch mapping catalog is visible to device users and admin actions live on equal-size cards', () => {
    assert.match(
        settingsLayout,
        /\{ id: 'switch-marking' as Route, label: t\.SETTINGS_TAB_SWITCH_MARKING/
    );
    assert.doesNotMatch(settingsLayout, /isAdmin \? \[[\s\S]*switch-marking/);
    assert.match(switchMarking, /const \{ session \} = useUserAuth\(\)/);
    assert.doesNotMatch(switchMarking, /映射实验室|RAM 草稿/);
    assert.match(switchMarking, /installSwitchMapping/);
    assert.match(switchMarking, /fetchSwitchMappingDetail/);
    assert.match(switchMarking, /setSelectedMapping\(detail\.revision\.mapping\)/);
    assert.match(switchMarking, /uploadSwitchMappingImage/);
    assert.match(switchMarking, /updateSwitchMappingMetadata/);
    assert.match(switchMarking, /deleteSwitchMapping/);
    assert.match(switchMarking, /clearInstalledSwitchMapping/);
    assert.match(switchMarking, /useLanguage/);
    assert.match(switchMarking, /aria-label=\{t\.SWITCH_MAPPING_ADD_ARIA\}/);
    assert.match(switchMarking, /isAdmin && item\.catalogId/);
    assert.match(switchMarking, /const CARD_WIDTH = "104px"/);
    assert.match(switchMarking, /const CARD_HEIGHT = "103px"/);
    assert.match(switchMarking, /width="1700px"/);
    assert.match(switchMarking, /padding="30px"/);
    assert.match(switchMarking, /overflowX="auto"/);
    assert.match(switchMarking, /flex="1 1 0"/);
    assert.match(
        settingsLayout,
        /overflow=\{currentRoute === 'switch-marking' \? 'hidden' : undefined\}/
    );
    assert.ok(
        switchMarking.indexOf('{axisItems.map') <
        switchMarking.indexOf('aria-label={t.SWITCH_MAPPING_ADD_ARIA}'),
        'add card should follow existing switch cards'
    );
    assert.match(switchMarking, /hoveredMappingId === item\.mappingId/);
    assert.match(switchMarking, /LuPencil/);
    assert.match(switchMarking, /LuDownload/);
    assert.match(switchMarking, /downloadPulse/);
    assert.match(switchMarking, /aria-label=\{t\.SWITCH_MAPPING_DOWNLOADING\}/);
    assert.doesNotMatch(switchMarking, /SWITCH_MAPPING_DRAFT/);
    assert.doesNotMatch(switchMarking, /SWITCH_MAPPING_RECORDING_TOOLBAR_TITLE/);
    assert.match(switchMarking, /alignItems="flex-end"/);
    assert.match(switchMarking, /startMarking/);
    assert.match(switchMarking, /stopMarking/);
    assert.match(switchMarking, /stepMarking/);
    assert.match(switchMarking, /updateSwitchMappingCurve/);
    assert.match(switchMarking, /serverSyncBusy/);
    assert.match(switchMarking, /SWITCH_MAPPING_NAME_DUPLICATE_TITLE/);
    assert.match(switchMarking, /item\.displayName\.trim\(\)\.toLocaleLowerCase\(\)/);
    assert.match(switchMarking, /editorLength/);
    assert.match(switchMarking, /editorStep/);
    assert.match(switchMarking, /const COVER_ASPECT_RATIO = "23 \/ 16"/);
    assert.match(switchMarking, /reader\.readAsDataURL\(file\)/);
    assert.doesNotMatch(switchMarking, /revokeObjectURL\(editorImagePreview\)/);
    assert.match(switchMarking, /blobToDataUrl\(image\)/);
    assert.doesNotMatch(switchMarking, /URL\.createObjectURL\(image\.image\)/);
    assert.doesNotMatch(switchMarking, /createdUrls\.forEach\(url => URL\.revokeObjectURL/);
    assert.match(switchMarking, /aspectRatio=\{COVER_ASPECT_RATIO\}/);
    assert.match(switchMarking, /SWITCH_MAPPING_COVER_CROP_HELPER/);
    assert.match(switchMarking, /onPointerDown=\{beginEditorCropDrag\}/);
    assert.match(switchMarking, /onWheel=\{zoomEditorCropWithWheel\}/);
    assert.match(switchMarking, /onClick=\{openEditorImagePicker\}/);
    assert.match(switchMarking, /ref=\{editorImageInputRef\}/);
    assert.match(switchMarking, /LuImagePlus/);
    assert.match(switchMarking, /!editorImage && \(/);
    assert.match(switchMarking, /type="file"[\s\S]*?hidden/);
    assert.doesNotMatch(switchMarking, /<Input\s+mt=\{2\}\s+type="file"/);
    assert.match(switchMarking, /type="range"/);
    assert.match(switchMarking, /canvas\.toBlob\(resolve, "image\/webp", 0\.9\)/);
    assert.match(switchMarking, /createCroppedCoverFile\(\)/);
    assert.doesNotMatch(switchMarking, /return \[\.\.\.items\.values\(\)\]\.sort/);
});

test('switch mapping page merges device and server entries by mapping ID without reentrant initialization', () => {
    assert.match(switchMarking, /const items = new Map<string, AxisListItem>\(\)/);
    assert.match(switchMarking, /items\.set\(item\.revisionId/);
    assert.match(switchMarking, /items\.set\(deviceMapping\.id/);
    assert.match(switchMarking, /initializationRunningRef\.current/);
    assert.match(switchMarking, /Promise\.allSettled\(\[/);

    const catalogLoader = gamepadConfigContext.slice(
        gamepadConfigContext.indexOf('const fetchSwitchMappingCatalog ='),
        gamepadConfigContext.indexOf('const fetchSwitchMappingDetail =')
    );
    assert.match(catalogLoader, /fetchSwitchMappingResponse/);
    assert.doesNotMatch(catalogLoader, /ms_get_list/);
});

test('adding a server switch mapping never replaces the mapping installed on the device', () => {
    const creator = gamepadConfigContext.slice(
        gamepadConfigContext.indexOf('const createSwitchMappingFromCurrent ='),
        gamepadConfigContext.indexOf('const beginMappingDraft =')
    );
    assert.match(creator, /\/api\/admin\/switch-mappings\/blank/);
    assert.match(creator, /length: input\.length/);
    assert.match(creator, /step: input\.step/);
    assert.doesNotMatch(creator, /\/publish/);
    assert.doesNotMatch(creator, /installCanonicalMapping|ms_install_mapping/);
});

test('recording publishes each completed step immediately and stop stays available during persistence', () => {
    assert.match(switchMarking, /originalValues: \[\.\.\.markingStatus\.values\]/);
    assert.match(switchMarking, /Promise\.all\(\[\s*syncMarkingProgress\(\),\s*updateSwitchMappingCurve/);
    assert.match(switchMarking, /runRecordingAction\(stopMarking, true\)/);
    assert.match(
        switchMarking,
        /disabled=\{!markingStatus\.is_marking &&\s*\(serverSyncBusy/,
    );
});
