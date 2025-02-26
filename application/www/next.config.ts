import type { NextConfig } from "next";
import withBundleAnalyzer from '@next/bundle-analyzer';
import TerserPlugin from 'terser-webpack-plugin';

const nextConfig: NextConfig = {
    // output: "export",   // 指定输出模式，export 表示导出静态文件，export 模式下，next 会生成一个 dist 目录，里面包含所有静态文件，使用这个模式的时候 要暂时删除 app/api 
    // distDir: 'build',
    // 忽略 build 错误
    typescript: {
        ignoreBuildErrors: true,
    },
    // 禁用 telemetry   
    // telemetry: {
    //     enabled: false,
    // },
    webpack: (config, { isServer, dev }) => {
        // 只在生产环境下应用优化配置
        if (!isServer && !dev) {
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
    async rewrites() {
        return [
            {
                source: '/keys',
                destination: '/',
            },
            {
                source: '/hotkeys',
                destination: '/',
            },
            {
                source: '/leds',
                destination: '/',
            },
            {
                source: '/rapid-trigger',
                destination: '/',
            },
            {
                source: '/switch-marking',
                destination: '/',
            },
            {
                source: '/firmware',
                destination: '/',
            },
        ];
    },
};

export default withBundleAnalyzer({
    enabled: process.env.ANALYZE === 'true', // 是否启用分析
})(nextConfig);