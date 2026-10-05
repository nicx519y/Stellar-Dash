"use client";
import { createContext, useContext, useEffect, useLayoutEffect, useRef, useState, type ReactNode } from 'react';
import { Box, Button, Flex, Icon, Popover, Portal, Spinner, Text } from '@chakra-ui/react';
import { LuSunDim, LuActivity, LuSparkles, LuWaves, LuTarget, LuCloudSunRain, LuAudioLines, LuDownload, LuTrash2 } from 'react-icons/lu';
import { keyframes } from '@emotion/react';
import { TbMeteorFilled } from 'react-icons/tb';
import { useLanguage } from '@/contexts/language-context';
import { useColorMode } from './ui/color-mode';
import { resourceKey, protectedResource, type InstalledResource, type ResourceItem, type ResourceRef } from '@/lib/resources';
import { keyEffectIds, ambientEffectIds } from '@/lib/hitbox-lighting-preview';

const EFFECT_CARD_HEIGHT = 76;
const ACTION_DELAY = 500;
const actionEnter = keyframes`from { opacity: 0; transform: translateY(12px); } to { opacity: 1; transform: translateY(0); }`;
const actionExit = keyframes`from { opacity: 1; transform: translateY(0); } to { opacity: 0; transform: translateY(12px); }`;

function useActionPopover() {
  const [openKey, setOpenKey] = useState<string | null>(null);
  const owner = useRef<string | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout>>();
  const clear = () => clearTimeout(timer.current);
  useEffect(() => () => clearTimeout(timer.current), []);
  return {
    openKey,
    enter(key: string, onOpen?: () => void) {
      clear();
      owner.current = key;
      setOpenKey(current => current === key ? current : null);
      timer.current = setTimeout(() => { setOpenKey(key); onOpen?.(); }, ACTION_DELAY);
    },
    hold(key: string) { if (owner.current === key) clear(); },
    leave(key: string) {
      if (owner.current !== key) return;
      clear();
      timer.current = setTimeout(() => { owner.current = null; setOpenKey(null); }, 180);
    },
    close(key: string) {
      if (owner.current !== key) return;
      clear(); owner.current = null; setOpenKey(null);
    },
  };
}
const ActionPopoverContext = createContext<ReturnType<typeof useActionPopover> | null>(null);
export function LightingActionPopoverProvider({children}: {children: ReactNode}) {
  const actions = useActionPopover();
  return <ActionPopoverContext.Provider value={actions}>{children}</ActionPopoverContext.Provider>;
}

