"use client";
import { useCallback, useEffect, useRef, useState } from "react";
import {
  Box,
  Button,
  Drawer,
  Portal,
  Flex,
  HStack,
  Text,
  Stack,
  Skeleton,
  Badge,
} from "@chakra-ui/react";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { ResourcePreview } from "./resource-preview";
import {
  toHex,
  fromHex,
  installLighting,
  resourceJSON,
  resourceKey,
  verifyResource,
  type ResourceType,
  type ResourceItem,
  type ResourceInventory,
  type InstalledResource,
  type ResourceSource,
} from "@/lib/resources";
export function ResourceLibrary({
  type,
  onSource,
  lightingSelector = false,
}: {
  type: ResourceType;
  onSource?: (source: ResourceSource | null) => void;
  lightingSelector?: boolean;
}) {
  const ctx = useGamepadConfig(),
    { currentLanguage } = useLanguage(),
    zh = currentLanguage === "zh",
    axis = type === "switch-mapping";
  const [inventory, setInventory] = useState<ResourceInventory | null>(null),
    [items, setItems] = useState<ResourceItem[]>([]),
    [open, setOpen] = useState(false),
    [tab, setTab] = useState("device"),
    [loading, setLoading] = useState(true),
    [error, setError] = useState(""),
    [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(""),
    [unsupported, setUnsupported] = useState(false);
  const [deviceSources, setDeviceSources] = useState<Record<string, ResourceSource>>({});
  const deviceSourcesRef = useRef(deviceSources);
  deviceSourcesRef.current = deviceSources;
  const apiRef = useRef(ctx);
  apiRef.current = ctx;
  const onSourceRef = useRef(onSource);
  onSourceRef.current = onSource;
  const generation = useRef(0);
  const refresh = useCallback(async () => {
    const g = ++generation.current;
    setLoading(true);
    setError("");
    onSourceRef.current?.(null);
    const api = apiRef.current;
    const results = await Promise.allSettled([
      axis ? api.fetchMappingList() : api.resourceCommand("resources_list"),
      api
        .fetchDeviceAuthorizedResource(`/api/resources?type=${type}`, {
          signal: AbortSignal.timeout(8000),
        })
        .then(resourceJSON<{ items: ResourceItem[] }>),
    ]);
    if (g !== generation.current) return;
    if (results[1].status === "fulfilled") setItems(results[1].value.items);
    else
      setError(
        zh
          ? "服务器资源暂不可用，仍可使用设备已安装资源。"
          : "Server unavailable. Installed resources remain available.",
      );
    if (results[0].status === "fulfilled") {
      setUnsupported(false);
      if (!axis) {
        const inv = results[0].value as ResourceInventory;
        if (inv.schemaVersion !== 1 || inv.engineVersion !== 1) {
          setUnsupported(true);
          setInventory(null);
          setError(
            zh
              ? "设备资源版本不兼容，请更新固件。"
              : "Unsupported resource version. Update the firmware.",
          );
          setLoading(false);
          return;
        }
        setInventory(inv);
        const p = inv.profiles.find(
          (p) => p.profileId === api.defaultProfile.id,
        );
        const ref = type === "key-lighting" ? p?.keys : p?.ambient;
        if (ref)
          try {
            const result = await api.resourceCommand("resources_get", ref);
            const source = await verifyResource(fromHex(result.hex));
            if (g === generation.current) onSourceRef.current?.(source);
          } catch (e) {
            if (g === generation.current) setError(String(e));
          }
      }
    } else {
      setUnsupported(true);
      setError(
        zh
          ? "设备不支持资源库或暂时无法读取，请检查连接与固件版本。"
          : "Resource library unavailable. Check connection and firmware.",
      );
    }
    if (g === generation.current) setLoading(false);
  }, [axis, type, zh]);
  useEffect(() => {
    if (ctx.deviceConnected && ctx.dataIsReady) void refresh();
    else {
      generation.current++;
      setInventory(null);
      setItems([]);
      setDeviceSources({});
      setLoading(false);
      onSourceRef.current?.(null);
    }
    return () => {
      generation.current++;
    };
  }, [ctx.deviceConnected, ctx.dataIsReady, ctx.defaultProfile.id, refresh]);
  useEffect(() => {
    if (!open || tab !== "device" || !inventory || axis || lightingSelector) return;
    let stopped = false;
    void (async () => {
      for (const item of inventory.items.filter((r) => r.type === type)) {
        const key = resourceKey(item);
        if (deviceSourcesRef.current[key]) continue;
        try {
          const result = await apiRef.current.resourceCommand(
            "resources_get",
            item,
          );
          const source = await verifyResource(fromHex(result.hex));
          if (stopped) return;
          setDeviceSources((old) => ({ ...old, [key]: source }));
        } catch {
          if (stopped) return;
        }
      }
    })();
    return () => {
      stopped = true;
    };
  }, [open, tab, inventory, axis, type, lightingSelector]);
  const profile = inventory?.profiles.find(
      (p) => p.profileId === ctx.defaultProfile.id,
    ),
    selected = type === "key-lighting" ? profile?.keys : profile?.ambient;
  const current = axis
    ? ctx.activeMapping?.name
    : inventory?.items.find(
        (r) => selected && resourceKey(r) === resourceKey(selected),
      )?.name;
  const installed = (item: ResourceItem) =>
    axis
      ? ctx.defaultMappingId === item.resourceId
      : inventory?.items.some((r) => resourceKey(r) === resourceKey(item));
  const referenced = (item: InstalledResource | ResourceItem) =>
    inventory?.profiles.some((p) =>
      [p.keys, p.ambient].some((r) => resourceKey(r) === resourceKey(item)),
    );
  const update = axis
    ? items.find(
        (r) =>
          r.resourceId !== ctx.defaultMappingId &&
          r.previousResourceIds?.includes(ctx.defaultMappingId),
      )
    : selected &&
      items.find(
        (r) =>
          r.resourceId === selected.resourceId &&
          r.revision > selected.revision,
      );
  const download = async (item: ResourceItem) => {
    const response = await ctx.fetchDeviceAuthorizedResource(
      `/api/resources/${encodeURIComponent(item.catalogId)}/revisions/${item.revision}/download`,
      { signal: AbortSignal.timeout(20000) },
    );
    if (!response.ok) throw new Error(`Download HTTP ${response.status}`);
    const bytes = new Uint8Array(await response.arrayBuffer()),
      source = await verifyResource(bytes);
    if (
      source.resourceId !== item.resourceId ||
      source.revision !== item.revision ||
      toHex(bytes.subarray(32, 64)) !== item.sha256
    )
      throw new Error("Resource catalog digest or identity mismatch");
    return bytes;
  };
  const perform = async (
    item: ResourceItem | InstalledResource,
    action: "download" | "apply" | "remove",
  ) => {
    setBusy(resourceKey(item));
    setNotice("");
    setError("");
    const profileId = ctx.defaultProfile.id;
    try {
      if (axis) {
        const full = item as ResourceItem;
        await ctx.installMappingResource(await download(full));
        await ctx.fetchCalibrationStatus();
        setNotice(
          zh
            ? "已安装并应用。请重新校准磁轴按键。"
            : "Installed and applied. Recalibrate the Hall keys.",
        );
      } else if (action === "remove") {
        await ctx.resourceTransaction((send) =>
          send("resources_remove", {
            resourceId: item.resourceId,
            revision: item.revision,
          }),
        );
      } else {
        await ctx.resourceTransaction(async (send) => {
          if (
            !inventory?.items.some((r) => resourceKey(r) === resourceKey(item))
          ) {
            const full = item as ResourceItem;
            await installLighting(send, item, await download(full), (n) =>
              setNotice(`${n}%`),
            );
          }
          if (action === "apply") {
            const result = await send("resources_apply", {
              profileId,
              resourceId: item.resourceId,
              revision: item.revision,
            });
            if (!result.applied || !result.runtimeReloaded)
              throw new Error(
                zh
                  ? "设备未确认应用，请重新连接核对。"
                  : "Device did not confirm application. Reconnect to verify.",
              );
          }
        });
        setNotice(
          action === "apply"
            ? zh
              ? "已应用，保留当前颜色、亮度和速度。"
              : "Applied. Your colors, brightness and speed are retained."
            : zh
              ? "已下载到设备，可离线使用。"
              : "Downloaded to device for offline use.",
        );
      }
      await refresh();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy("");
    }
  };
  const title = axis
    ? zh
      ? "轴体映射"
      : "Switch mapping"
    : type === "key-lighting"
      ? zh
        ? "按键灯效"
        : "Key lighting"
      : zh
        ? "氛围灯效"
        : "Ambient lighting";
  return (
    <Box width="100%" minW="0" borderWidth={lightingSelector ? "0" : "1px"} borderRadius="lg" p={lightingSelector ? "0" : "4"}>
      <Flex justify="space-between" align="center" gap="3" wrap="wrap">
        <Stack gap="1">
          {!lightingSelector && <Text fontWeight="bold">{title}</Text>}
          {loading ? (
            <Skeleton height="20px" width="180px" />
          ) : (
            <Text color="fg.muted" fontSize="sm">
              {current || (zh ? "尚未读取" : "Not loaded")}
              {selected ? ` · v${selected.revision}` : ""}
            </Text>
          )}
          {update && (
            <Badge colorPalette="green">
              {zh ? "有新版本" : "Update available"}
            </Badge>
          )}
        </Stack>
        <Button
          size="sm"
          disabled={!ctx.deviceConnected || !!busy}
          onClick={() => {
            setOpen(true);
            setTab(axis ? "server" : "device");
          }}
        >
          {zh ? "浏览资源" : "Browse resources"}
        </Button>
      </Flex>
      {notice && (
        <Text role="status" mt="2" color="green.500">
          {notice}
        </Text>
      )}
      {error && (
        <Text role="alert" mt="2" fontSize="sm" color="red.500">
          {error}
        </Text>
      )}
      {axis && (
        <Text fontSize="sm" mt="2" color="fg.muted">
          {ctx.calibrationStatus.allCalibrated
            ? zh
              ? "已校准"
              : "Calibrated"
            : zh
              ? "需要校准"
              : "Calibration required"}
          {ctx.activeMapping
            ? ` · ${ctx.activeMapping.length} ${zh ? "个采样点" : "samples"} · ${Number(ctx.activeMapping.step.toFixed(3))} mm`
            : ""}
        </Text>
      )}
      {axis && !ctx.calibrationStatus.allCalibrated && (
        <Button
          mt="2"
          size="sm"
          onClick={() =>
            ctx.startManualCalibration().catch((e) => setError(String(e)))
          }
        >
          {zh ? "开始校准" : "Start calibration"}
        </Button>
      )}
      <Drawer.Root
        open={open}
        onOpenChange={(e) => !busy && setOpen(e.open)}
        placement="end"
        size="md"
      >
        <Portal>
          <Drawer.Backdrop />
          <Drawer.Positioner>
            <Drawer.Content>
              <Drawer.Header>
                <Drawer.Title>{title}</Drawer.Title>
              </Drawer.Header>
              <Drawer.Body>
                <HStack mb="4">
                  {!axis && (
                    <Button
                      size="sm"
                      variant={tab === "device" ? "solid" : "outline"}
                      onClick={() => setTab("device")}
                    >
                      {zh ? "设备已安装" : "Installed"}
                    </Button>
                  )}
                  <Button
                    size="sm"
                    variant={tab === "server" ? "solid" : "outline"}
                    onClick={() => setTab("server")}
                  >
                    {zh ? "服务器资源" : "Server resources"}
                  </Button>
                  <Button
                    size="sm"
                    variant="ghost"
                    onClick={() => void refresh()}
                    disabled={!!busy}
                  >
                    {zh ? "刷新" : "Refresh"}
                  </Button>
                </HStack>
                {!axis && inventory && (
                  <Text fontSize="sm" color="fg.muted" mb="3">
                    {Math.round(inventory.used / 1024)} /{" "}
                    {inventory.capacity / 1024} KiB · {inventory.items.length} /{" "}
                    {inventory.maxEntries}
                  </Text>
                )}
                <Text fontSize="sm" color="fg.muted" mb="3">
                  {axis
                    ? zh
                      ? "轴体映射全局应用于全部磁轴按键，安装后需要重新校准。"
                      : "The curve applies to all Hall keys. Recalibration is required after installation."
                    : zh
                      ? "选择具体版本下载；应用仅影响当前 Profile。"
                      : "Download a specific revision. Applying affects the current Profile."}
                </Text>
                <Stack gap="4">
                  {loading ? (
                    <Skeleton height="220px" />
                  ) : (
                    (tab === "server"
                      ? items
                      : inventory?.items.filter((r) => r.type === type) || []
                    ).map((item) => {
                      const full =
                        "source" in item
                          ? (item as ResourceItem)
                          : items.find(
                              (s) => resourceKey(s) === resourceKey(item),
                            );
                      const active = axis
                        ? ctx.defaultMappingId === item.resourceId
                        : selected &&
                          resourceKey(selected) === resourceKey(item);
                      return (
                        <Box
                          key={resourceKey(item)}
                          borderWidth="1px"
                          borderRadius="lg"
                          p="4"
                        >
                          <HStack justify="space-between">
                            <Text fontWeight="bold">{item.name}</Text>
                            <Badge>
                              {active
                                ? zh
                                  ? "使用中"
                                  : "Active"
                                : `v${item.revision}`}
                            </Badge>
                          </HStack>
                          {!lightingSelector && (deviceSources[resourceKey(item)] ||
                            full?.source) && (
                            <ResourcePreview
                              source={
                                deviceSources[resourceKey(item)] || full?.source
                              }
                            />
                          )}
                          <Text fontSize="sm" color="fg.muted" mb="3">
                            {full?.description}
                            {full
                              ? ` · v${full.revision} · ${full.size} B · XORA 2.0`
                              : ""}
                          </Text>
                          <Flex gap="2" wrap="wrap">
                            {tab === "server" &&
                              !axis &&
                              !installed(item as ResourceItem) && (
                                <Button
                                  size="sm"
                                  disabled={
                                    !!busy ||
                                    unsupported ||
                                    !inventory?.storageReady
                                  }
                                  onClick={() => void perform(item, "download")}
                                >
                                  {zh ? "下载到设备" : "Download to device"}
                                </Button>
                              )}
                            <Button
                              size="sm"
                              disabled={!!busy || unsupported || !!active}
                              onClick={() => void perform(item, "apply")}
                            >
                              {update &&
                              resourceKey(update) === resourceKey(item)
                                ? zh
                                  ? "更新并应用"
                                  : "Update and apply"
                                : axis
                                  ? zh
                                    ? "下载并应用"
                                    : "Download and apply"
                                  : zh
                                    ? "应用到当前 Profile"
                                    : "Apply to current Profile"}
                            </Button>
                            {tab === "device" && (
                              <Button
                                size="sm"
                                variant="ghost"
                                disabled={
                                  !!busy ||
                                  !!active ||
                                  referenced(item) ||
                                  !!(item as InstalledResource).factorySupplied
                                }
                                onClick={() => void perform(item, "remove")}
                              >
                                {zh ? "移除" : "Remove"}
                              </Button>
                            )}
                          </Flex>
                        </Box>
                      );
                    })
                  )}
                </Stack>
                {error && (
                  <Text role="alert" mt="3" color="red.500">
                    {error}
                  </Text>
                )}
                {notice && (
                  <Text role="status" mt="3">
                    {notice}
                  </Text>
                )}
              </Drawer.Body>
              <Drawer.Footer>
                <Button disabled={!!busy} onClick={() => setOpen(false)}>
                  {zh ? "关闭" : "Close"}
                </Button>
              </Drawer.Footer>
            </Drawer.Content>
          </Drawer.Positioner>
        </Portal>
      </Drawer.Root>
    </Box>
  );
}
