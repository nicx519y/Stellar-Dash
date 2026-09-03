export const scrollbarStyle = {
  scrollbarWidth: "thin",
  scrollbarColor: "rgba(92,255,138,0.62) rgba(255,255,255,0.04)",
  "&::-webkit-scrollbar": {
    width: "10px",
    height: "10px",
  },
  "&::-webkit-scrollbar-track": {
    background: "rgba(255,255,255,0.04)",
    borderRadius: "999px",
  },
  "&::-webkit-scrollbar-thumb": {
    background: "linear-gradient(90deg, rgba(92,255,138,0.78), rgba(98,247,255,0.62))",
    borderRadius: "999px",
    border: "2px solid rgba(11,15,22,0.95)",
  },
  "&::-webkit-scrollbar-thumb:hover": {
    background: "linear-gradient(90deg, rgba(92,255,138,0.95), rgba(98,247,255,0.85))",
  },
} as const;
