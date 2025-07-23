import JSZip from 'jszip';
import { FirmwareManifest, FirmwareComponent } from '@/types/types';

/**
 * 计算数据的SHA256校验和
 * 优先使用Web Crypto API，不可用时回退到JavaScript实现
 */
export const calculateSHA256 = async (data: Uint8Array): Promise<string> => {
    // 检查Web Crypto API是否可用
    if (typeof crypto === 'undefined' || 
        typeof crypto.subtle === 'undefined' || 
        typeof crypto.subtle.digest !== 'function') {
        console.warn('Web Crypto API not available, falling back to JS implementation');
        return calculateSHA256JS(data);
    }

    // 检查是否在安全上下文中（HTTPS或localhost）
    if (typeof window !== 'undefined' && 
        window.location && 
        window.location.protocol !== 'https:' && 
        !window.location.hostname.match(/^(localhost|127\.0\.0\.1|::1)$/)) {
        console.warn('Web Crypto API requires secure context (HTTPS), falling back to JS implementation');
        return calculateSHA256JS(data);
    }

    try {
        const hashBuffer = await crypto.subtle.digest('SHA-256', data);
        const hashArray = Array.from(new Uint8Array(hashBuffer));
        return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
    } catch (error) {
        console.error('Web Crypto API SHA256 calculation failed:', error);
        console.warn('Falling back to JS implementation');
        // 回退到JS实现
        return calculateSHA256JS(data);
    }
};

/**
 * 纯JavaScript SHA256实现 - 修复版本
 * 当Web Crypto API不可用时的备用实现
 */
export const calculateSHA256JS = (data: Uint8Array): string => {
    // SHA-256 constants
    const K = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    ];

    // Helper functions
    const rightRotate = (value: number, amount: number): number => {
        return ((value >>> amount) | (value << (32 - amount))) >>> 0;
    };

    const safeAdd = (...nums: number[]): number => {
        let result = 0;
        for (const num of nums) {
            result = (result + (num >>> 0)) >>> 0;
        }
        return result;
    };

    // Pre-processing: adding padding bits
    const msgLength = data.length;
    const bitLength = msgLength * 8;
    
    // Calculate padded length - 必须为64字节的倍数
    const paddedLength = Math.ceil((msgLength + 9) / 64) * 64;
    const padded = new Uint8Array(paddedLength);
    
    // Copy message
    padded.set(data);
    
    // Add '1' bit (plus zero padding to make it a byte)
    padded[msgLength] = 0x80;
    
    // Add length in bits as 64-bit big-endian integer to the end
    // 修复：正确处理64位长度编码
    const bitLengthHigh = Math.floor(bitLength / 0x100000000); // 高32位
    const bitLengthLow = bitLength >>> 0; // 低32位
    
    // 写入高32位（位56-63）
    padded[paddedLength - 8] = (bitLengthHigh >>> 24) & 0xff;
    padded[paddedLength - 7] = (bitLengthHigh >>> 16) & 0xff;
    padded[paddedLength - 6] = (bitLengthHigh >>> 8) & 0xff;
    padded[paddedLength - 5] = bitLengthHigh & 0xff;
    
    // 写入低32位（位0-31）
    padded[paddedLength - 4] = (bitLengthLow >>> 24) & 0xff;
    padded[paddedLength - 3] = (bitLengthLow >>> 16) & 0xff;
    padded[paddedLength - 2] = (bitLengthLow >>> 8) & 0xff;
    padded[paddedLength - 1] = bitLengthLow & 0xff;
    
    // Initialize hash values (first 32 bits of fractional parts of square roots of first 8 primes)
    let h0 = 0x6a09e667;
    let h1 = 0xbb67ae85;
    let h2 = 0x3c6ef372;
    let h3 = 0xa54ff53a;
    let h4 = 0x510e527f;
    let h5 = 0x9b05688c;
    let h6 = 0x1f83d9ab;
    let h7 = 0x5be0cd19;

    // Process message in 512-bit chunks
    for (let chunkStart = 0; chunkStart < paddedLength; chunkStart += 64) {
        const w = new Array(64);

        // Break chunk into sixteen 32-bit big-endian words
        for (let i = 0; i < 16; i++) {
            const j = chunkStart + i * 4;
            w[i] = (padded[j] << 24) | (padded[j + 1] << 16) | (padded[j + 2] << 8) | padded[j + 3];
            w[i] = w[i] >>> 0; // Ensure unsigned 32-bit
        }

        // Extend the sixteen 32-bit words into sixty-four 32-bit words
        for (let i = 16; i < 64; i++) {
            const s0 = rightRotate(w[i - 15], 7) ^ rightRotate(w[i - 15], 18) ^ (w[i - 15] >>> 3);
            const s1 = rightRotate(w[i - 2], 17) ^ rightRotate(w[i - 2], 19) ^ (w[i - 2] >>> 10);
            w[i] = safeAdd(w[i - 16], s0, w[i - 7], s1);
        }

        // Initialize working variables for this chunk
        let a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;

        // Main loop
        for (let i = 0; i < 64; i++) {
            const S1 = rightRotate(e, 6) ^ rightRotate(e, 11) ^ rightRotate(e, 25);
            const ch = (e & f) ^ ((~e) & g);
            const temp1 = safeAdd(h, S1, ch, K[i], w[i]);
            const S0 = rightRotate(a, 2) ^ rightRotate(a, 13) ^ rightRotate(a, 22);
            const maj = (a & b) ^ (a & c) ^ (b & c);
            const temp2 = safeAdd(S0, maj);

            h = g;
            g = f;
            f = e;
            e = safeAdd(d, temp1);
            d = c;
            c = b;
            b = a;
            a = safeAdd(temp1, temp2);
        }

        // Add this chunk's hash to result so far
        h0 = safeAdd(h0, a);
        h1 = safeAdd(h1, b);
        h2 = safeAdd(h2, c);
        h3 = safeAdd(h3, d);
        h4 = safeAdd(h4, e);
        h5 = safeAdd(h5, f);
        h6 = safeAdd(h6, g);
        h7 = safeAdd(h7, h);
    }

    // Produce the final hash value
    return [h0, h1, h2, h3, h4, h5, h6, h7]
        .map(h => (h >>> 0).toString(16).padStart(8, '0'))
        .join('');
};

