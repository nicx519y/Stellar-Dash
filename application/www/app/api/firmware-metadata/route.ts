import { NextResponse } from 'next/server';

// 固件元数据接口
// 获取当前固件的版本、构建信息、硬件信息等元数据
export async function GET() {
    try {
        // TODO: 从实际固件中读取版本信息和manifest.json
        // 这里需要根据实际固件实现获取元数据的逻辑

        const device_id = "1234567890";
        
        const firmwareMetadata = {
            // 基本版本信息 (来自manifest.json)
            version: "0.0.0",
            slot: "A", // 当前运行的槽位
            build_date: "2024-12-08 14:30:22",
            // 组件信息 (来自manifest.json的components数组)
            components: [
                {
                    name: "application",
                    file: "application_slot_a.hex",
                    address: "0x90000000", // 槽A地址
                    size: 1048576,
                    sha256: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    status: "active" // active, inactive, corrupted
                },
                {
                    name: "webresources",
                    file: "webresources.bin", 
                    address: "0x90100000", // 槽A WebResources地址
                    size: 1572864,
                    sha256: "a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3",
                    status: "active"
                },
                {
                    name: "adc_mapping",
                    file: "slot_a_adc_mapping.bin",
                    address: "0x90280000", // 槽A ADC Mapping地址  
                    size: 131072,
                    sha256: "2cf24dba4f21d4288bbc89d4f52b82a864c2bf37d742c6f2c5e5e29a4f5c2b3e",
                    status: "active"
                }
            ],
        };
        
        return NextResponse.json({
            errNo: 0,
            data: {
                device_id: device_id,
                firmware: firmwareMetadata
            },
        });
    } catch (error) {
        console.error('获取固件元数据失败:', error);
        return NextResponse.json({ 
            errNo: 1, 
            errorMessage: 'Failed to get firmware metadata' 
        }, { 
            status: 500 
        });
    }
} 