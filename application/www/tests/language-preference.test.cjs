const test = require('node:test');
const assert = require('node:assert/strict');
const { readLanguagePreference, saveLanguagePreference } = require('../lib/language-preference.ts');

test('saved language is read without overwriting the preference during initialization', () => {
  const writes = [];
  global.localStorage = { getItem: () => 'zh', setItem: (...args) => writes.push(args) };
  assert.equal(readLanguagePreference(), 'zh');
  assert.deepEqual(writes, []);
});

test('explicit language changes persist across later reads', () => {
  const values = new Map();
  global.localStorage = { getItem: key => values.get(key) ?? null, setItem: (key, value) => values.set(key, value) };
  assert.equal(readLanguagePreference(), 'en');
  saveLanguagePreference('zh');
  assert.equal(values.get('preferred_language'), 'zh');
  assert.equal(readLanguagePreference(), 'zh');
  saveLanguagePreference('en');
  assert.equal(readLanguagePreference(), 'en');
});

test('invalid or unavailable browser storage does not prevent the UI from starting', () => {
  global.localStorage = { getItem: () => 'invalid' };
  assert.equal(readLanguagePreference(), 'en');
  global.localStorage = {
    getItem: () => { throw new Error('Storage blocked'); },
    setItem: () => { throw new Error('Storage blocked'); },
  };
  assert.equal(readLanguagePreference(), 'en');
  assert.doesNotThrow(() => saveLanguagePreference('zh'));
  delete global.localStorage;
  assert.equal(readLanguagePreference(), 'en');
});
