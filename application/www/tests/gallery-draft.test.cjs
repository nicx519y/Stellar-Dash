const test = require('node:test');
const assert = require('node:assert/strict');
const { galleryDraftChanged, moveGalleryItem, moveGalleryItemToIndex, nearestGallerySlot, planGalleryPublish } = require('../lib/admin/gallery-draft.ts');

test('gallery draft stages additions, renames and deletions until publication', () => {
  const baseline = [{ id: 'a', title: 'One' }, { id: 'b', title: 'Two' }];
  const unchanged = planGalleryPublish(baseline, baseline.map(item => ({ ...item, kind: 'existing' })));
  assert.equal(galleryDraftChanged(unchanged), false);

  const draft = [
    { id: 'a', title: 'Renamed', kind: 'existing' },
    { id: 'local-1', title: 'New', kind: 'new' },
  ];
  const plan = planGalleryPublish(baseline, draft);
  assert.equal(galleryDraftChanged(plan), true);
  assert.deepEqual(plan.additions.map(item => item.id), ['local-1']);
  assert.deepEqual(plan.renames.map(item => item.id), ['a']);
  assert.deepEqual(plan.deletions, ['b']);
  assert.equal(galleryDraftChanged(planGalleryPublish(baseline, baseline.map(item => ({ ...item, kind: 'existing' })))), false);
});

test('deleting a newly added image cancels its upload without deleting server images', () => {
  const plan = planGalleryPublish([{ id: 'a', title: 'One' }], [{ id: 'a', title: 'One', kind: 'existing' }]);
  assert.equal(galleryDraftChanged(plan), false);
  assert.deepEqual(plan.deletions, []);
});

test('dragging images changes publication order and dragging back clears the draft', () => {
  const baseline = [{ id: 'a', title: 'One' }, { id: 'b', title: 'Two' }, { id: 'c', title: 'Three' }];
  const draft = baseline.map(item => ({ ...item, kind: 'existing' }));
  const reordered = moveGalleryItem(draft, 'c', 'a', 'before');
  assert.deepEqual(reordered.map(item => item.id), ['c', 'a', 'b']);
  const plan = planGalleryPublish(baseline, reordered);
  assert.equal(plan.orderChanged, true);
  assert.equal(galleryDraftChanged(plan), true);
  assert.deepEqual(plan.renames, []);
  const restored = moveGalleryItem(reordered, 'c', 'b', 'after');
  assert.deepEqual(restored.map(item => item.id), ['a', 'b', 'c']);
  assert.equal(galleryDraftChanged(planGalleryPublish(baseline, restored)), false);
});

test('drag preview chooses the nearest center and shifts later images into preview slots', () => {
  const cards = ['a', 'b', 'c', 'd'].map(id => ({ id }));
  const slots = [{ x: 100, y: 100 }, { x: 300, y: 100 }, { x: 100, y: 300 }, { x: 300, y: 300 }];
  assert.equal(nearestGallerySlot({ x: 110, y: 290 }, slots), 2);
  assert.deepEqual(moveGalleryItemToIndex(cards, 'a', 2).map(item => item.id), ['b', 'c', 'a', 'd']);
  assert.deepEqual(moveGalleryItemToIndex(cards, 'd', 0).map(item => item.id), ['d', 'a', 'b', 'c']);
});
