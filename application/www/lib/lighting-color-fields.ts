import type { ResourceSource } from './resources';

export type LightingColorRole = 'solid' | 'base' | 'peak' | 'wave' | 'meteor' | 'pressed' | 'background1' | 'background2' | 'slot';
export type LightingColorField = { index: number; role: LightingColorRole };

export function lightingColorFields(source: ResourceSource | null): LightingColorField[] {
  if (!source || source.type === 'switch-mapping') return [];
  const layers = source.payload.layers;
  const used = new Set<number>();
  for (const layer of layers) {
    used.add(layer.from);
    // A constant layer always renders its starting color.
    if (layer.signal !== 'constant') used.add(layer.to);
  }
  const fields: LightingColorField[] = [...used].sort((a, b) => a - b)
    .map(index => ({ index, role: 'slot' }));
  const layer = layers[0];
  const pressed = layers[1];
  // Factory key effects draw a background, then replace held keys with a
  // separate color. Recognize the structure rather than resource IDs.
  if (source.type === 'key-lighting' && layers.length === 2 &&
      layer.blend === 'replace' && layer.mask === 0 &&
      pressed.blend === 'replace' && pressed.mask === 1 &&
      pressed.signal === 'pressed' && pressed.from === pressed.to &&
      pressed.from !== layer.from &&
      (layer.signal === 'constant' || pressed.from !== layer.to)) {
    return fields.map(field => ({ ...field, role: field.index === pressed.from ? 'pressed'
      : layer.signal === 'constant' || layer.from === layer.to ? 'base'
      : field.index === layer.from ? 'background1' : 'background2' }));
  }
  if (layers.length !== 1 || layer.blend !== 'replace' || layer.mask !== 0) return fields;
  if (fields.length === 1) return fields.map(field => ({ ...field, role: 'solid' }));
  const effectRole: Partial<Record<string, LightingColorRole>> = {
    sine: 'peak', 'center-wave': 'wave', 'ring-tail': 'meteor',
  };
  const role = effectRole[layer.signal];
  return role ? fields.map(field => ({ ...field, role: field.index === layer.from ? 'base' : role })) : fields;
}
