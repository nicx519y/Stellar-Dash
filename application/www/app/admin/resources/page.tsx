"use client";
import { useEffect, useRef, useState } from "react";
import {
  Box,
  Badge,
  Field,
  Button,
  Flex,
  Input,
  Tabs,
  Stack,
  Text,
  Textarea,
  Skeleton,
  Image,
} from "@chakra-ui/react";
import { AdminAccessGuard } from "@/components/admin-access-guard";
import { AdminCard, AdminPageHeader } from "@/components/admin/admin-surface";
import { ResourcePreview } from "@/components/resource-preview";
import { useLanguage } from "@/contexts/language-context";
import { resourceFetch } from "@hbox/resource-api";
import {
  resourceJSON,
  verifyResource,
  type ResourceItem,
} from "@/lib/resources";
import { LuUpload, LuRefreshCw, LuDownload, LuFileJson, LuSave } from 'react-icons/lu';
const statusPalette = {draft:'gray',published:'green',withdrawn:'orange'} as const;
function Resources() {
  const { currentLanguage } = useLanguage(),
    zh = currentLanguage === "zh";
  const [items, setItems] = useState<ResourceItem[]>([]),
    [loading, setLoading] = useState(true),
    [type, setType] = useState("switch-mapping"),
    [selected, setSelected] = useState<ResourceItem | null>(null),
    [name, setName] = useState(""),
    [description, setDescription] = useState(""),
    [error, setError] = useState(""),
    [busy, setBusy] = useState(false),
    [imageURL, setImageURL] = useState("");
  const upload = useRef<HTMLInputElement>(null),
    cover = useRef<HTMLInputElement>(null);
  const load = async () => {
    setLoading(true);
    try {
      const data = await resourceJSON<{ items: ResourceItem[] }>(
        await resourceFetch("/api/admin/resources"),
      );
      setItems(data.items);
      setSelected((old) =>
        old
          ? data.items.find(
              (r) =>
                r.catalogId === old.catalogId && r.revision === old.revision,
            ) || null
          : null,
      );
    } finally {
      setLoading(false);
    }
  };
  useEffect(() => {
    void load().catch((e) => setError(String(e)));
  }, []);
  useEffect(() => {
    let stopped = false,
      url = "";
    setImageURL("");
    if (selected?.type === "switch-mapping" && selected.hasImage)
      void resourceFetch(
        `/api/admin/resources/${encodeURIComponent(selected.catalogId)}/image`,
      )
        .then(async (response) => {
          if (!response.ok) throw Error("Cover unavailable");
          url = URL.createObjectURL(await response.blob());
          if (!stopped) setImageURL(url);
          else URL.revokeObjectURL(url);
        })
        .catch(() => {});
    return () => {
      stopped = true;
      if (url) URL.revokeObjectURL(url);
    };
  }, [selected]);
  const run = async (operation: () => Promise<unknown>) => {
    setBusy(true);
    setError("");
    try {
      await operation();
      await load();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };
  const request = (url: string, method: string, body?: unknown) =>
    resourceFetch(url, {
      method,
      headers: { "Content-Type": "application/json" },
      ...(body ? { body: JSON.stringify(body) } : {}),
    }).then(resourceJSON);
  const choose = (item: ResourceItem) => {
    setSelected(item);
    setName(item.name);
    setDescription(item.description);
  };
  const download = async (item: ResourceItem, format: string) => {
    const response = await resourceFetch(
      `/api/admin/resources/${encodeURIComponent(item.catalogId)}/revisions/${item.revision}/${format}`,
    );
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const url = URL.createObjectURL(await response.blob());
    const a = document.createElement("a");
    a.href = url;
    a.download = `${item.resourceId}.xora-resource${format === "source" ? ".json" : ""}`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  return (
    <Tabs.Root value={type} variant="outline" width="full"
      onValueChange={({value}) => {setType(value);setSelected(null);setName('');setDescription('');}}>
    <Stack gap="6" color="fg">
      <AdminPageHeader
        title={zh ? "资源管理" : "Resources"}
        description={
          zh
            ? "管理轴体映射、按键灯效和氛围灯效。上传后校验，发布后供设备下载。"
            : "Manage switch curves and lighting resources. Validate uploads and publish device downloads."
        }
      />
      <Tabs.List colorPalette="green" width="full" overflowX="auto">
        <Tabs.Trigger value="switch-mapping" flexShrink={0} px={{base:2,md:4}} fontSize={{base:'xs',md:'sm'}}>
          {zh ? '轴体映射' : 'Switch mappings'}
        </Tabs.Trigger>
        <Tabs.Trigger value="key-lighting" flexShrink={0} px={{base:2,md:4}} fontSize={{base:'xs',md:'sm'}}>
          {zh ? '按键灯效' : 'Key lighting'}
        </Tabs.Trigger>
        <Tabs.Trigger value="ambient-lighting" flexShrink={0} px={{base:2,md:4}} fontSize={{base:'xs',md:'sm'}}>
          {zh ? '氛围灯效' : 'Ambient lighting'}
        </Tabs.Trigger>
      </Tabs.List>
      <AdminCard p="4" bg="bg.panel" borderColor="border">
      <Flex gap="3" wrap="wrap">
        <Button colorPalette="green" disabled={busy} onClick={() => upload.current?.click()}>
          <LuUpload aria-hidden="true" />{zh ? "上传资源文件" : "Upload resource"}
        </Button>
        <Button
          variant="surface"
          disabled={busy}
          onClick={() => void run(load)}
        >
          <LuRefreshCw aria-hidden="true" />{zh ? "刷新" : "Refresh"}
        </Button>
      </Flex>
      </AdminCard>
      <input
        hidden
        ref={upload}
        type="file"
        accept=".json,.xora-resource"
        onChange={(e) => {
          const file = e.target.files?.[0];
          e.target.value = "";
          if (file)
            void run(async () => {
              if (file.size > 65536)
                throw new Error(zh ? "文件过大" : "File too large");
              const source = file.name.endsWith(".json")
                ? JSON.parse(await file.text())
                : await verifyResource(
                    new Uint8Array(await file.arrayBuffer()),
                  );
              await request("/api/admin/resources", "POST", {
                source,
                catalogId:
                  source.type === "switch-mapping" &&
                  selected?.type === source.type
                    ? selected?.catalogId
                    : undefined,
              });
            });
        }}
      />
      {error && (
        <Text role="alert" color="red.500">
          {error}
        </Text>
      )}
      <Tabs.Content value={type} p="0">
      <Flex gap="6" direction={{ base: "column", lg: "row" }} align="start">
        <Stack flex="1" width="full" minW="0">
          {loading ? (
            <Skeleton height="240px" />
          ) : (
            items
              .filter((r) => r.type === type)
              .map((item) => (
                <AdminCard
                  key={`${item.catalogId}:${item.revision}`}
                  p="4"
                  borderColor={selected?.catalogId === item.catalogId && selected.revision === item.revision ? "green.500" : "border"}
                  transition="border-color 150ms ease"
                  bg={
                    selected?.catalogId === item.catalogId &&
                    selected.revision === item.revision
                      ? "bg.muted"
                      : "bg.panel"
                  }
                >
                  <Flex justify="space-between" gap="2" wrap="wrap">
                    <Box>
                      <Text fontWeight="semibold" fontSize="md" color="fg" overflowWrap="anywhere">
                        {item.name}<Text as="span" fontSize="xs" fontWeight="normal" color="fg.muted"> · v{item.revision}</Text>
                      </Text>
                      <Flex align="center" gap="3" mt="2">
                      <Badge size="sm" variant="subtle" colorPalette={statusPalette[item.status]}>
                        {
                          {
                            draft: zh ? "待发布" : "Draft",
                            published: zh ? "已发布" : "Published",
                            withdrawn: zh ? "已下架" : "Withdrawn",
                          }[item.status]
                        }
                      </Badge>
                      <Text fontSize="xs" color="fg.muted">{item.size} B</Text>
                      </Flex>
                    </Box>
                    <Button
                      size="sm"
                      variant={selected?.catalogId === item.catalogId && selected.revision === item.revision ? "surface" : "outline"}
                      colorPalette={selected?.catalogId === item.catalogId && selected.revision === item.revision ? "green" : "gray"}
                      onClick={() => choose(item)}
                    >
                      {zh ? "查看" : "Details"}
                    </Button>
                  </Flex>
                  {item.validationError && (
                    <Text color="red.fg" fontSize="sm" mt="2">
                      {item.validationError}
                    </Text>
                  )}
                  <Flex mt="4" pt="3" borderTopWidth="1px" borderColor="border" gap="2" wrap="wrap">
                    <Button
                      size="sm"
                      colorPalette="green"
                      variant={item.status === "published" ? "surface" : "solid"}
                      disabled={
                        busy ||
                        !!item.validationError ||
                        item.status === "published"
                      }
                      onClick={() =>
                        void run(() =>
                          request(
                            `/api/admin/resources/${encodeURIComponent(item.catalogId)}/revisions/${item.revision}/publish`,
                            "POST",
                          ),
                        )
                      }
                    >
                      {zh ? "发布" : "Publish"}
                    </Button>
                    {item.status === "published" && (
                      <Button
                        size="sm"
                        variant="outline"
                        colorPalette="orange"
                        disabled={busy}
                        onClick={() =>
                          void run(() =>
                            request(
                              `/api/admin/resources/${encodeURIComponent(item.catalogId)}/unpublish`,
                              "POST",
                            ),
                          )
                        }
                      >
                        {zh ? "下架" : "Withdraw"}
                      </Button>
                    )}
                    <Button
                      size="sm"
                      variant="ghost"
                      color="fg.muted"
                      _hover={{color:"fg",bg:"bg.muted"}}
                      disabled={busy}
                      onClick={() => void run(() => download(item, "source"))}
                    >
                      <LuFileJson aria-hidden="true" />{zh ? "导出源文件" : "Export source"}
                    </Button>
                    <Button
                      size="sm"
                      variant="ghost"
                      color="fg.muted"
                      _hover={{color:"fg",bg:"bg.muted"}}
                      disabled={busy || !!item.validationError}
                      onClick={() => void run(() => download(item, "download"))}
                    >
                      <LuDownload aria-hidden="true" />{zh ? "下载运行文件" : "Download asset"}
                    </Button>
                  </Flex>
                </AdminCard>
              ))
          )}
        </Stack>
        {selected && (
          <AdminCard
            width={{ base: "full", lg: "440px" }}
            maxW="100%"
            flexShrink={0}
            bg="bg.panel"
            borderColor="border"
            p="5"
          >
            <Stack gap="4">
              <Stack gap="2">
              <Text fontWeight="semibold" fontSize="lg" color="fg" overflowWrap="anywhere">{selected.name}</Text>
              <Flex align="center" gap="2">
                <Badge variant="subtle" colorPalette={statusPalette[selected.status]}>{zh ? {draft:'待发布',published:'已发布',withdrawn:'已下架'}[selected.status] : {draft:'Draft',published:'Published',withdrawn:'Withdrawn'}[selected.status]}</Badge>
                <Text fontSize="xs" color="fg.muted">v{selected.revision} · {selected.size} B</Text>
              </Flex>
              </Stack>
              {selected.type === "switch-mapping" && imageURL && (
                <Image
                  src={imageURL}
                  alt={selected.name}
                  maxH="180px"
                  objectFit="contain"
                />
              )}
              {!selected.validationError && (
                <Box bg="bg" borderWidth="1px" borderColor="border" borderRadius="lg" p="3">
                  <ResourcePreview source={selected.source} />
                </Box>
              )}
              <Box bg="bg" borderWidth="1px" borderColor="border" borderRadius="md" p="3">
                <Text fontSize="2xs" color="fg.subtle" mb="1">SHA-256</Text>
                <Text fontSize="2xs" fontFamily="mono" color="fg.muted" wordBreak="break-all">{selected.sha256 || "—"}</Text>
              </Box>
              <Field.Root>
              <Field.Label color="fg.muted" fontSize="xs">{zh ? "资源名称" : "Resource name"}</Field.Label>
              <Input bg="bg.muted" borderColor="border" color="fg"
                aria-label={zh ? "资源名称" : "Resource name"}
                value={name}
                onChange={(e) => setName(e.target.value)}
                maxLength={80}
              />
              </Field.Root>
              <Field.Root>
              <Field.Label color="fg.muted" fontSize="xs">{zh ? "资源描述" : "Description"}</Field.Label>
              <Textarea bg="bg.muted" borderColor="border" color="fg"
                aria-label={zh ? "资源描述" : "Description"}
                value={description}
                onChange={(e) => setDescription(e.target.value)}
                maxLength={500}
              />
              </Field.Root>
              <Button
                colorPalette="green"
                disabled={busy}
                onClick={() =>
                  void run(() =>
                    request(
                      `/api/admin/resources/${encodeURIComponent(selected.catalogId)}`,
                      "PATCH",
                      { name, description },
                    ),
                  )
                }
              >
                <LuSave aria-hidden="true" />{zh ? "保存信息" : "Save details"}
              </Button>
              {selected.type === 'switch-mapping' ? <>
              <Button
                disabled={busy}
                variant="outline"
                onClick={() => cover.current?.click()}
              >
                {zh ? "上传封面" : "Upload cover"}
              </Button>
              <input
                hidden
                ref={cover}
                type="file"
                accept="image/png,image/jpeg,image/webp"
                onChange={(e) => {
                  const file = e.target.files?.[0];
                  e.target.value = "";
                  if (file)
                    void run(async () =>
                      resourceJSON(
                        await resourceFetch(
                          `/api/admin/resources/${encodeURIComponent(selected.catalogId)}/image`,
                          {
                            method: "PUT",
                            headers: { "Content-Type": file.type },
                            body: file,
                          },
                        ),
                      ),
                    );
                }}
              />
              </> : <Text fontSize="xs" color="fg.subtle" lineHeight="1.7">
                {zh ? '灯效选择器使用图标，无需上传封面。' : 'Lighting selectors use icons. No cover image is needed.'}
              </Text>}
              <Text fontSize="xs" color="fg.subtle" lineHeight="1.7">
                {zh
                  ? "效果修改请编辑源文件并提高修订号后上传；已发布版本保持不变。"
                  : "Edit the source file and upload a higher revision to change the effect. Published versions are immutable."}
              </Text>
            </Stack>
          </AdminCard>
        )}
      </Flex>
      </Tabs.Content>
    </Stack>
    </Tabs.Root>
  );
}
export default function AdminResourcesPage() {
  return (
    <AdminAccessGuard>
      <Resources />
    </AdminAccessGuard>
  );
}