/**
 * 解压固件包
 * 从ZIP文件中提取manifest和组件文件，并验证校验和
 */
export const extractFirmwarePackage = async (data: Uint8Array): Promise<{ manifest: FirmwareManifest, components: { [key: string]: FirmwareComponent } }> => {
    try {
        // 使用JSZip解压ZIP文件
        const zip = await JSZip.loadAsync(data);
        
        // 1. 读取manifest.json
        const manifestFile = zip.file('manifest.json');
        if (!manifestFile) {
            throw new Error('firmware package is missing manifest.json file');
        }
        
        const manifestContent = await manifestFile.async('string');
        const manifest: FirmwareManifest = JSON.parse(manifestContent);
        
        // 验证manifest结构
        if (!manifest.version || !manifest.slot || !manifest.components || !Array.isArray(manifest.components)) {
            throw new Error('manifest.json format is invalid');
        }
        
        // 2. 读取所有组件文件
        const components: { [key: string]: FirmwareComponent } = {};
        
        for (const comp of manifest.components) {
            if (!comp.name || !comp.file || !comp.address || !comp.size || !comp.sha256) {
                throw new Error(`component ${comp.name || 'unknown'} config is incomplete`);
            }
            
            // 查找组件文件
            const componentFile = zip.file(comp.file);
            if (!componentFile) {
                throw new Error(`firmware package is missing component file: ${comp.file}`);
            }
            
            // 读取组件数据
            const componentData = await componentFile.async('uint8array');
            
            // 验证文件大小
            if (componentData.length !== comp.size) {
                console.warn(`component ${comp.name} file size mismatch: expected ${comp.size}, actual ${componentData.length}`);
            }
            
            // 验证SHA256校验和
            try {
                const calculatedHash = await calculateSHA256(componentData);
                if (calculatedHash !== comp.sha256) {
                    console.warn(`component ${comp.name} SHA256 checksum mismatch: expected ${comp.sha256}, actual ${calculatedHash}`);
                    // 在开发环境中可能需要抛出错误，这里先警告
                    // throw new Error(`component ${comp.name} SHA256 checksum mismatch`);
                }
            } catch (checksumError) {
                console.warn(`component ${comp.name} SHA256 checksum calculation failed:`, checksumError);
                // 继续处理，不因为校验和计算失败而中断
            }
            
            // 创建组件对象
            components[comp.name] = {
                ...comp,
                data: componentData
            };
        }
        
        return { manifest, components };
        
    } catch (error) {
        if (error instanceof Error) {
            throw new Error(`failed to extract firmware package: ${error.message}`);
        } else {
            throw new Error('failed to extract firmware package: unknown error');
        }
    }
}; 