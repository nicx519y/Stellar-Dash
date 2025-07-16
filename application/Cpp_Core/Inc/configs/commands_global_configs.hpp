#pragma once

#include "configs/websocket_message.hpp"

/**
 * @brief WebSocket命令处理器 - 获取全局配置
 * @param request 上行消息对象
 * @return 下行响应消息
 * 
 * 对应HTTP接口: GET /api/global-config
 * 
 * 请求格式:
 * {
 *   "cid": 1,
 *   "command": "get_global_config",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 1,
 *   "command": "get_global_config", 
 *   "errNo": 0,
 *   "data": {
 *     "globalConfig": {
 *       "inputMode": "XINPUT",
 *       "autoCalibrationEnabled": true,
 *       "manualCalibrationActive": false
 *     }
 *   }
 * }
 */
WebSocketDownstreamMessage handle_websocket_get_global_config(const WebSocketUpstreamMessage& request);

/**
 * @brief WebSocket命令处理器 - 更新全局配置
 * @param request 上行消息对象
 * @return 下行响应消息
 * 
 * 对应HTTP接口: POST /api/update-global-config
 * 
 * 请求格式:
 * {
 *   "cid": 2,
 *   "command": "update_global_config",
 *   "params": {
 *     "globalConfig": {
 *       "inputMode": "PS4",
 *       "autoCalibrationEnabled": false
 *     }
 *   }
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 2,
 *   "command": "update_global_config",
 *   "errNo": 0,
 *   "data": {
 *     "globalConfig": {
 *       "inputMode": "PS4",
 *       "autoCalibrationEnabled": false,
 *       "manualCalibrationActive": false
 *     }
 *   }
 * }
 */
WebSocketDownstreamMessage handle_websocket_update_global_config(const WebSocketUpstreamMessage& request);

/**
 * @brief WebSocket命令处理器 - 获取快捷键配置
 * @param request 上行消息对象
 * @return 下行响应消息
 * 
 * 对应HTTP接口: GET /api/hotkeys-config
 * 
 * 请求格式:
 * {
 *   "cid": 3,
 *   "command": "get_hotkeys_config",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 3,
 *   "command": "get_hotkeys_config",
 *   "errNo": 0,
 *   "data": {
 *     "hotkeysConfig": [
 *       {
 *         "key": 0,
 *         "action": "WebConfigMode",
 *         "isHold": false,
 *         "isLocked": true
 *       }
 *     ]
 *   }
 * }
 */
WebSocketDownstreamMessage handle_websocket_get_hotkeys_config(const WebSocketUpstreamMessage& request);

/**
 * @brief WebSocket命令处理器 - 更新快捷键配置
 * @param request 上行消息对象
 * @return 下行响应消息
 * 
 * 对应HTTP接口: POST /api/update-hotkeys-config
 * 
 * 请求格式:
 * {
 *   "cid": 4,
 *   "command": "update_hotkeys_config",
 *   "params": {
 *     "hotkeysConfig": [
 *       {
 *         "key": 0,
 *         "action": "XInputMode",
 *         "isHold": false,
 *         "isLocked": false
 *       }
 *     ]
 *   }
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 4,
 *   "command": "update_hotkeys_config",
 *   "errNo": 0,
 *   "data": {
 *     "hotkeysConfig": [
 *       {
 *         "key": 0,
 *         "action": "XInputMode",
 *         "isHold": false,
 *         "isLocked": false
 *       }
 *     ]
 *   }
 * }
 */
WebSocketDownstreamMessage handle_websocket_update_hotkeys_config(const WebSocketUpstreamMessage& request);

/**
 * @brief WebSocket命令处理器 - 系统重启
 * @param request 上行消息对象
 * @return 下行响应消息
 * 
 * 对应HTTP接口: POST /api/reboot
 * 
 * 请求格式:
 * {
 *   "cid": 5,
 *   "command": "reboot",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 5,
 *   "command": "reboot",
 *   "errNo": 0,
 *   "data": {
 *     "message": "System is rebooting"
 *   }
 * }
 */
WebSocketDownstreamMessage handle_websocket_reboot(const WebSocketUpstreamMessage& request); 