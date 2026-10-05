export const resourceFetch = (input: RequestInfo | URL, init?: RequestInit) =>
  fetch(input, { credentials: "same-origin", cache: "no-store", ...init });
