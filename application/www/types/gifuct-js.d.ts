declare module 'gifuct-js' {
    export function parseGIF(buffer: ArrayBuffer | Uint8Array): any;
    export function decompressFrames(gif: any, buildImagePatches: boolean): Array<{
        patch: Uint8ClampedArray;
        delay?: number;
        dims?: { width: number; height: number; top: number; left: number };
    }>;
}
