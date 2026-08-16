import type { NextConfig } from "next";
import withBundleAnalyzer from '@next/bundle-analyzer';
import path from 'node:path';

const isDevelopment = process.env.NODE_ENV === 'development';
const requestedMockBuild = process.env.HBOX_BUILD_VARIANT === 'mock';
const mockTransportEnabled = process.env.NEXT_PUBLIC_DEVICE_TRANSPORT === 'mock';
const offlinePreviewEnabled = process.env.NEXT_PUBLIC_OFFLINE_PREVIEW === 'true';
const isMockBuild = requestedMockBuild && mockTransportEnabled && offlinePreviewEnabled;
const configuredOutputDir = process.env.HBOX_WEB_OUTPUT_DIR;

if (requestedMockBuild !== mockTransportEnabled ||
    requestedMockBuild !== offlinePreviewEnabled) {
    throw new Error(
        'Mock builds require HBOX_BUILD_VARIANT=mock, ' +
        'NEXT_PUBLIC_DEVICE_TRANSPORT=mock and NEXT_PUBLIC_OFFLINE_PREVIEW=true together.',
    );
}

const nextConfig: NextConfig = {
    output: "export",   // 指定输出模式，export 表示导出静态文件，export 模式下，next 会生成一个 dist 目录，里面包含所有静态文件，使用这个模式的时候 要暂时删除 app/api 
    // Keep the normal Next.js development cache separate from deployable
    // exports. Mock exports must never be consumed by makefsdata.js.
    distDir: configuredOutputDir ||
        (isDevelopment ? '.next' : isMockBuild ? 'build-mock' : 'build'),
    // Hosted V2 builds must fail on type errors.
    typescript: {
        ignoreBuildErrors: false,
    },
    images: {
        unoptimized: true,
    },
    trailingSlash: true,
    // 禁用 telemetry   
    // telemetry: {
    //     enabled: false,
    // },
    compress: true,
    poweredByHeader: false,
    generateEtags: false,
    webpack(config) {
        // Next's persistent resolver cache does not include arbitrary build
        // environment variables. Keep Hosted and Mock alias resolutions in
        // separate cache generations so alternating builds cannot reuse the
        // other variant's entry module.
        if (config.cache && typeof config.cache === 'object') {
            config.cache.version = [
                config.cache.version,
                `hbox-build-variant:${isMockBuild ? 'mock' : 'hosted'}`,
            ].filter(Boolean).join('|');
        }
        config.resolve.alias = {
            ...(config.resolve.alias || {}),
            '@hbox/device-transport-runtime$': path.resolve(
                process.cwd(),
                isMockBuild
                    ? 'lib/device-transport/factory-runtime-mock.ts'
                    : 'lib/device-transport/factory-runtime-hosted.ts',
            ),
            '@hbox/build-variant-badge$': path.resolve(
                process.cwd(),
                isMockBuild
                    ? 'components/build-variant-badge-mock.tsx'
                    : 'components/build-variant-badge-hosted.tsx',
            ),
        };
        return config;
    },
};

export default withBundleAnalyzer({
    enabled: process.env.ANALYZE === 'true', // 是否启用分析
})(nextConfig);
