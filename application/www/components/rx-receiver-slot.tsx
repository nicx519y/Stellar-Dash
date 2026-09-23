'use client';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Badge, Box, Button, Flex, HStack, Icon, SimpleGrid, Text, VStack } from '@chakra-ui/react';
import { LuCheck, LuUsb } from 'react-icons/lu';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { useLanguage } from '@/contexts/language-context';
import { BindingState, bindingHex, pairReceiver, paired, present, readBinding, canCancelBinding, cancelReceiverPairing } from '@/lib/device-transport/rf-binding';
import { ReceiverClient, ReceiverDevice, isReceiver, receiverFilters } from '@/lib/device-transport/rx-receiver-client';
import { getReceiverHid as receiverHid } from '@hbox/device-transport-runtime';

const copy = {
  zh: {
    title: 'Dongle', select: '选择接收器', change: '更换接收器', missing: '未检测到已授权接收器',
    permission: '首次使用需要选择接收器并授权。', reading: '正在读取接收器信息…',
    pair: '配对', pairing: '正在配对…', paired: '已配对', unpaired: '未与此 HBox 配对',
    resume: '继续完成配对', cancel: '取消未完成配对', incomplete: '配对未完成', address: '连接地址', id: '设备标识',
    unassigned: '未分配', replace: '将替换原有配对关系。', host: '请先连接 HBox。',
    hint: '绑定仅保存配对关系；使用无线时请拨动物理开关。', retry: '重新读取',
    multiple: '检测到多个接收器，请选择目标。', unsupported: '浏览器或固件不支持接收器配对；请使用 HTTPS Chrome/Edge 和支持配对的固件。',
    failed: '读取或配对失败，请重新读取设备状态。', disconnected: '接收器已断开。',
    busy: '设备正忙，请完成其他操作后重试。', conflict: '存在其他未完成的配对，请连接原来的 HBox 和接收器继续完成。',
    storage: '设备保存失败，请重新读取状态后重试。', timeout: '接收器未回复，请确认固件支持配对且未被其他程序占用。',
  },
  en: {
    title: 'Dongle', select: 'Select receiver', change: 'Change receiver', missing: 'No authorized receiver detected',
    permission: 'Select and authorize the receiver on first use.', reading: 'Reading receiver…',
    pair: 'Pair', pairing: 'Pairing…', paired: 'Paired', unpaired: 'Not paired with this HBox',
    resume: 'Resume pairing', cancel: 'Cancel pending pairing', incomplete: 'Pairing incomplete', address: 'Connection address', id: 'Device ID',
    unassigned: 'Unassigned', replace: 'This replaces the previous pairing.', host: 'Connect HBox first.',
    hint: 'Pairing only saves the binding. Use the physical switch to enable wireless.', retry: 'Read again',
    multiple: 'Multiple receivers detected. Select a receiver.', unsupported: 'Browser or firmware does not support pairing. Use HTTPS Chrome/Edge and pairing-capable firmware.',
    failed: 'Read or pairing failed. Read device state again.', disconnected: 'Receiver disconnected.',
    busy: 'Device is busy. Finish the other operation first.', conflict: 'Another pairing is incomplete. Reconnect its original HBox and receiver to finish.',
    storage: 'Device could not save the binding. Read state before retrying.', timeout: 'Receiver did not reply. Check firmware support and whether another application is using it.',
  },
};

