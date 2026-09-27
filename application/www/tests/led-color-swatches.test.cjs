const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const ts = require('typescript');

const source = fs.readFileSync(path.join(__dirname, '../components/leds-setting-content.tsx'), 'utf8');
const ast = ts.createSourceFile('leds.tsx', source, ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
const handlers = [];
function visit(node) {
  if (ts.isJsxOpeningElement(node) && node.tagName.getText(ast) === 'ColorPicker.SwatchTrigger') {
    const click = node.attributes.properties.find(p => p.name?.getText(ast) === 'onClick');
    handlers.push(click?.initializer?.expression?.getText(ast));
  }
  ts.forEachChild(node, visit);
}
visit(ast);

test('every button and ambient color swatch stages its selected color and requests preview without a drag-end event', () => {
  assert.equal(handlers.length, 2, 'both picker families are exercised');
  const families = ['handleLedColorChange', 'handleAroundLedColorChange'];
  for (let family = 0; family < families.length; family++) {
    assert.ok(handlers[family], `${families[family]} swatches must handle click/keyboard activation`);
    for (let index = 0; index < (family === 0 ? 3 : 2); index++) {
      for (const item of ['#ff0000', '#00ff00', '#0000ff', '#000000']) {
        const changes = [];
        const color = { hex: item };
        const activate = new Function('index', 'item', 'parseColor', ...families, 'requestLedsCommit',
          `return (${handlers[family]});`)(index, item, value => {
            assert.equal(value, item);
            return color;
          }, (slot, value) => changes.push(['button', slot, value]),
          (slot, value) => changes.push(['ambient', slot, value]),
          () => changes.push(['preview-and-stage']));
        // Intentionally no onValueChangeEnd: Chakra swatches never emit it.
        activate();
        assert.deepEqual(changes, [[family === 0 ? 'button' : 'ambient', index, color], ['preview-and-stage']]);
      }
    }
  }
});
