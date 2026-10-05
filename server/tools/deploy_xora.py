#!/usr/bin/env python3
"""XORA single-host deployment. Python 3.10+, OpenSSH; remote Linux/systemd.

Local package/config operations never connect to a server. Remote operations
are explicit subcommands. This file is also the remote runner (sent via SSH
stdin), so both sides execute the same implementation without shell templates.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
import uuid

ROOT = Path('/opt/xora')
STATE = Path('/var/lib/xora')
ETC = Path('/etc/xora')
BACKUPS = Path('/var/backups/xora')
UNIT = Path('/etc/systemd/system/xora-server.service')
SERVICE = 'xora-server.service'
RELEASE_RE = r'[0-9]{8}-[0-9]{6}-[a-f0-9]{8}'
PAGES = ('index.html', 'global/index.html', 'keys/index.html', 'lighting/index.html',
         'buttons-performance/index.html', 'switch-marking/index.html',
         'firmware/index.html', 'view-logs/index.html', 'auth/verify/index.html',
         'admin/users/index.html', 'admin/firmware/index.html', 'admin/resources/index.html',
         'admin/images/index.html', 'admin/service-tokens/index.html',
         'admin/firmware/detail/index.html', 'firmware/releases/index.html')
MOCK_MARKERS = (b'HBOX-V2-MOCK-0001', b'mock-session', b'MOCK DEVICE',
                b'XORA V2 Mock Device', b'HBox V2 Mock Device')


def need(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else hashlib.sha256(stream.read()).hexdigest()


def stop_process(process):
    if process.poll() is not None:
        return
    if os.name == 'nt':
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    else:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
    process.wait(timeout=10)


def run(args, *, cwd=None, timeout=120, capture=False, data=None, env=None):
    """Bound every owned process tree, while ordinary output stays live."""
    started = time.monotonic()
    print(f'[START] {Path(str(args[0])).name}; timeout={timeout}s', flush=True)
    process = subprocess.Popen(
        [str(x) for x in args], cwd=cwd, env=env,
        stdin=subprocess.PIPE if data is not None else subprocess.DEVNULL,
        stdout=subprocess.PIPE if capture else None,
        start_new_session=os.name != 'nt')
    print(f'[PID] {process.pid}', flush=True)
    try:
        output, _ = process.communicate(data, timeout=timeout)
    except BaseException:
        stop_process(process)
        raise
    print(f'[END] code={process.returncode}; elapsed={time.monotonic()-started:.1f}s', flush=True)
    need(process.returncode == 0, f'{Path(str(args[0])).name} failed (exit {process.returncode})')
    return (output or b'').decode('utf-8').strip()


def validate_config(config):
    defaults = dict(ssh_port=22, ssh_key='', port=3000, node='/usr/bin/node')
    defaults.update(config)
    defaults.setdefault('admin_domain', defaults.get('domain'))
    need(set(defaults) == {'host', 'user', 'ssh_port', 'ssh_key', 'domain', 'admin_domain', 'port', 'node', 'email_from'},
         'Config must contain host, user, domain, email_from and only documented fields')
    for name in ('host', 'domain', 'admin_domain'):
        need(isinstance(defaults[name], str) and re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?', defaults[name]), f'Invalid {name}')
    need('.' in defaults['domain'], 'Use a public DNS domain')
    need('.' in defaults['admin_domain'], 'Use a public admin DNS domain')
    need(re.fullmatch(r'[a-z_][a-z0-9_-]*', defaults['user']), 'Invalid SSH user')
    for name in ('port', 'ssh_port'):
        need(type(defaults[name]) is int and 1 <= defaults[name] <= 65535, f'Invalid {name}')
    need(defaults['port'] >= 1024, 'Node port must be >= 1024')
    need(re.fullmatch(r'/[A-Za-z0-9_./-]+', defaults['node']) and '..' not in defaults['node'].split('/'), 'Invalid absolute Node path')
    need(re.fullmatch(r'[A-Za-z0-9 ._-]+ <[A-Za-z0-9._+-]+@[A-Za-z0-9.-]+>', defaults['email_from']), 'email_from must look like XORA <no-reply@auth.example.com>')
    need(isinstance(defaults['ssh_key'], str) and '\n' not in defaults['ssh_key'], 'Invalid ssh_key')
    return defaults


def environment(config):
    origin = 'https://' + config['domain']
    origins = ','.join(dict.fromkeys([origin, 'https://' + config['admin_domain']]))
    return dict(NODE_ENV='production', PORT=str(config['port']), LISTEN_HOST='127.0.0.1',
                TRUST_PROXY_HOPS='1', DOMAIN_NAME=config['domain'], SERVER_URL=origin, DOMAIN_URL=origin,
                WEB_CONFIG_ORIGINS=origins, WEB_CONFIG_STATIC_DIR=str(ROOT / 'current/webconfig'),
                WEB_CONFIG_REQUIRE_STATIC='1', HBOX_SERVER_DATA_DIR=str(STATE / 'data'),
                HBOX_SERVER_UPLOAD_DIR=str(STATE / 'uploads'), HBOX_GALLERY_ASSET_DIR=str(STATE / 'gallery-assets'),
                FIRMWARE_RELEASE_PUBLIC_KEY_FILE=str(ETC / 'keys/firmware-release-public.pem'),
                USER_AUTH_ENABLED='1', USER_AUTH_PUBLIC_ORIGIN=origin, USER_AUTH_ALLOWED_ORIGINS=origins,
                USER_AUTH_EMAIL_FROM=config['email_from'],
                RESEND_API_KEY_FILE=str(ETC / 'secrets/resend-api-key'))


def templates(config):
    env = ''.join(f'{k}={json.dumps(v)}\n' for k, v in environment(config).items())
    unit = f'''[Unit]
Description=XORA WebConfig and administrator API
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=xora
Group=xora
WorkingDirectory={ROOT}/current/server
EnvironmentFile={ETC}/server.env
ExecStart={config['node']} {ROOT}/current/server/src/server.js
Restart=on-failure
RestartSec=3
TimeoutStopSec=30
UMask=0027
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=strict
ReadWritePaths={STATE}

[Install]
WantedBy=multi-user.target
'''
    nginx = '# Generated XORA configuration. Install only after both TLS certificates exist.\n'
    for domain in dict.fromkeys((config['domain'], config['admin_domain'])):
        redirects = ''
        if config['domain'] != config['admin_domain']:
            if domain == config['domain']:
                redirects = f'''    location ~ ^/admin(?:/|$) {{
        return 302 https://{config['admin_domain']}$request_uri;
    }}
'''
            else:
                redirects = f'''    location = / {{
        return 302 https://{config['admin_domain']}/admin/users/;
    }}
    location ~ ^/(?:global|keys|lighting|buttons-performance|switch-marking|firmware|view-logs|webhid-trace|webhid-benchmark)(?:/|$) {{
        return 302 https://{config['domain']}$request_uri;
    }}
'''
        nginx += f'''server {{
    listen 80;
    server_name {domain};
    return 301 https://{domain}$request_uri;
}}
server {{
    listen 443 ssl;
    server_name {domain};
    ssl_certificate /etc/letsencrypt/live/{domain}/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/{domain}/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    client_max_body_size 64m;
{redirects}
    location / {{
        proxy_pass http://127.0.0.1:{config['port']};
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $remote_addr;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header Connection "";
        proxy_read_timeout 120s;
        proxy_send_timeout 120s;
        proxy_cache off;
    }}
}}
'''
    return {'server.env': env, 'xora-server.service': unit, 'nginx.conf': nginx}


def safe_name(name):
    parts = PurePosixPath(name).parts
    need(parts and str(PurePosixPath(name)) == name and not name.startswith('/') and '\\' not in name
         and ':' not in name and not any(ord(c) < 32 for c in name)
         and not any(p in ('.', '..') or p.startswith('.') for p in parts), f'Unsafe bundle path: {name}')
    need(not any(p in ('node_modules', 'data', 'uploads', 'gallery-assets') for p in parts)
         and not name.endswith(('.pem', '.key', '.sqlite3', '.db')), f'Private/runtime file in bundle: {name}')
    need(name == 'manifest.json' or name in ('common/uimg-jpeg.cjs', 'common/xora-resource-codec.cjs') or name in ('server/package.json', 'server/package-lock.json')
         or name == 'resources/default-switch-mapping.json'
         or name.startswith(('server/src/', 'server/scripts/', 'webconfig/', 'resources/xora/')), f'Unexpected bundle path: {name}')


def verify_export(root):
    for page in PAGES:
        need((root / page).is_file(), f'Missing Hosted page: {page}')
    need((root / '_next/static').is_dir(), 'Missing Hosted chunks')
    chunks = set()
    for path in root.rglob('*'):
        need(not path.is_symlink() and not (hasattr(path, 'is_junction') and path.is_junction()), f'Linked export path: {path}')
        if path.is_file() and path.suffix in ('.html', '.js'):
            content = path.read_bytes()
            need(not any(marker in content for marker in MOCK_MARKERS), f'Mock code found in {path.name}')
            if path.suffix == '.html':
                chunks.update(re.findall(rb'(?:src|href)=["\'](/_next/static/[^"\']+\.js)(?:\?[^"\']*)?["\']', content))
    need(chunks, 'Hosted HTML has no referenced JavaScript chunks')
    for chunk in chunks:
        need((root / chunk.decode().lstrip('/')).is_file(), 'Hosted HTML references a missing chunk')


def build_package(repo, output, skip_build=False):
    web = repo / 'application/www'
    if not skip_build:
        npm = shutil.which('npm')
        need(npm, 'npm is not installed')
        run([npm, 'run', 'build:hosted'], cwd=web, timeout=600)
    verify_export(web / 'build')
    release = time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8]
    output.mkdir(parents=True, exist_ok=True)
    stage = output / release
    stage.mkdir()
    sources = ('server/src', 'server/scripts', 'server/package.json', 'server/package-lock.json', 'common/uimg-jpeg.cjs', 'common/xora-resource-codec.cjs', 'resources/xora', 'resources/default-switch-mapping.json')
    for item in sources + ('application/www/build',):
        source = repo / item
        need(source.exists(), f'Missing source: {item}')
        need(not source.is_symlink() and not (hasattr(source, 'is_junction') and source.is_junction()), f'Linked source root: {source}')
        files = source.rglob('*') if source.is_dir() else [source]
        for file in files:
            need(not file.is_symlink() and not (hasattr(file, 'is_junction') and file.is_junction()), f'Linked source: {file}')
            if not file.is_file():
                continue
            relative = ('webconfig/' + file.relative_to(source).as_posix()) if item == 'application/www/build' else file.relative_to(repo).as_posix()
            safe_name(relative)
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, target)
    (stage / 'webconfig/deployment.json').write_text(json.dumps({'release': release}), encoding='utf-8')
    manifest = {'schema': 1, 'release': release, 'files': {p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob('*')) if p.is_file()}}
    (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    archive = output / f'xora-{release}.tar.gz'
    with tarfile.open(archive, 'x:gz') as tar:
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                tar.add(path, arcname=path.relative_to(stage).as_posix(), recursive=False)
    checksum = digest(archive)
    archive.with_suffix(archive.suffix + '.sha256').write_text(f'{checksum}  {archive.name}\n', encoding='ascii')
    inspect_bundle(archive)
    print(f'PACKAGE={archive}\nSHA256={checksum}\nRELEASE={release}', flush=True)
    return archive


def inspect_bundle(archive, destination=None):
    """Do not use extractall: reject links, traversal, duplicates and extras."""
    with tarfile.open(archive, 'r:gz') as tar:
        members = tar.getmembers()
        need(len(members) <= 30000 and sum(m.size for m in members) <= 512 * 1024 * 1024, 'Bundle exceeds limits')
        names = [m.name for m in members]
        need(len(names) == len(set(names)), 'Duplicate bundle entry')
        for member in members:
            safe_name(member.name)
            need(member.isfile(), 'Bundle may contain regular files only')
        need('manifest.json' in names and tar.getmember('manifest.json').size < 4 * 1024 * 1024, 'Missing/oversized manifest')
        manifest = json.load(tar.extractfile('manifest.json'))
        need(manifest.get('schema') == 1 and re.fullmatch(RELEASE_RE, manifest.get('release', '')), 'Invalid deployment manifest')
        need(isinstance(manifest.get('files'), dict) and set(manifest['files']) == set(names) - {'manifest.json'}, 'Manifest file set mismatch')
        required = {'server/src/server.js', 'server/package.json', 'server/package-lock.json', 'server/scripts/account-role.js',
                    'common/uimg-jpeg.cjs', 'common/xora-resource-codec.cjs', 'resources/xora/key-static.xora-resource.json',
                    'resources/default-switch-mapping.json', 'webconfig/deployment.json'} | {'webconfig/' + page for page in PAGES}
        need(required <= set(names), 'Incomplete deployment bundle')
        for member in members:
            data = tar.extractfile(member).read()
            if member.name != 'manifest.json':
                need(hashlib.sha256(data).hexdigest() == manifest['files'][member.name], f'Hash mismatch: {member.name}')
            if member.name.startswith('webconfig/') and member.name.endswith(('.html', '.js')):
                need(not any(marker in data for marker in MOCK_MARKERS), 'Mock artifact rejected')
            if member.name == 'webconfig/deployment.json':
                need(json.loads(data) == {'release': manifest['release']}, 'Release marker mismatch')
            if destination is not None:
                target = destination / member.name
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as stream:
                    stream.write(data)
                target.chmod(0o644)
    return manifest


def ssh_args(config):
    args = ['ssh', '-p', str(config['ssh_port']), '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
            '-o', 'ConnectTimeout=10', '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=3']
    if config['ssh_key']:
        args += ['-i', str(Path(config['ssh_key']).expanduser())]
    return args + [config['user'] + '@' + config['host']]


def remote_call(config, action, **extra):
    payload = {'config': config, 'action': action, **extra}
    payload['config'] = {**config, 'ssh_key': ''}  # No local credential paths on server.
    token = base64.urlsafe_b64encode(json.dumps(payload).encode()).decode()
    command = shlex.join(['sudo', '-n', 'python3', '-', '--remote', token])
    return run(ssh_args(config) + [command], timeout=1800, data=Path(__file__).read_bytes())


def upload_deploy(config, archive):
    manifest = inspect_bundle(archive)
    remote_call(config, 'check')
    incoming = '/tmp/xora-deploy-' + uuid.uuid4().hex
    run(ssh_args(config) + [shlex.join(['mkdir', '-m', '700', incoming])], timeout=30)
    scp = ['scp', '-P', str(config['ssh_port']), '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes', '-o', 'ConnectTimeout=10']
    if config['ssh_key']:
        scp += ['-i', str(Path(config['ssh_key']).expanduser())]
    run(scp + [str(archive.resolve()), f"{config['user']}@{config['host']}:{incoming}/bundle.tar.gz"], timeout=600)
    # Keep an interrupted upload for diagnosis; never retry a remote mutation blindly.
    remote_call(config, 'deploy', archive=incoming + '/bundle.tar.gz', sha256=digest(archive), release=manifest['release'])


def read_env(path):
    values = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        key, sep, value = line.partition('=')
        need(sep and re.fullmatch(r'[A-Z][A-Z0-9_]*', key) and key not in values, 'Invalid/duplicate environment entry')
        values[key] = json.loads(value) if value.startswith('"') else value
        need(isinstance(values[key], str) and '\n' not in values[key], 'Invalid environment value')
    return values


def remote_setup(config):
    import pwd
    try:
        account = pwd.getpwnam('xora')
    except KeyError:
        run(['useradd', '--system', '--user-group', '--home-dir', str(STATE), '--shell', '/usr/sbin/nologin', 'xora'])
        account = pwd.getpwnam('xora')
    need(account.pw_uid != 0, 'xora must be an unprivileged service account')
    for path in (ROOT, ROOT / 'releases', ETC, ETC / 'keys', ETC / 'secrets', STATE, BACKUPS):
        need(not path.is_symlink(), f'Refusing linked managed directory: {path}')
        path.mkdir(parents=True, exist_ok=True)
    BACKUPS.chmod(0o700)
    for name in ('data', 'uploads', 'gallery-assets', 'npm-cache'):
        path = STATE / name
        need(not path.is_symlink(), f'Refusing linked data directory: {path}')
        path.mkdir(exist_ok=True)
        os.chown(path, account.pw_uid, account.pw_gid)
        path.chmod(0o750)
    for path in (ETC, ETC / 'keys', ETC / 'secrets'):
        os.chown(path, 0, account.pw_gid)
        path.chmod(0o750)
    for name, text in templates(config).items():
        path = UNIT if name.endswith('.service') else ETC / name
        if path.exists():
            need(not path.is_symlink() and path.read_text() == text, f'Existing {path} differs; reconcile it manually; nothing overwritten')
        else:
            with path.open('x', encoding='utf-8', newline='\n') as stream:
                stream.write(text)
            path.chmod(0o600 if name == 'server.env' else 0o644)
    run(['systemctl', 'daemon-reload'])
    print(f'Setup complete. Provision keys in {ETC}; install {ETC}/nginx.conf after TLS setup. Service not started.')


def remote_check(config):
    for path in (ROOT, ROOT / 'releases', STATE, ETC, BACKUPS):
        need(path.is_dir() and not path.is_symlink(), f'Missing/linked managed directory: {path}')
    for executable in ('systemctl', 'runuser', 'npm', 'nginx'):
        need(shutil.which(executable), f'Missing server executable: {executable}')
    need(UNIT.is_file() and UNIT.read_text() == templates(config)['xora-server.service'], 'Managed systemd unit missing/different; run setup or reconcile manually')
    env = read_env(ETC / 'server.env')
    need(env == environment(config), 'server.env differs from deployment config; reconcile before deploying')
    for name in ('data', 'uploads', 'gallery-assets', 'npm-cache'):
        path = STATE / name
        need(path.is_dir() and not path.is_symlink(), f'Missing/linked data path: {path}')
        run(['runuser', '-u', 'xora', '--', 'test', '-w', str(path)])
    for key in ('RESEND_API_KEY_FILE', 'FIRMWARE_RELEASE_PUBLIC_KEY_FILE'):
        file = Path(env[key])
        need(file.is_file() and file.stat().st_size > 0, f'Missing {key} file')
        run(['runuser', '-u', 'xora', '--', 'test', '-r', str(file)])
    need(Path(config['node']).with_name('npm').is_file(), 'npm must be installed alongside the configured Node binary')
    version = run(['runuser', '-u', 'xora', '--', config['node'], '--version'], capture=True)
    need(int(version.lstrip('v').split('.')[0]) >= 22, 'Deploy server requires Node.js >= 22; use a supported LTS')
    run([config['node'], '-e', "const c=require('crypto'),f=require('fs');const k=c.createPublicKey(f.readFileSync(process.argv[1]));if(k.asymmetricKeyType!=='ec'||k.asymmetricKeyDetails?.namedCurve!=='prime256v1')process.exit(2)", env['FIRMWARE_RELEASE_PUBLIC_KEY_FILE']])
    run(['nginx', '-t'])
    print('Server prerequisites OK (DNS, actual email delivery and hardware not tested).')
    return env


def response(base, path, expected=200, *, follow_redirects=True):
    request = urllib.request.Request(base + path, headers={'Cache-Control': 'no-cache'})
    try:
        if follow_redirects:
            result = urllib.request.urlopen(request, timeout=5)
        else:
            class NoRedirect(urllib.request.HTTPRedirectHandler):
                def redirect_request(self, req, fp, code, msg, headers, newurl):
                    return None
            result = urllib.request.build_opener(NoRedirect).open(request, timeout=5)
    except urllib.error.HTTPError as error:
        result = error
    with result:
        need(result.status == expected, f'HTTP check failed: {path}, status={result.status}')
        return result.read(), result.headers


def probe(base, release, pages=PAGES):
    marker, _ = response(base, '/deployment.json?release=' + release)
    need(json.loads(marker) == {'release': release}, 'Unexpected deployed release (proxy/cache/old service)')
    health, _ = response(base, '/health')
    need(json.loads(health).get('status') == 'ok', 'Health API not ready')
    session, _ = response(base, '/api/auth/session')
    need(json.loads(session).get('registrationEnabled') is True, 'Email authentication disabled')
    response(base, '/api/admin/profile', 401)
    for page in pages:
        url = '/' if page == 'index.html' else '/' + page.removesuffix('index.html')
        html, headers = response(base, url)
        need('text/html' in headers.get('Content-Type', '') and html, f'Missing HTML: {url}')
        need('sha256-' in headers.get('Content-Security-Policy', ''), f'Missing CSP hashes: {url}')
        need('hid=(self)' in headers.get('Permissions-Policy', ''), f'Missing WebHID policy: {url}')


def probe_sites(config, release):
    config_origin = 'https://' + config['domain']
    admin_origin = 'https://' + config['admin_domain']
    if config_origin == admin_origin:
        probe(config_origin, release)
        return
    probe(config_origin, release, tuple(p for p in PAGES if not p.startswith('admin/')))
    probe(admin_origin, release, tuple(p for p in PAGES if p.startswith(('admin/', 'auth/'))))
    for base, path, expected in ((config_origin, '/admin/users/', admin_origin + '/admin/users/'),
                                 (admin_origin, '/', admin_origin + '/admin/users/'),
                                 (admin_origin, '/global/', config_origin + '/global/')):
        _, headers = response(base, path, 302, follow_redirects=False)
        need(headers.get('Location') == expected, f'Unexpected domain redirect: {base}{path}')


def wait_ready(base, release, process=None):
    deadline = time.monotonic() + 45
    while True:
        if process is not None:
            need(process.poll() is None, 'Candidate process exited; inspect preflight.log')
        try:
            probe(base, release)
            return
        except (OSError, ValueError, RuntimeError) as error:
            if time.monotonic() >= deadline:
                raise RuntimeError(f'Readiness timed out: {error}') from error
            time.sleep(1)


def smoke_candidate(config, release_dir, env):
    import pwd
    account = pwd.getpwnam('xora')
    temporary = Path(tempfile.mkdtemp(prefix='.deploy-check-', dir=STATE))
    os.chown(temporary, account.pw_uid, account.pw_gid)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    isolated = {**env, 'PORT': str(port), 'WEB_CONFIG_STATIC_DIR': str(release_dir / 'webconfig'),
                'HBOX_SERVER_DATA_DIR': str(temporary / 'data'), 'HBOX_SERVER_UPLOAD_DIR': str(temporary / 'uploads'),
                'HBOX_GALLERY_ASSET_DIR': str(temporary / 'gallery-assets')}
    log_path = release_dir / 'preflight.log'
    try:
        with log_path.open('wb') as log:
            process = subprocess.Popen(['runuser', '-u', 'xora', '--', config['node'], 'src/server.js'],
                                       cwd=release_dir / 'server', env={**os.environ, **isolated},
                                       stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            print(f'[PREFLIGHT] pid={process.pid}; log={log_path}', flush=True)
            try:
                wait_ready(f'http://127.0.0.1:{port}', release_dir.name, process)
            finally:
                stop_process(process)
    finally:
        need(temporary.parent == STATE and temporary.name.startswith('.deploy-check-') and not temporary.is_symlink(), 'Unsafe temporary path')
        shutil.rmtree(temporary)


def current_release():
    current = ROOT / 'current'
    need(not current.exists() or current.is_symlink(), 'current must be a deployment symlink')
    if not current.is_symlink():
        return None
    target = current.resolve(strict=True)
    need(target.parent == ROOT / 'releases' and re.fullmatch(RELEASE_RE, target.name), 'current points outside managed releases')
    return target


def switch_to(target):
    temporary = ROOT / ('current-' + uuid.uuid4().hex)
    temporary.symlink_to(target, target_is_directory=True)
    os.replace(temporary, ROOT / 'current')


def activate(config, target):
    previous = current_release()
    need(previous != target, 'Requested release is already current')
    active = subprocess.run(['systemctl', 'is-active', '--quiet', SERVICE], timeout=15).returncode == 0
    if not active:
        with socket.socket() as sock:
            need(sock.connect_ex(('127.0.0.1', config['port'])) != 0, 'Node port occupied by another service; stop/migrate the old service explicitly')
    print(f'[ACTIVATE] previous={previous}; target={target}', flush=True)
    run(['systemctl', 'stop', SERVICE], timeout=45)
    backup = BACKUPS / ('data-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8] + '.tar.gz')
    try:
        run(['tar', '-czf', str(backup), '-C', str(STATE), 'data', 'uploads', 'gallery-assets'], timeout=600)
        print(f'[BACKUP] {backup}', flush=True)
        switch_to(target)
    except BaseException:
        if active:
            run(['systemctl', 'start', SERVICE], timeout=45)
        raise
    try:
        run(['systemctl', 'start', SERVICE], timeout=45)
        wait_ready(f"http://127.0.0.1:{config['port']}", target.name)
    except BaseException:
        run(['systemctl', 'stop', SERVICE], timeout=45)
        print(f'Activation failed. Service STOPPED; current={target}; previous={previous}; backup={backup}. '
              'Database may have migrated. Inspect logs before explicit rollback; data was NOT restored.', flush=True)
        raise
    run(['systemctl', 'enable', SERVICE])
    # Public failure leaves a locally healthy service running for proxy diagnosis.
    try:
        probe_sites(config, target.name)
    except Exception:
        print(f'Local service is healthy and RUNNING at {target}. Public HTTPS validation failed; check DNS/TLS/Nginx. No rollback performed.', flush=True)
        raise
    print(f'DEPLOYED={target.name}; BACKUP={backup}', flush=True)


def remote_main(payload):
    import fcntl
    need(sys.platform.startswith('linux') and os.geteuid() == 0, 'Remote runner requires Linux root via sudo -n')
    def interrupted(signum, _frame):
        raise RuntimeError(f'Remote deployment interrupted by signal {signum}; inspect reported service state before retrying')
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)
    config = validate_config(payload['config'])
    # /run is root-owned; lock is released automatically on disconnect or failure.
    with open('/run/xora-deploy.lock', 'a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Another XORA deployment is running')
        action = payload['action']
        if action == 'setup':
            remote_setup(config)
            return
        env = remote_check(config)
        if action == 'check':
            return
        if action == 'rollback':
            need(payload.get('database_compatible') is True, 'Rollback requires --database-compatible after reviewing schema compatibility')
            release = payload.get('release', '')
            need(re.fullmatch(RELEASE_RE, release), 'Invalid release ID')
            target = ROOT / 'releases' / release
            need(target.is_dir() and not target.is_symlink(), 'Rollback version not found')
            # Compare deployed files with their original manifest; node_modules/logs are not package files.
            manifest = json.loads((target / 'manifest.json').read_text())
            need(manifest['release'] == release, 'Rollback manifest mismatch')
            for name, checksum in manifest['files'].items():
                safe_name(name)
                file = target / name
                need(not file.is_symlink() and file.resolve().is_relative_to(target) and digest(file) == checksum, 'Rollback files changed')
            smoke_candidate(config, target, env)
            activate(config, target)
            return
        need(action == 'deploy', 'Unknown remote operation')
        archive = Path(payload['archive'])
        need(re.fullmatch(r'/tmp/xora-deploy-[a-f0-9]{32}/bundle.tar.gz', str(archive)) and archive.resolve() == archive, 'Invalid incoming archive path')
        need(digest(archive) == payload['sha256'], 'Uploaded archive checksum mismatch')
        manifest = inspect_bundle(archive)
        need(manifest['release'] == payload['release'], 'Release ID mismatch')
        target = ROOT / 'releases' / manifest['release']
        target.mkdir()  # Existing versions are never overwritten, including interrupted attempts.
        inspect_bundle(archive, target)
        verify_export(target / 'webconfig')
        # Install scripts execute as the unprivileged application user, not root.
        run(['chown', '-R', 'xora:xora', str(target)])
        print('[INSTALL] Linux production dependencies', flush=True)
        node_dir = str(Path(config['node']).parent)
        run(['runuser', '-u', 'xora', '--', 'env', f'PATH={node_dir}:{os.defpath}',
             str(Path(config['node']).with_name('npm')), 'ci', '--omit=dev', '--no-audit', '--no-fund',
             '--cache', str(STATE / 'npm-cache')], cwd=target / 'server', timeout=600)
        run(['chown', '-R', 'root:root', str(target)])
        smoke_candidate(config, target, env)
        activate(config, target)
        archive.unlink()
        archive.parent.rmdir()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=Path('.hbox/deploy/config.json'))
    commands = parser.add_subparsers(dest='command', required=True)
    package = commands.add_parser('package', help='Build Hosted and create a local bundle; no network deployment')
    package.add_argument('--skip-build', action='store_true', help='Reuse existing export, still validate it')
    package.add_argument('--output', type=Path)
    commands.add_parser('setup', help='Create remote directories, env and systemd/Nginx templates; do not start services')
    commands.add_parser('check', help='Check remote runtime/configuration; no application start')
    deploy = commands.add_parser('deploy', help='Upload an existing package, preflight, back up and activate')
    deploy.add_argument('--package', type=Path, required=True)
    rollback = commands.add_parser('rollback', help='Activate a retained release without restoring data')
    rollback.add_argument('--release', required=True)
    rollback.add_argument('--database-compatible', action='store_true')
    args = parser.parse_args(argv)
    if args.command == 'package':
        repo = Path(__file__).resolve().parents[2]
        output = (args.output or repo / '.hbox/deploy/packages').resolve()
        build_package(repo, output, args.skip_build)
        return
    config = validate_config(json.loads(args.config.read_text(encoding='utf-8-sig')))
    need(shutil.which('ssh') and shutil.which('scp'), 'OpenSSH ssh/scp are required')
    if args.command == 'deploy':
        upload_deploy(config, args.package)
    elif args.command == 'rollback':
        need(args.database_compatible, 'Review database compatibility and pass --database-compatible')
        remote_call(config, 'rollback', release=args.release, database_compatible=True)
    else:
        remote_call(config, args.command)


if __name__ == '__main__':
    try:
        if len(sys.argv) == 3 and sys.argv[1] == '--remote':
            remote_main(json.loads(base64.urlsafe_b64decode(sys.argv[2])))
        else:
            main()
    except (Exception, KeyboardInterrupt) as error:
        print(f'ERROR: {error}', file=sys.stderr, flush=True)
        sys.exit(1)
