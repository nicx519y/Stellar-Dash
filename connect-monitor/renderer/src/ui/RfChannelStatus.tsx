import { Box, Text, VStack } from "@chakra-ui/react";
import type { PacketEvent } from "../../../shared/monitor-types";

const states = ["Normal", "Prepare", "Confirm", "Scheduled", "Verify", "Recovery", "Probe", "Returning", "Backup recovery"];
const disabled = ["Available", "Timing acceptance pending", "Peer capability missing", "Rate unsupported", "No safe probe window", "Maintenance budget exhausted"];
const reasons = ["None", "Persistent loss", "Severe loss", "Manual", "Rollback", "ACK missing", "Prepare timeout", "Confirm timeout", "Radio setup failed", "Probe failed", "Insufficient samples", "Accepted", "No improvement"];
const loss = (value: number | undefined) => value === undefined || value === 65535 ? "Unknown" : `${(value/10).toFixed(1)}%`;
const us = (value: number | undefined) => value === undefined ? "—" : value < 1000 ? `${value}µs` : `${(value/1000).toFixed(2)}ms`;

export function RfChannelStatus({ packets }: { packets: PacketEvent[] }) {
  const pages = new Map<number, PacketEvent>();
  let newest=0;
  for (const packet of packets) {
    newest=Math.max(newest,packet.timestampMs);
    if (packet.rfChannel) {
    const previous=pages.get(packet.rfChannel.page);
    if (!previous || previous.timestampMs<=packet.timestampMs) pages.set(packet.rfChannel.page,packet);
    }
  }
  if (!pages.size) return null;
  const a=pages.get(0)?.rfChannel, b=pages.get(1)?.rfChannel, c=pages.get(2)?.rfChannel;
  const local=pages.get(3)?.rfChannel, receiver=pages.get(4)?.rfChannel, transition=pages.get(5)?.rfChannel;
  const fresh=(page: number) => {
    const packet=pages.get(page);
    return !!packet?.rfChannel && packet.rfChannel.ageMs<3000 && newest-packet.timestampMs<3000;
  };
  const scores=[...(fresh(1)?b?.scores??[]:[]),...(fresh(2)?c?.scores??[]:[])];
  return <Box p={3} borderBottomWidth="1px" borderColor="whiteAlpha.200">
    <VStack align="stretch" gap={1} fontSize="11px" color="gray.300">
      <Text fontWeight="bold" color="green.200">RF v3 · {fresh(0) ? states[a?.state??0]??"Unknown" : "TX status stale"}</Text>
      <Text>Probe: {fresh(0)?disabled[a?.probeDisabled??1]??"Unavailable":"Unavailable status"}</Text>
      <Text>Primary {fresh(0)?a?.primary:"—"} · Candidate {fresh(0)?a?.candidate:"—"} · Backup {fresh(0)?a?.backups?.join(" / "):"—"}</Text>
      <Text>{fresh(0)?reasons[a?.reason??0]??"Unknown":"—"}{fresh(1)&&b?.probation?" · Observing":""}</Text>
      <Text>Maintenance reserved: {fresh(0)?us(a?.maintenanceUs):"—"} / 1s · limit 30ms</Text>
      <Text>Loss: {fresh(0)?loss(a?.beforePermille):"—"} → {fresh(0)?loss(a?.afterPermille):"—"}</Text>
      <Text>ACK timing: {fresh(0)&&((a?.localCaps??0)&(a?.peerCaps??0)&32)?"Accepted":"Conservative / acceptance pending"}</Text>
      <Text>Samples {fresh(1)?b?.sampleCount??"—":"—"} · {fresh(1)&&b?.sampleSource===2?"Probe":"Residence"} · age {fresh(2)&&c?.historyAgeMs!==65535?`${c?.historyAgeMs}ms`:"Unknown"}</Text>
      <Text>Probes {fresh(1)?b?.probes??"—":"—"} · Failures {fresh(1)?b?.failures??"—":"—"}</Text>
      <Text>Input gap {us(local?.lastGapUs)} · max {us(local?.maxGapUs)}</Text>
      <Text>First input after switch {us(local?.firstPacketUs)}</Text>
      <Text>Gap before switch {us(transition?.beforeGapUs)} · across switch {us(transition?.transitionGapUs)}</Text>
      <Text>RX failures: reservation {transition?.reservationFailures??"—"} · radio {transition?.radioFailures??"—"} · zero receive {transition?.candidateFailures??"—"}</Text>
      <Text>Transition sequence gaps {local?.transitionMissing??"—"} · Input coalesced {local?.inputCoalesced??"—"}</Text>
      {!!receiver?.versionMismatches && <Text color="orange.200">Protocol mismatches: {receiver.versionMismatches} · update both TX and RX</Text>}
      {scores.length>0 && <Text>Recent loss: {scores.map(s=>`${s.channel}: ${loss(s.lossPermille)}`).join(" · ")}</Text>}
      <Text color="gray.500">Switch timing unverified until hardware acceptance. Coalesced input is not RF loss.</Text>
    </VStack>
  </Box>;
}
