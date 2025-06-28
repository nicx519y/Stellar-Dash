import { NextResponse } from 'next/server';
import { SessionStore } from './session-store';

// 固件升级会话管理接口
export async function POST(request: Request) {
    try {
        const body = await request.json();
        const { action, session_id, manifest } = body;

        switch (action) {
            case 'create':
                // 创建新的升级会话
                if (!session_id || !manifest) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Missing session_id or manifest'
                    }, { status: 400 });
                }

                // 检查会话是否已存在
                const existingSession = await SessionStore.getSession(session_id);
                if (existingSession) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Session already exists'
                    }, { status: 409 });
                }

                // 创建新会话
                const newSession = await SessionStore.createSession(session_id, manifest);
                console.log(`Created upgrade session: ${session_id}`);

                return NextResponse.json({
                    errNo: 0,
                    data: {
                        session_id: newSession.sessionId,
                        status: newSession.status,
                        message: 'Upgrade session created successfully'
                    }
                });

            case 'complete':
                // 完成升级会话
                if (!session_id) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Missing session_id'
                    }, { status: 400 });
                }

                const completeSuccess = await SessionStore.updateSessionStatus(session_id, 'completed');
                if (!completeSuccess) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Session not found'
                    }, { status: 404 });
                }

                console.log(`Completed upgrade session: ${session_id}`);
                return NextResponse.json({
                    errNo: 0,
                    data: {
                        session_id: session_id,
                        status: 'completed',
                        message: 'Upgrade session completed successfully'
                    }
                });

            case 'abort':
                // 中止升级会话
                if (!session_id) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Missing session_id'
                    }, { status: 400 });
                }

                const abortSuccess = await SessionStore.updateSessionStatus(session_id, 'aborted');
                if (!abortSuccess) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Session not found'
                    }, { status: 404 });
                }

                console.log(`Aborted upgrade session: ${session_id}`);
                return NextResponse.json({
                    errNo: 0,
                    data: {
                        session_id: session_id,
                        status: 'aborted',
                        message: 'Upgrade session aborted successfully'
                    }
                });

            case 'status':
                // 查询会话状态
                if (!session_id) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Missing session_id'
                    }, { status: 400 });
                }

                const session = await SessionStore.getSession(session_id);
                if (!session) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Session not found'
                    }, { status: 404 });
                }

                // 计算总体进度
                let totalProgress = 0;
                const totalComponents = session.components.size;
                
                if (totalComponents > 0) {
                    for (const [, componentData] of session.components) {
                        if (componentData.totalChunks > 0) {
                            totalProgress += (componentData.receivedChunks / componentData.totalChunks) * 100;
                        }
                    }
                    totalProgress = totalProgress / totalComponents;
                }

                return NextResponse.json({
                    errNo: 0,
                    data: {
                        session_id: session.sessionId,
                        status: session.status,
                        progress: Math.round(totalProgress),
                        created_at: session.createdAt,
                        components: Array.from(session.components.entries()).map(([name, data]) => ({
                            name,
                            total_chunks: data.totalChunks,
                            received_chunks: data.receivedChunks,
                            total_size: data.totalSize,
                            received_size: data.receivedSize,
                            progress: data.totalChunks > 0 ? Math.round((data.receivedChunks / data.totalChunks) * 100) : 0
                        }))
                    }
                });

            default:
                return NextResponse.json({
                    errNo: 1,
                    errorMessage: 'Invalid action'
                }, { status: 400 });
        }

    } catch (error) {
        console.error('Firmware upgrade session error:', error);
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Internal server error'
        }, { status: 500 });
    }
}

// 清理会话的GET接口
export async function DELETE(request: Request) {
    try {
        const url = new URL(request.url);
        const action = url.searchParams.get('action');
        const sessionId = url.searchParams.get('session_id');

        switch (action) {
            case 'cleanup':
                // 清理指定会话
                if (!sessionId) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Missing session_id'
                    }, { status: 400 });
                }

                const cleanupSuccess = await SessionStore.cleanupSession(sessionId);
                if (!cleanupSuccess) {
                    return NextResponse.json({
                        errNo: 1,
                        errorMessage: 'Session not found'
                    }, { status: 404 });
                }

                return NextResponse.json({
                    errNo: 0,
                    data: {
                        message: `Session ${sessionId} cleaned up successfully`
                    }
                });

            case 'cleanup-completed':
                // 清理所有已完成的会话
                const cleanedCount = await SessionStore.cleanupCompletedSessions();
                return NextResponse.json({
                    errNo: 0,
                    data: {
                        message: `Cleaned up ${cleanedCount} completed sessions`
                    }
                });

            case 'clear-all':
                // 清理所有会话
                await SessionStore.clearAllSessions();
                return NextResponse.json({
                    errNo: 0,
                    data: {
                        message: 'All sessions cleared successfully'
                    }
                });

            default:
                return NextResponse.json({
                    errNo: 1,
                    errorMessage: 'Invalid action'
                }, { status: 400 });
        }

    } catch (error) {
        console.error('Session cleanup error:', error);
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Internal server error'
        }, { status: 500 });
    }
} 