import { IconButton } from "@chakra-ui/react";
import { FaPause, FaPlay } from "react-icons/fa";
import { GrDocumentDownload } from "react-icons/gr";
import { GrClearOption } from "react-icons/gr";

import { toolbarIconButtonProps } from "./panelStyles";

export function ClearDataIconButton({
  label,
  disabled = false,
  onClick,
}: {
  label: string;
  disabled?: boolean;
  onClick: () => void;
}) {
  return (
    <IconButton
      {...toolbarIconButtonProps}
      aria-label={label}
      title={label}
      disabled={disabled}
      onClick={onClick}
    >
      <GrClearOption />
    </IconButton>
  );
}

export function ListeningToggleIconButton({
  listening,
  disabled = false,
  onToggle,
}: {
  listening: boolean;
  disabled?: boolean;
  onToggle: () => void;
}) {
  const label = listening ? "Pause Listening" : "Start Listening";

  return (
    <IconButton
      {...toolbarIconButtonProps}
      aria-label={label}
      title={label}
      disabled={disabled}
      colorPalette={listening ? "green" : "yellow"}
      onClick={onToggle}
    >
      {listening ? <FaPause /> : <FaPlay />}
    </IconButton>
  );
}

export function ExportMarkdownIconButton({
  label,
  disabled = false,
  onClick,
}: {
  label: string;
  disabled?: boolean;
  onClick: () => void;
}) {
  return (
    <IconButton
      {...toolbarIconButtonProps}
      aria-label={label}
      title={label}
      disabled={disabled}
      onClick={onClick}
    >
      <GrDocumentDownload />
    </IconButton>
  );
}
