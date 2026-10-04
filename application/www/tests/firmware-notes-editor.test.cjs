const test = require('node:test');
const assert = require('node:assert/strict');
const { NotesEditor, MarkdownImportError, readMarkdownNotes } = require('../lib/admin/firmware-notes-editor.ts');

const file = (name, bytes) => ({ name, size: bytes.length, arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) });
const bytes = text => new TextEncoder().encode(text);
const tick = () => new Promise(resolve => setImmediate(resolve));

test('draft notes debounce rapid edits and report the persisted revision', async () => {
  const calls = [];
  const editor = new NotesEditor({ notes: 'old', revision: 1, delayMs: 15,
    save: async (revision, notes) => { calls.push([revision, notes]); return { revision: revision + 1, notes }; },
    onState() {}, });
  editor.change('first'); editor.change('final');
  assert.equal(editor.snapshot.status, 'unsaved');
  await new Promise(resolve => setTimeout(resolve, 40));
  assert.deepEqual(calls, [[1, 'final']]);
  assert.deepEqual([editor.snapshot.status, editor.snapshot.revision, editor.snapshot.savedNotes], ['saved', 2, 'final']);
  editor.dispose();
});

test('a save in flight cannot overwrite newer edits; the latest edit is saved next', async () => {
  const pending = []; const calls = [];
  const editor = new NotesEditor({ notes: 'old', revision: 1,
    save: (revision, notes) => { calls.push([revision, notes]); return new Promise(resolve => pending.push(resolve)); },
    onState() {}, });
  editor.change('first', true);
  editor.change('second');
  assert.deepEqual(calls, [[1, 'first']]);
  pending[0]({ notes: 'first', revision: 2 }); await tick();
  assert.equal(editor.snapshot.notes, 'second');
  assert.deepEqual(calls, [[1, 'first'], [2, 'second']]);
  pending[1]({ notes: 'second', revision: 3 }); await tick();
  assert.deepEqual([editor.snapshot.status, editor.snapshot.revision], ['saved', 3]);
  editor.dispose();
});

test('failed saves retain text and can be retried without resetting the revision', async () => {
  let attempts = 0; const states = [];
  const editor = new NotesEditor({ notes: 'old', revision: 4,
    save: async (revision, notes) => { attempts++; if (attempts === 1) throw Error('offline'); return { revision: revision + 1, notes }; },
    onState: state => states.push(state.status), });
  editor.change('new', true);
  assert.equal(await editor.flush(), false);
  assert.deepEqual([editor.snapshot.status, editor.snapshot.notes, editor.snapshot.revision], ['error', 'new', 4]);
  assert.equal(await editor.flush(), true);
  assert.deepEqual([editor.snapshot.status, editor.snapshot.revision, attempts], ['saved', 5, 2]);
  assert.ok(states.includes('saving') && states.includes('error'));
  editor.dispose();
});

test('Markdown import reads UTF-8, strips BOM and rejects invalid or oversized content', async () => {
  assert.equal(await readMarkdownNotes(file('notes.MD', bytes('\uFEFF# XORA\nNew release'))), '# XORA\nNew release');
  await assert.rejects(readMarkdownNotes(file('notes.txt', bytes('text'))), error => error instanceof MarkdownImportError && error.code === 'type');
  await assert.rejects(readMarkdownNotes(file('notes.md', Uint8Array.of(0xff))), error => error.code === 'encoding');
  await assert.rejects(readMarkdownNotes(file('notes.md', bytes('  \n'))), error => error.code === 'empty');
  await assert.rejects(readMarkdownNotes(file('notes.md', bytes('x'.repeat(10001)))), error => error.code === 'length');
  await assert.rejects(readMarkdownNotes(file('notes.md', bytes('x'.repeat(65537)))), error => error.code === 'size');
});

test('Markdown replaces existing notes and starts an immediate save', async () => {
  const calls = [];
  const editor = new NotesEditor({ notes: 'Previous notes', revision: 7,
    save: async (revision, notes) => { calls.push([revision, notes]); return { revision: revision + 1, notes }; },
    onState() {}, });
  editor.change(await readMarkdownNotes(file('release.md', bytes('# XORA\nUpdated experience'))), true);
  assert.deepEqual(calls, [[7, '# XORA\nUpdated experience']]);
  assert.equal(await editor.flush(), true);
  assert.deepEqual([editor.snapshot.notes, editor.snapshot.status], ['# XORA\nUpdated experience', 'saved']);
  editor.dispose();
});
