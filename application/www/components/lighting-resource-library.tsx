"use client";
import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from 'react';
import { Box, Button, Text } from '@chakra-ui/react';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { useLanguage } from '@/contexts/language-context';
import { resourceJSON, resourceKey, fromHex, toHex, verifyResource, installLighting, protectedResource, type ResourceInventory, type ResourceItem, type ResourceSource, type ResourceRef } from '@/lib/resources';
import { LightingActionPopoverProvider, LightingEffectSelector, type LightingChoice } from './lighting-effect-selector';
import { keyEffectIds, ambientEffectIds } from '@/lib/hitbox-lighting-preview';
import { ConfirmDialog, type ConfirmDialogTone } from './dialog-confirm';

type LightType = 'key-lighting' | 'ambient-lighting';
type Modal = {message:string; confirm:boolean; tone:ConfirmDialogTone; resolve:(value:boolean)=>void};
function useLibrary() {
  const ctx = useGamepadConfig(), {currentLanguage} = useLanguage(), zh = currentLanguage === 'zh';
  const api = useRef(ctx); api.current = ctx;
  const [inventory, setInventory] = useState<ResourceInventory|null>(null), [catalog, setCatalog] = useState<ResourceItem[]>([]);
  const [loading, setLoading] = useState(true), [serverError, setServerError] = useState(''), [error, setError] = useState('');
  const [busy, setBusy] = useState(''), [progress, setProgress] = useState<number|null>(null), [modal, setModal] = useState<Modal|null>(null);
  const [previews, setPreviews] = useState<Partial<Record<LightType, ResourceSource>>>({});
  const generation = useRef(0), selection = useRef(0), locked = useRef(false), cache = useRef(new Map<string, ResourceSource>());
  const modalRef = useRef(modal); modalRef.current = modal;
  const alive = (g:number) => g === generation.current && api.current.deviceConnected;
  const readSource = useCallback(async (ref:ResourceRef) => {
    const g = generation.current;
    const key = resourceKey(ref), cached = cache.current.get(key);
    if (cached) return cached;
    const reply = await api.current.resourceCommand('resources_get', ref);
    const source = await verifyResource(fromHex(reply.hex));
    if (resourceKey(source) !== key) throw Error('Resource identity mismatch');
    if (g === generation.current) cache.current.set(key, source); return source;
  }, []);
  const loadCatalog = useCallback(async (g = generation.current) => {
    try {
      const result = await resourceJSON<{items:ResourceItem[]}>(await api.current.fetchDeviceAuthorizedResource('/api/resources', {signal:AbortSignal.timeout(8000)}));
      if (g === generation.current) { setCatalog(result.items.filter(item => item.type !== 'switch-mapping')); setServerError(''); }
    } catch { if (g === generation.current) setServerError('server'); }
  }, []);
  useEffect(() => {
    const g = ++generation.current;
    cache.current.clear(); setInventory(null); setCatalog([]); setPreviews({}); setError(''); setServerError('');
    setBusy(''); locked.current = false; setProgress(null);
    modalRef.current?.resolve(false); setModal(null);
    if (!ctx.deviceConnected || !ctx.dataIsReady) {setLoading(false); return;}
    setLoading(true);
    void Promise.allSettled([
      api.current.resourceCommand('resources_list').then(reply => {
        const inv = reply as unknown as ResourceInventory;
        if (inv.schemaVersion !== 1 || inv.engineVersion !== 1) throw Error('Unsupported resource version');
        if (g === generation.current) setInventory(inv);
      }).catch(e => {if (g === generation.current) setError(String(e));}),
      loadCatalog(g),
    ]).then(() => {if (g === generation.current) setLoading(false);});
    return () => {generation.current = g + 1; modalRef.current?.resolve(false);};
  }, [ctx.deviceConnected, ctx.dataIsReady, loadCatalog]);
  useEffect(() => {if (inventory) api.current.syncLightingResourceReferences(inventory.profiles);}, [inventory]);
  // Profile selection resets only the preview. The directory and installed cards stay mounted.
  useEffect(() => {
    const s = ++selection.current, g = generation.current;
    setPreviews({});
    const profile = inventory?.profiles.find(p => p.profileId === ctx.defaultProfile.id);
    if (!profile) return;
    for (const [type, ref] of [['key-lighting',profile.keys],['ambient-lighting',profile.ambient]] as const)
      void readSource(ref).then(source => {if (g === generation.current && s === selection.current) setPreviews(old => ({...old,[type]:source}));})
        .catch(e => {if (g === generation.current && s === selection.current) setError(String(e));});
  // Inventory updates from installation do not reset a deliberately chosen preview.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ctx.defaultProfile.id, !!inventory, readSource]);
  const ask = (message:string, confirm = false, tone:ConfirmDialogTone = 'info') => new Promise<boolean>(resolve => setModal({message,confirm,tone,resolve}));
  const describeError = (value:unknown) => {
    const text = String(value instanceof Error ? value.message : value);
    if (text.includes('RESOURCE_COUNT_LIMIT')) return zh ? '灯效数量已达上限，请先卸载一些灯效再安装。' : 'Effect limit reached. Uninstall an effect before installing another.';
    if (text.includes('RESOURCE_STORAGE_FULL')) return zh ? '设备存储空间不足，请先卸载一些灯效。' : 'Not enough device storage. Uninstall an effect first.';
    if (text.includes('RESOURCE_CONFIG_SAVE_FAILED')) return zh ? 'Profile 保存失败，灯效未卸载。' : 'Profile save failed. The effect was not removed.';
    if (text.includes('RESOURCE_REMOVE_FAILED')) return zh ? 'Profile 已切换到静态灯效，但资源删除失败，请重试。' : 'Profiles now use static lighting, but removal failed. Please retry.';
    return text;
  };
  const select = async (item:LightingChoice) => {
    if (locked.current || !ctx.deviceConnected) return;
    const g = generation.current, s = ++selection.current, profileId = api.current.defaultProfile.id;
    if (!item.installed) {
      if (item.server) setPreviews(old => ({...old,[item.type]:item.server!.source}));
      return;
    }
    locked.current = true; setBusy(resourceKey(item)); setProgress(null); setError('');
    try {
      const source = await readSource(item);
      if (!alive(g) || s !== selection.current || profileId !== api.current.defaultProfile.id) return;
      setPreviews(old => ({...old,[item.type]:source}));
      const zone = item.type === 'key-lighting' ? 'keys' : 'ambient';
      const current = inventory?.profiles.find(p => p.profileId === profileId)?.[zone];
      if (current && resourceKey(current) === resourceKey(item)) return;
      await api.current.resourceTransaction(async send => {
        if (!alive(g) || profileId !== api.current.defaultProfile.id) throw Error('Profile or connection changed');
        const result = await send('resources_apply', {resourceId:item.resourceId,revision:item.revision,profileId});
        if (!result.applied || !result.runtimeReloaded) throw Error(zh ? '设备未确认应用，请重新连接核对。' : 'Device did not confirm application. Reconnect to verify.');
      });
      if (alive(g)) setInventory(old => old && ({...old,profiles:old.profiles.map(p => p.profileId === profileId ? {...p,[zone]:{resourceId:item.resourceId,revision:item.revision}} : p)}));
    } catch(e) {if (alive(g)) setError(describeError(e));}
    finally {if (g === generation.current) {locked.current = false; setBusy('');}}
  };
  const action = async (item:LightingChoice) => {
    if (locked.current || !ctx.deviceConnected) return;
    if (!inventory?.removeWithFallback || !inventory.limits?.[item.type]) {await ask(zh ? '请更新设备固件后安装或卸载灯效。' : 'Update device firmware to install or uninstall effects.'); return;}
    if (!inventory.storageReady) {await ask(zh ? '设备资源存储不可用，请重新连接核对。' : 'Device resource storage is unavailable. Reconnect to verify.', false, 'error'); return;}
    if (item.installed && protectedResource(item)) return;
    const g = generation.current;
    let reconcile = false;
    locked.current = true;
    try {
      if (!item.installed && inventory.items.filter(r => r.type === item.type).length >= inventory.limits[item.type]!) {
        await ask(zh ? '灯效数量已达上限（8 套），要继续安装，请先卸载一些灯效。' : 'The 8-effect limit has been reached. Uninstall an effect before installing another.', false, 'warning'); return;
      }
      if (item.installed) {
        const zone = item.type === 'key-lighting' ? 'keys' : 'ambient';
        const affected = inventory.profiles.filter(p => resourceKey(p[zone]) === resourceKey(item));
        const names = affected.map(p => ctx.profileList.items.find(profile => profile.id === p.profileId)?.name || p.profileId).join(', ');
        if (!await ask((zh ? `卸载“${item.name}”？` : `Uninstall “${item.name}”?`) + (names ? (zh ? `\n以下 Profile 将切换为静态灯效：${names}` : `\nThese profiles will use static lighting: ${names}`) : ''), true, 'danger')) return;
      }
      if (!alive(g)) return;
      setBusy(resourceKey(item)); setProgress(null); setError('');
      if (item.installed) {
        reconcile = true;
        const result = await api.current.resourceTransaction(send => send('resources_remove',{resourceId:item.resourceId,revision:item.revision,resetReferences:true}));
        if (!alive(g)) return;
        const zone = item.type === 'key-lighting' ? 'keys' : 'ambient', affected = (result.affectedProfiles || []) as string[], fallback = result.fallback as ResourceRef;
        setInventory(old => old && ({...old, items:result.removed ? old.items.filter(r => resourceKey(r) !== resourceKey(item)) : old.items,
          profiles:result.configurationSaved && fallback ? old.profiles.map(p => affected.includes(p.profileId) ? {...p,[zone]:fallback} : p) : old.profiles}));
        if (result.configurationSaved && affected.includes(api.current.defaultProfile.id) && fallback) {
          const profileId = api.current.defaultProfile.id;
          const source = await readSource(fallback);
          if (alive(g) && api.current.defaultProfile.id === profileId) setPreviews(old => ({...old,[item.type]:source}));
        }
        if (!result.removed || result.errorCode) throw Error(String(result.errorCode || 'RESOURCE_REMOVE_FAILED'));
      } else if (item.server) {
        const response = await api.current.fetchDeviceAuthorizedResource(`/api/resources/${encodeURIComponent(item.server.catalogId)}/revisions/${item.revision}/download`, {signal:AbortSignal.timeout(20000)});
        if (!response.ok) throw Error(`Download HTTP ${response.status}`);
        const bytes = new Uint8Array(await response.arrayBuffer());
        if (toHex(bytes.subarray(32,64)) !== item.server.sha256) throw Error('Resource catalog digest mismatch');
        if (!alive(g)) return;
        reconcile = true;
        await api.current.resourceTransaction(async send => {
          if (!alive(g)) throw Error('Connection changed');
          await installLighting(send,item,bytes,n => {if (alive(g)) setProgress(n);});
        });
        if (alive(g)) setInventory(old => old && ({...old,items:[...old.items.filter(r => resourceKey(r) !== resourceKey(item)),{resourceId:item.resourceId,revision:item.revision,name:item.name,type:item.type}],used:old.used+bytes.length}));
      }
    } catch(e) {
      if (alive(g)) {setError(describeError(e)); if (/RESOURCE_COUNT_LIMIT|RESOURCE_STORAGE_FULL/.test(String(e))) await ask(describeError(e), false, 'warning');}
    } finally {
      // A failed acknowledgement can still follow a committed bank. Reconcile
      // device facts without clearing cards or refetching the server directory.
      if (reconcile && alive(g)) {
        try {
          const reply = await api.current.resourceCommand('resources_list');
          if (alive(g)) setInventory(reply as unknown as ResourceInventory);
        } catch {if (alive(g)) setError(zh ? '无法核对设备库存，请重新连接。' : 'Unable to verify device inventory. Reconnect to verify.');}
      }
      if (g === generation.current) {locked.current = false; setBusy(''); setProgress(null);}
    }
  };
  return {inventory,catalog,loading,serverError,error,busy,progress,previews,select,action,loadCatalog,modal,setModal,zh};
}
const LibraryContext = createContext<ReturnType<typeof useLibrary>|null>(null);
export function LightingResourceProvider({children}:{children:ReactNode}) {
  const library = useLibrary(), {modal,setModal,zh} = library;
  const close = (value:boolean) => {modal?.resolve(value); setModal(null);};
  return <LibraryContext.Provider value={library}><LightingActionPopoverProvider>{children}</LightingActionPopoverProvider>
    <ConfirmDialog open={!!modal} upperMiddle title={zh ? '灯效资源' : 'Lighting resources'}
      message={modal?.message ?? ''} tone={modal?.tone} showCancel={!!modal?.confirm}
      onCancel={() => close(false)} onConfirm={() => close(true)} />
  </LibraryContext.Provider>;
}
export function LightingResourceLibrary({type,onSource,selectionDisabled=false}:{type:LightType;onSource:(source:ResourceSource|null)=>void;selectionDisabled?:boolean}) {
  const lib = useContext(LibraryContext)!, ctx = useGamepadConfig();
  const source = lib.previews[type];
  useEffect(() => {onSource(source || null);}, [source,onSource]);
  const choices = new Map<string,LightingChoice>();
  for (const item of lib.catalog.filter(r => r.type === type)) choices.set(resourceKey(item),{...item,installed:false,server:item});
  for (const item of lib.inventory?.items.filter(r => r.type === type) || []) choices.set(resourceKey(item),{...choices.get(resourceKey(item)),...item,installed:true});
  const ids = type === 'key-lighting' ? keyEffectIds : ambientEffectIds;
  const order = (item:LightingChoice) => {const i = ids.indexOf(item.resourceId); return i < 0 ? ids.length : i;};
  const items = [...choices.values()].sort((a,b) => order(a)-order(b) || a.resourceId.localeCompare(b.resourceId) || a.revision-b.revision);
  const profile = lib.inventory?.profiles.find(p => p.profileId === ctx.defaultProfile.id);
  return <Box width="full" minW="0">
    <LightingEffectSelector items={items} selected={type === 'key-lighting' ? profile?.keys : profile?.ambient} preview={source}
      ambient={type === 'ambient-lighting'} disabled={selectionDisabled || !!lib.busy || !ctx.deviceConnected} loading={lib.loading}
      busy={lib.busy} progress={lib.progress} onSelect={item => void lib.select(item)} onAction={item => void lib.action(item)}/>
    {lib.serverError && <Text fontSize="xs" mt="2" color="fg.muted">{lib.zh ? '服务器暂不可用，仍可使用已安装灯效。' : 'Server unavailable. Installed effects remain available.'}<Button size="xs" variant="ghost" onClick={() => void lib.loadCatalog()}>{lib.zh ? '重试' : 'Retry'}</Button></Text>}
    {lib.error && <Text role="alert" fontSize="xs" color="red.500" mt="2">{lib.error}</Text>}
  </Box>;
}
