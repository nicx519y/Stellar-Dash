import type { NextConfig } from "next";
import withBundleAnalyzer from '@next/bundle-analyzer';
import TerserPlugin from 'terser-webpack-plugin';

const isDevelopment = process.env.NODE_ENV === 'development';
const isMockBuild = process.env.HBOX_BUILD_VARIANT === 'mock';
const isLegacyEmbeddedBuild = process.env.HBOX_BUILD_VARIANT === 'legacy';
const configuredOutputDir = process.env.HBOX_WEB_OUTPUT_DIR;

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
    webpack: (config, { isServer, dev }) => {
        // The hosted V2 site is served as a normal static Next.js export and
        // should retain Next's normal chunking/minifier.  The historical
        // single-bundle Terser pass is needed only by the explicitly selected
        // legacy embedded image; applying it to V2 made local/CI hosted builds
        // take many minutes and provided no device-side benefit.
        if (!isServer && !dev && isLegacyEmbeddedBuild) {
            // 禁用代码分割
            config.optimization = {
                minimize: true,
                minimizer: [
                    new TerserPlugin({
                        terserOptions: {
                            compress: {
                                drop_console: true,
                                drop_debugger: true,
                                passes: 3, // 压缩次数 1-2 默认和中等，3 是最大 时间显著增加
                                toplevel: true, // 压缩顶级函数
                                dead_code: true, // 删除未使用的代码
                                unsafe_arrows: true, // 压缩箭头函数
                                unsafe_math: true, // 压缩数学运算
                                unsafe_proto: true, // 压缩原型链
                                unsafe_undefined: true, // 压缩 undefined
                                inline: true, // 内联函数
                                collapse_vars: true, // 压缩变量
                                reduce_vars: true, // 压缩变量
                                reduce_funcs: true, // 压缩函数
                            },
                            output: {
                                comments: false,
                            },
                            mangle: true
                        },
                    }),
                ],
                // 关键改动：将所有代码强制打包到一个文件
                concatenateModules: true,
                splitChunks: false,  // 完全禁用代码分割
                runtimeChunk: false,
            };

            // 修改输出配置
            config.output = {
                ...config.output,
                filename: 'static/js/[name].[contenthash].js',
                chunkFilename: 'static/js/[name].[contenthash].js',
            };
        }
        return config;
    },
    compress: true,
    poweredByHeader: false,
    generateEtags: false,
};

export default withBundleAnalyzer({
    enabled: process.env.ANALYZE === 'true', // 是否启用分析
})(nextConfig);