export function RxReceiverSlot({ disabled = false }: { disabled?: boolean }) {
  const { currentLanguage } = useLanguage(), t = copy[currentLanguage];
  const { deviceConnected, isLoading, deferredConfigSaving, rfBindingBusy, rfBindingRequest, runRfBindingOperation } = useGamepadConfig();
  const [client, setClient] = useState<ReceiverClient | null>(null);
  const [states, setStates] = useState<{ rx: BindingState; tx: BindingState } | null>(null);
  const [reading, setReading] = useState(false), [working, setWorking] = useState(false);
  const [error, setError] = useState(''), [multiple, setMultiple] = useState(false);
  const clientRef = useRef<ReceiverClient | null>(null), epoch = useRef(0), busy = useRef(false);
  const connected = useRef(deviceConnected); connected.current = deviceConnected;
  const blocked = disabled || isLoading || deferredConfigSaving || rfBindingBusy || !deviceConnected;
  const blockedRef = useRef(blocked); blockedRef.current = blocked;
  const attach = useCallback(async (device: ReceiverDevice) => {
    const turn = ++epoch.current;
    const old = clientRef.current; clientRef.current = null; setClient(null); setStates(null); setError('');
    if (old) await old.close().catch(() => {});
    if (turn !== epoch.current) return;
    const next = new ReceiverClient(device); clientRef.current = next;
    try { await next.open(); if (turn === epoch.current) setClient(next); else await next.close(); }
    catch (e) { if (turn === epoch.current) { clientRef.current = null; setError((e as Error).message); } }
  }, []);
  useEffect(() => {
    const hid = receiverHid(); let disposed = false;
    const lifecycleEpoch = epoch;
    if (!hid) { setError('BINDING_UNSUPPORTED'); return; }
    const discover = async () => {
      if (disposed || clientRef.current || busy.current) return;
      try {
        const devices = (await hid.getDevices()).filter(isReceiver);
        if (disposed || clientRef.current || busy.current) return;
        setMultiple(devices.length > 1);
        if (devices.length === 1) await attach(devices[0]);
      } catch (e) { if (!disposed) setError((e as Error).message); }
    };
    const onDisconnect = (event: Event) => {
      if ((event as Event & { device: ReceiverDevice }).device !== clientRef.current?.device) return;
      ++epoch.current; void clientRef.current?.close().catch(() => {}); clientRef.current = null;
      setClient(null); setStates(null); setError('BINDING_DISCONNECTED');
    };
    const onConnect = () => { void discover(); };
    hid.addEventListener('connect', onConnect); hid.addEventListener('disconnect', onDisconnect); void discover();
    return () => {
      disposed = true; ++lifecycleEpoch.current; hid.removeEventListener('connect', onConnect); hid.removeEventListener('disconnect', onDisconnect);
      void clientRef.current?.close().catch(() => {}); clientRef.current = null;
    };
  }, [attach]);
  useEffect(() => { ++epoch.current; setStates(null); }, [deviceConnected]);
  const refresh = useCallback(async () => {
    if (!client || !connected.current || busy.current || blockedRef.current) return;
    const turn = epoch.current; busy.current = true; setReading(true);
    try {
      const rx = await readBinding(client.request), tx = await readBinding(rfBindingRequest);
      if (turn === epoch.current) { setStates({ rx, tx }); setError(''); }
    } catch (e) { if (turn === epoch.current) { setStates(null); setError((e as Error).message); } }
    finally { busy.current = false; setReading(false); }
  }, [client, rfBindingRequest]);
  useEffect(() => {
    void refresh(); const timer = setInterval(() => { if (!document.hidden) void refresh(); }, 2000);
    return () => clearInterval(timer);
  }, [refresh, deviceConnected]);
  const select = async () => {
    if (busy.current) return;
    try {
      const hid = receiverHid(); if (!hid) throw new Error('BINDING_UNSUPPORTED');
      const devices = (await hid.requestDevice({ filters: receiverFilters })).filter(isReceiver);
      if (devices.length === 1) { setMultiple(false); await attach(devices[0]); }
      else if (devices.length > 1) { setMultiple(true); setError('BINDING_CONFLICT'); }
    } catch (e) { if ((e as Error).name !== 'NotFoundError') setError((e as Error).message); }
  };
  const pair = async (cancel = false) => {
    if (!client || busy.current || blockedRef.current || !states || error) return;
    const turn = epoch.current; busy.current = true; setWorking(true); setError('');
    const alive = () => { if (turn !== epoch.current || !connected.current || clientRef.current !== client) throw new Error('BINDING_DISCONNECTED'); };
    try {
      const words = crypto.getRandomValues(new Uint32Array(1));
      const result = await runRfBindingOperation(() => cancel
        ? cancelReceiverPairing(client.request, rfBindingRequest, alive)
        : pairReceiver(client.request, rfBindingRequest, words[0] || 1, alive));
      alive(); setStates(result);
    } catch (e) { if (turn === epoch.current) { setStates(null); setError((e as Error).message); } }
    finally { busy.current = false; setWorking(false); }
  };
  const done = !!states && paired(states.rx, states.tx);
  const pending = !!states && (present(states.rx.pending) || present(states.tx.pending));
  const replacing = !!states && !done && (present(states.rx.active) || present(states.tx.active));
  const errorText = !error ? '' : error.includes('UNSUPPORTED') || error.includes('STATUS_4') ? t.unsupported :
    error.includes('CONFLICT') || error.includes('STATUS_2') || error.includes('CHANGED') ? t.conflict :
    error.includes('BUSY') || error.includes('STATUS_5') ? t.busy : error.includes('DISCONNECTED') ? t.disconnected :
    error.includes('STATUS_3') ? t.storage : error.includes('TIMEOUT') ? t.timeout : t.failed;
  return (
    <Box as="section" aria-label={t.title} borderWidth="1px" borderColor="border.subtle" borderRadius="lg" bg="bg.subtle" p={4} w="100%">
      <VStack align="stretch" gap={4}>
        <Flex align="start" justify="space-between" gap={3} wrap="wrap">
          <HStack align="start" gap={3} flex="1" minW="200px">
            <Box p={2} borderRadius="md" bg="bg.muted" color="fg.muted">
              <Icon fontSize="20px"><LuUsb /></Icon>
            </Box>
            <Box minW={0}>
              <Text fontSize="xs" color="fg.muted" mb={1}>{t.title}</Text>
              <Text fontSize="sm" fontWeight="medium" lineHeight="1.5" overflowWrap="anywhere">
                {client?.device.productName || (multiple ? t.multiple : t.missing)}
              </Text>
            </Box>
          </HStack>
          {states && !error && <Badge size="sm" variant="subtle" colorPalette={working || pending ? 'orange' : done ? 'green' : 'gray'} role="status" textTransform="none" letterSpacing="normal">
            {done && !working && <LuCheck />}
            {working ? t.pairing : done ? t.paired : pending ? t.incomplete : t.unpaired}
          </Badge>}
        </Flex>

        {states && <SimpleGrid as="dl" columns={{ base: 1, sm: 2 }} gap={3}>
          <Box>
            <Text as="dt" fontSize="xs" color="fg.muted" mb={1}>{t.id}</Text>
            <Text as="dd" fontSize="sm" fontFamily="mono" letterSpacing="normal">{bindingHex(states.rx.active.localId)}</Text>
          </Box>
          <Box>
            <Text as="dt" fontSize="xs" color="fg.muted" mb={1}>{t.address}</Text>
            <Text as="dd" fontSize="sm" fontFamily={present(states.rx.active) ? 'mono' : 'inherit'} letterSpacing="normal">
              {present(states.rx.active) ? bindingHex(states.rx.active.address) : t.unassigned}
            </Text>
          </Box>
        </SimpleGrid>}

        <VStack align="stretch" gap={2} aria-live="polite">
          {!deviceConnected && <Text fontSize="xs" color="fg.muted">{t.host}</Text>}
          {reading && !states && <Text fontSize="xs" color="fg.muted">{t.reading}</Text>}
          {errorText && <Text fontSize="xs" lineHeight="1.6" color="fg.error" role="alert">{errorText}</Text>}
          {replacing && <Text fontSize="xs" color="fg.muted">{t.replace}</Text>}
          <Flex gap={2} wrap="wrap">
            {!done && states && <Button size="sm" colorPalette="green" disabled={blocked || reading || working || !!error} onClick={() => void pair()}>{working ? t.pairing : pending ? t.resume : t.pair}</Button>}
            <Button size="sm" variant={client ? 'outline' : 'solid'} colorPalette={client ? 'gray' : 'green'} disabled={working || reading} onClick={() => void select()}>{client ? t.change : t.select}</Button>
            {error && client && <Button size="sm" variant="outline" disabled={blocked || reading || working} onClick={() => void refresh()}>{t.retry}</Button>}
            {states && canCancelBinding(states.rx, states.tx) && <Button size="sm" variant="ghost" disabled={blocked || reading || working || !!error} onClick={() => void pair(true)}>{t.cancel}</Button>}
          </Flex>
        </VStack>
        <Text fontSize="xs" color="fg.muted" lineHeight="1.6">{client ? t.hint : t.permission}</Text>
      </VStack>
    </Box>
  );
}