export type LightingChoice = InstalledResource & { installed: boolean; server?: ResourceItem };
export function LightingEffectSelector({ items, selected, preview, ambient, disabled, loading, busy, progress, onSelect, onAction }: {
  items: LightingChoice[]; selected?: ResourceRef; preview?: ResourceRef; ambient: boolean; disabled: boolean;
  loading: boolean; busy: string; progress: number | null;
  onSelect: (item: LightingChoice) => void; onAction: (item: LightingChoice) => void;
}) {
  const {t, currentLanguage} = useLanguage(), {colorMode} = useColorMode(), zh = currentLanguage === 'zh';
  const container = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(0), [height, setHeight] = useState(EFFECT_CARD_HEIGHT), [visibleCount, setVisibleCount] = useState(0);
  const previousHeight = useRef(EFFECT_CARD_HEIGHT);
  useLayoutEffect(() => {
    const node = container.current;
    if (!node) return;
    const measure = () => setWidth(node.clientWidth);
    measure();
    const observer = new ResizeObserver(measure); observer.observe(node);
    return () => observer.disconnect();
  }, []);
  useEffect(() => {
    if (loading || !width) return;
    const columns = Math.max(1, Math.floor((width + 8) / 120));
    const rows = Math.max(1, Math.ceil(items.length / columns)), next = rows * EFFECT_CARD_HEIGHT + (rows - 1) * 8;
    const grow = next > previousHeight.current;
    previousHeight.current = next; setHeight(next);
    if (!grow || window.matchMedia('(prefers-reduced-motion: reduce)').matches) { setVisibleCount(items.length); return; }
    const timer = setTimeout(() => setVisibleCount(items.length), 240);
    return () => clearTimeout(timer);
  }, [items.length, loading, width]);
  const ids = ambient ? ambientEffectIds : keyEffectIds;
  const icons = ambient ? [LuSunDim, LuActivity, LuAudioLines, TbMeteorFilled] : [LuSunDim, LuActivity, LuSparkles, LuWaves, LuTarget, LuCloudSunRain];
  const labels = ambient
    ? [t.SETTINGS_LEDS_STATIC_LABEL, t.SETTINGS_LEDS_BREATHING_LABEL, t.SETTINGS_LEDS_QUAKE_LABEL, t.SETTINGS_LEDS_METEOR_LABEL]
    : [t.SETTINGS_LEDS_STATIC_LABEL, t.SETTINGS_LEDS_BREATHING_LABEL, t.SETTINGS_LEDS_STAR_LABEL, t.SETTINGS_LEDS_FLOWING_LABEL, t.SETTINGS_LEDS_RIPPLE_LABEL, t.SETTINGS_LEDS_TRANSFORM_LABEL];
  return <Box width="full" minW="0" data-testid={ambient ? 'ambient-effect-selector' : 'key-effect-selector'}>
    <Text fontSize="sm" mb="2">{ambient ? t.SETTINGS_AMBIENT_LIGHT_EFFECT_LABEL : t.SETTINGS_LEDS_EFFECT_STYLE_CHOICE}</Text>
    <Box ref={container} height={`${height}px`} transition="height 240ms ease" _motionReduce={{transition:'none'}} aria-busy={loading}>
      <Flex gap="8px" wrap="wrap" alignItems="flex-start" justifyContent="flex-start">
        {items.slice(0, visibleCount).map(item => {
          const key = resourceKey(item), index = ids.indexOf(item.resourceId), EffectIcon = icons[index] ?? LuSparkles;
          const label = `${labels[index] ?? item.name}${items.filter(other => other.resourceId === item.resourceId).length > 1 ? ` · v${item.revision}` : ''}`;
          return <EffectCard key={key} item={item} label={label} icon={<Icon fontSize="2xl"><EffectIcon/></Icon>}
            active={!!selected && resourceKey(selected) === key} previewing={!!preview && resourceKey(preview) === key && (!selected || resourceKey(selected) !== key)}
            disabled={disabled} dark={colorMode === 'dark'} zh={zh} busy={busy === key} progress={progress}
            onSelect={() => onSelect(item)} onAction={() => onAction(item)}/>;
        })}
      </Flex>
    </Box>
  </Box>;
}
function EffectCard({item, label, icon, active, previewing, disabled, dark, zh, busy, progress, onSelect, onAction}: {
  item:LightingChoice; label:string; icon:React.ReactNode; active:boolean; previewing:boolean; disabled:boolean; dark:boolean; zh:boolean; busy:boolean; progress:number|null; onSelect:()=>void; onAction:()=>void;
}) {
  const actions = useContext(ActionPopoverContext)!;
  const key = resourceKey(item), open = actions.openKey === key;
  const longPress = useRef(false), touch = useRef<{x:number;y:number}|null>(null);
  const actionButton = useRef<HTMLButtonElement>(null);
  const enter = () => actions.enter(key), leave = () => actions.leave(key);
  const protectedDefault = protectedResource(item);
  return <Popover.Root open={open} onOpenChange={e => {if (!e.open) actions.close(key);}} positioning={{placement:'top', gutter:8}} autoFocus={false}>
    {/* Position the hover action without trigger click/focus restoration rearming an old card. */}
    <Popover.Anchor asChild>
      <Button data-resource-key={resourceKey(item)} data-installed={item.installed} aria-label={label} aria-pressed={active} aria-disabled={disabled}
        width="112px" minW="112px" height={`${EFFECT_CARD_HEIGHT}px`} flexShrink={0} p="2" gap="1" flexDirection="column" position="relative"
        variant={item.installed ? (dark ? 'subtle' : 'solid') : 'outline'} colorPalette={active ? 'green' : 'gray'}
        borderWidth="1px" borderColor={active ? 'green.500' : previewing ? 'green.400' : 'border'}
        opacity={disabled ? 0.5 : 1}
        onMouseEnter={enter} onMouseLeave={leave} onFocus={e => {if (!touch.current && e.currentTarget.matches(':focus-visible')) enter();}} onBlur={leave}
        onKeyDown={e => {if (e.key === 'ArrowUp' && open) {e.preventDefault(); actionButton.current?.focus();}}}
        onPointerDown={e => { if (e.pointerType === 'touch') {touch.current = {x:e.clientX,y:e.clientY}; longPress.current = false; actions.enter(key, () => {longPress.current = true;});} }}
        onPointerMove={e => {if (touch.current && Math.hypot(e.clientX-touch.current.x,e.clientY-touch.current.y)>10) {actions.close(key); touch.current=null;}}}
        onPointerUp={e => {if (e.pointerType === 'touch') {actions.hold(key); touch.current=null;}}} onPointerCancel={() => {actions.close(key); touch.current=null;}}
        onClick={e => {e.preventDefault(); if (longPress.current) {longPress.current = false; return;} if (!disabled && !busy) onSelect();}}>
        {busy ? <Spinner size="sm"/> : icon}
        <Text fontSize="xs" whiteSpace="normal" lineClamp={2} lineHeight="16px">{busy && progress !== null ? `${progress}%` : label}</Text>
        {previewing && <Text position="absolute" top="0" right="1" fontSize="9px" color="green.500">{zh ? '预览' : 'Preview'}</Text>}
      </Button>
    </Popover.Anchor>
    <Portal><Popover.Positioner><Popover.Content width="auto" p="1" data-lighting-action={key}
      _open={{animation: `${actionEnter} 220ms ease-out both`, _motionReduce:{animationDuration:'0ms'}}}
      _closed={{animation: `${actionExit} 180ms ease-in both`, _motionReduce:{animationDuration:'0ms'}, pointerEvents:'none'}}
      onMouseEnter={() => actions.hold(key)} onMouseLeave={leave} onFocus={() => actions.hold(key)} onBlur={leave}>
      {item.installed && protectedDefault ? <Text fontSize="xs" px="2" py="1">{zh ? '保底静态灯效不可卸载' : 'The static fallback cannot be removed'}</Text> :
        <Button ref={actionButton} size="xs" variant="ghost" disabled={busy} onClick={e => {e.stopPropagation(); actions.close(key); onAction();}}><Icon aria-hidden="true" fontSize="sm">{item.installed ? <LuTrash2/> : <LuDownload/>}</Icon>{item.installed ? (zh ? '卸载' : 'Uninstall') : (zh ? '安装' : 'Install')}</Button>}
    </Popover.Content></Popover.Positioner></Portal>
  </Popover.Root>;
}
