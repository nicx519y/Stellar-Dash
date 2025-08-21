module.exports = {
  apps: [{
    name: 'hbox-firmware-server',
    script: 'start.js',
    cwd: '/opt/hbox-server',
    instances: 1,
    exec_mode: 'fork',
    autorestart: true,
    watch: false,
    max_memory_restart: '1G',
    node_args: '--max-old-space-size=1024',
    
    // 环境变量
    env: {
      NODE_ENV: 'production',
      PORT: 3000,
      SERVER_URL: 'https://firmware.st-dash.com'
    },
    
    // 日志配置
    log_file: './logs/combined.log',
    out_file: './logs/out.log',
    error_file: './logs/err.log',
    log_date_format: 'YYYY-MM-DD HH:mm:ss Z',
    
    // 日志文件大小限制（10MB）
    max_size: '10M',
    
    // 保留的日志文件数量
    retain: '5',
    
    // 日志轮转配置
    log_type: 'json',
    
    // 进程管理
    min_uptime: '10s',
    max_restarts: 10,
    
    // 监控配置
    pmx: true,
    
    // 错误处理
    kill_timeout: 5000,
    listen_timeout: 3000,
    
    // 启动配置
    merge_logs: true,
    time: true
  }]
}; 