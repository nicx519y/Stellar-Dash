import { NextResponse } from 'next/server';
import crypto from 'crypto';
import { SessionStore } from '../session-store';

// 计算数据的SHA256校验和
function calculateSHA256(data: Buffer): string {
    return crypto.createHash('sha256').update(data).digest('hex');
}

// 模拟处理延迟
function simulateProcessingDelay(): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, 5)); // 5ms延迟
}

// 固件分片传输接口
export async function POST(request: Request) {
    try {
        // 检查Content-Type
        const contentType = request.headers.get('content-type');
        console.log('Request Content-Type:', contentType);
        
        // 尝试解析FormData
        let formData;
        try {
            formData = await request.formData();
        } catch (formDataError) {
            console.error('FormData parsing error:', formDataError);
            
            // 如果FormData解析失败，尝试其他方法
            const errorMessage = formDataError instanceof Error ? formDataError.message : String(formDataError);
            return NextResponse.json({
                errNo: 1,
                errorMessage: `FormData parsing failed: ${errorMessage}. Content-Type: ${contentType}`
            }, { status: 400 });
        }
        
        // 获取元数据
        const metadataStr = formData.get('metadata') as string;
        if (!metadataStr) {
            return NextResponse.json({
                errNo: 1,
                errorMessage: 'Missing metadata'
            }, { status: 400 });
        }

        let metadata;
        try {
            metadata = JSON.parse(metadataStr);
        } catch {
            return NextResponse.json({
                errNo: 1,
                errorMessage: 'Invalid metadata JSON format'
            }, { status: 400 });
        }

        // 验证必需的元数据字段
        const {
            session_id,
            component_name,
            chunk_index,
            total_chunks,
            target_address,
            chunk_size,
            chunk_offset,
            checksum
        } = metadata;

        if (!session_id || component_name === undefined || chunk_index === undefined || 
            total_chunks === undefined || !target_address || chunk_size === undefined || 
            chunk_offset === undefined || !checksum) {
            return NextResponse.json({
                errNo: 1,
                errorMessage: 'Missing required metadata fields'
            }, { status: 400 });
        }

        // 获取二进制数据
        const dataFile = formData.get('data') as File;
        if (!dataFile) {
            return NextResponse.json({
                errNo: 1,
                errorMessage: 'Missing binary data'
            }, { status: 400 });
        }

        // 读取二进制数据
        const arrayBuffer = await dataFile.arrayBuffer();
        const chunkData = Buffer.from(arrayBuffer);

        // 验证数据大小
        if (chunkData.length !== chunk_size) {
            return NextResponse.json({
                errNo: 1,
                errorMessage: `Chunk size mismatch: expected ${chunk_size}, received ${chunkData.length}`
            }, { status: 400 });
        }

        // 验证会话
        const session = await SessionStore.getSession(session_id);
        if (!session) {
            console.warn(`Session ${session_id} not found. This may be due to server restart or session expiration.`);
            return NextResponse.json({
                errNo: 2, // 使用特殊错误码表示会话不存在
                errorMessage: 'Session not found. The upgrade session may have expired or the server was restarted. Please create a new upgrade session.',
                errorCode: 'SESSION_NOT_FOUND'
            }, { status: 404 });
        }

        if (session.status !== 'active') {
            return NextResponse.json({
                errNo: 1,
                errorMessage: 'Session is not active'
            }, { status: 400 });
        }

        // 验证组件
        const componentData = session.components.get(component_name);
        if (!componentData) {
            return NextResponse.json({
                errNo: 1,
                errorMessage: 'Component not found in session'
            }, { status: 400 });
        }

        // 验证SHA256校验和
        const calculatedChecksum = calculateSHA256(chunkData);
        if (calculatedChecksum !== checksum) {
            return NextResponse.json({
                errNo: 1,
                errorMessage: `Checksum mismatch: expected ${checksum}, calculated ${calculatedChecksum}`
            }, { status: 400 });
        }

        // 验证地址格式（十六进制）
        if (!/^0x[0-9A-Fa-f]+$/.test(target_address)) {
            return NextResponse.json({
                errNo: 1,
                errorMessage: 'Invalid target address format'
            }, { status: 400 });
        }

        // 模拟处理延迟
        await simulateProcessingDelay();

        // 存储chunk数据
        if (componentData.totalChunks === 0) {
            componentData.totalChunks = total_chunks;
        } else if (componentData.totalChunks !== total_chunks) {
            return NextResponse.json({
                errNo: 1,
                errorMessage: 'Total chunks mismatch'
            }, { status: 400 });
        }

        // 检查chunk是否已经接收过
        if (componentData.chunks.has(chunk_index)) {
            console.warn(`Chunk ${chunk_index} for component ${component_name} already received, overwriting...`);
        } else {
            componentData.receivedChunks++;
        }

        // 存储chunk
        componentData.chunks.set(chunk_index, {
            data: chunkData,
            checksum: calculatedChecksum,
            targetAddress: target_address,
            chunkSize: chunk_size,
            chunkOffset: chunk_offset
        });

        // 更新接收大小
        componentData.receivedSize = Array.from(componentData.chunks.values())
            .reduce((total, chunk) => total + chunk.chunkSize, 0);

        // 立即保存chunk数据到持久化存储
        await SessionStore.forceSave();

        // 计算进度
        const componentProgress = (componentData.receivedChunks / componentData.totalChunks) * 100;
        const isComponentComplete = componentData.receivedChunks === componentData.totalChunks;

        console.log(`Received chunk ${chunk_index}/${total_chunks} for component ${component_name}`);
        console.log(`Target address: ${target_address}, offset: ${chunk_offset}, size: ${chunk_size}`);
        console.log(`Component progress: ${componentProgress.toFixed(1)}%`);

        // 如果组件完成，验证完整性
        if (isComponentComplete) {
            console.log(`Component ${component_name} upload completed!`);
            
            // 验证所有chunk是否连续
            const sortedChunks = Array.from(componentData.chunks.entries())
                .sort(([a], [b]) => a - b);
            
            let expectedOffset = 0;
            for (const [chunkIndex, chunk] of sortedChunks) {
                if (chunk.chunkOffset !== expectedOffset) {
                    console.warn(`Chunk offset mismatch at index ${chunkIndex}: expected ${expectedOffset}, got ${chunk.chunkOffset}`);
                }
                expectedOffset += chunk.chunkSize;
            }
            
            if (componentData.receivedSize !== componentData.totalSize) {
                console.warn(`Component ${component_name} size mismatch: expected ${componentData.totalSize}, received ${componentData.receivedSize}`);
            }
        }

        return NextResponse.json({
            errNo: 0,
            data: {
                status: 'success',
                message: 'Chunk received and verified successfully',
                progress: {
                    component_name: component_name,
                    chunk_index: chunk_index,
                    total_chunks: total_chunks,
                    component_progress: Math.round(componentProgress),
                    received_size: componentData.receivedSize,
                    total_size: componentData.totalSize,
                    is_component_complete: isComponentComplete
                }
            }
        });

    } catch (error) {
        console.error('Firmware chunk upload error:', error);
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Internal server error'
        }, { status: 500 });
    }
} 