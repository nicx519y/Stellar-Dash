const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { transform } = require('sucrase');
const { chromium } = require('playwright');

test('React consumers ignore unrelated state and action updates, and resubscribe when selector keys change', { timeout: 60000 }, async () => {
  const browser = await chromium.launch({ channel: 'chrome', headless: true });
  try {
    const page = await browser.newPage();
    await page.setContent('<main id="root"></main>');
    await page.addScriptTag({ path: path.join(path.dirname(require.resolve('react/package.json')), 'umd/react.development.js') });
    await page.addScriptTag({ path: path.join(path.dirname(require.resolve('react-dom/package.json')), 'umd/react-dom.development.js') });
    for (const module of ['selection-store', 'selection-context']) {
      const source = fs.readFileSync(path.join(__dirname, '../contexts/gamepad-config', `${module}.ts`), 'utf8');
      const compiled = transform(source, { transforms: ['typescript', 'imports'] }).code;
      await page.addScriptTag({ content: `(() => { const exports = {}; const require = name => name === 'react' ? window.React : window.selectionModules[name.replace('./', '')]; ${compiled}; (window.selectionModules ??= {})[${JSON.stringify(module)}] = exports; })();` });
    }
    const evidence = await page.evaluate(() => {
      const { SelectionStore } = window.selectionModules['selection-store'];
      const { createSelectionContext } = window.selectionModules['selection-context'];
      const { Context, useFields } = createSelectionContext();
      let calls = [];
      let counts = { firmware: 0, dynamic: 0 };
      let observed, dynamicValue;
      const Firmware = React.memo(() => { observed = useFields('firmware', 'action'); counts.firmware++; return null; });
      const Dynamic = React.memo(({ field }) => { dynamicValue = useFields(field); counts.dynamic++; return null; });
      const store = new SelectionStore({ firmware: 'v1', calibration: 0, action: () => calls.push('first') });
      const root = ReactDOM.createRoot(document.getElementById('root'));
      const render = field => ReactDOM.flushSync(() => root.render(React.createElement(Context.Provider, { value: store },
        React.createElement(Firmware), React.createElement(Dynamic, { field }))));
      render('calibration');
      const initial = { ...counts };
      const action = observed.action;
      ReactDOM.flushSync(() => store.publish({ firmware: 'v1', calibration: 1, action: () => calls.push('second') }));
      const unrelated = { ...counts };
      action();
      render('firmware');
      const switched = { counts: { ...counts }, keys: Object.keys(dynamicValue), value: dynamicValue.firmware };
      ReactDOM.flushSync(() => store.publish({ firmware: 'v1', calibration: 2, action: () => calls.push('third') }));
      const afterSwitch = { ...counts };
      ReactDOM.flushSync(() => store.publish({ firmware: 'v1', calibration: 2, action: () => calls.push('fourth') }));
      const actionsOnly = { ...counts };
      action();
      ReactDOM.flushSync(() => store.publish({ firmware: 'v2', calibration: 2, action: () => calls.push('fifth') }));
      const changed = { ...counts };
      const stableAction = action === observed.action;
      ReactDOM.flushSync(() => root.unmount());
      return { initial, unrelated, switched, afterSwitch, actionsOnly, changed, calls, stableAction };
    });
    assert.deepEqual(evidence.initial, { firmware: 1, dynamic: 1 });
    assert.deepEqual(evidence.unrelated, { firmware: 1, dynamic: 2 });
    assert.deepEqual(evidence.switched, { counts: { firmware: 1, dynamic: 3 }, keys: ['firmware'], value: 'v1' });
    assert.deepEqual(evidence.afterSwitch, evidence.switched.counts);
    assert.deepEqual(evidence.actionsOnly, evidence.afterSwitch);
    assert.deepEqual(evidence.changed, { firmware: 2, dynamic: 4 });
    assert.deepEqual(evidence.calls, ['second', 'fourth']);
    assert.equal(evidence.stableAction, true);
    console.log(JSON.stringify(evidence));
  } finally {
    await browser.close();
  }
});
