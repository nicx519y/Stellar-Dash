#!/usr/bin/env python3
"""Renewal deploy hook for the single XORA CDN domain; never prints secrets."""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import ssl
import stat
import subprocess
import sys
import tempfile
import time

DOMAIN = 'assets.st-dash.com'
LINEAGE = Path('/etc/letsencrypt/live') / DOMAIN
CREDENTIALS = Path('/etc/xora/secrets/aliyun-cdn-assets.json')
STATE = Path('/var/lib/xora-cdn-cert/state.json')


class CertificateError(RuntimeError):
    pass


@dataclass(frozen=True)
class Certificate:
    fingerprint: str
    fullchain: str = field(repr=False)
    private_key: str = field(repr=False)


def openssl(arguments, data=None):
    result = subprocess.run(['openssl', *arguments], input=data, capture_output=True, timeout=15)
    if result.returncode:
        # OpenSSL / SDK exception details may contain request or key material.
        raise CertificateError('Certificate validation failed: ' + arguments[0])
    return result.stdout


def read_certificate(lineage=LINEAGE):
    lineage = Path(lineage)
    cert = lineage / 'cert.pem'
    openssl(['x509', '-in', str(cert), '-noout', '-checkend', '604800'])
    openssl(['verify', '-verify_hostname', DOMAIN, '-CApath', '/etc/ssl/certs',
             '-untrusted', str(lineage / 'chain.pem'), str(cert)])
    der = openssl(['x509', '-in', str(cert), '-outform', 'DER'])
    fullchain = (lineage / 'fullchain.pem').read_text(encoding='ascii')
    if openssl(['x509', '-outform', 'DER'], fullchain.encode('ascii')) != der:
        raise CertificateError('Full chain and leaf certificate do not match')
    key_path = lineage / 'privkey.pem'
    require_private_file(key_path, allow_symlink=True)
    private_key = key_path.read_text(encoding='ascii')
    public = openssl(['x509', '-in', str(cert), '-pubkey', '-noout'])
    key_public = openssl(['pkey', '-pubout'], private_key.encode('ascii'))
    if public != key_public:
        raise CertificateError('Certificate and key do not match')
    return Certificate(hashlib.sha256(der).hexdigest(), fullchain, private_key)


def require_private_file(path, allow_symlink=False):
    path = Path(path)
    if path.is_symlink() and not allow_symlink:
        raise CertificateError('Credential/state file must not be a symlink')
    info = path.stat()
    if not stat.S_ISREG(info.st_mode):
        raise CertificateError('Expected a regular private file')
    if os.name == 'posix' and (info.st_uid != 0 or info.st_mode & 0o077):
        raise CertificateError('Private file must be root-owned with mode 600 or stricter')


def read_credentials(path=CREDENTIALS):
    require_private_file(path)
    value = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    if set(value) != {'access_key_id', 'access_key_secret'} or any(
        not isinstance(value.get(key), str) or not value[key].strip() for key in value
    ):
        raise CertificateError('Dedicated CDN credentials are missing or malformed')
    return value


def upload_certificate(certificate, credentials_path=CREDENTIALS):
    from aliyunsdkcore.client import AcsClient
    from aliyunsdkcdn.request.v20180510.SetCdnDomainSSLCertificateRequest import SetCdnDomainSSLCertificateRequest

    credentials = read_credentials(credentials_path)
    client = AcsClient(credentials['access_key_id'], credentials['access_key_secret'],
                       'cn-hangzhou', auto_retry=False, port=443, connect_timeout=10,
                       timeout=20, verify=True, debug=False)
    request = SetCdnDomainSSLCertificateRequest()
    request.set_endpoint('cdn.aliyuncs.com')
    request.set_protocol_type('https')
    request.set_method('POST')
    request.set_accept_format('json')
    request.set_DomainName(DOMAIN)
    request.set_CertType('upload')
    request.set_CertName('xora-assets-' + certificate.fingerprint[:24])
    request.set_SSLProtocol('on')
    # Keep PEM material in the signed POST body, never in a request URL.
    request.add_body_params('SSLPub', certificate.fullchain)
    request.add_body_params('SSLPri', certificate.private_key)
    try:
        response = json.loads(client.do_action_with_exception(request))
    except Exception as error:
        code = error.get_error_code() if hasattr(error, 'get_error_code') else type(error).__name__
        code = re.sub(r'[^A-Za-z0-9_.-]', '', str(code))[:96]
        raise CertificateError('CDN certificate API failed: ' + code) from None
    request_id = response.get('RequestId')
    if not isinstance(request_id, str) or not re.fullmatch(r'[A-Za-z0-9-]{1,128}', request_id):
        raise CertificateError('CDN API returned no valid acknowledgement')
    return request_id


def live_fingerprint():
    try:
        context = ssl.create_default_context()
        with socket.create_connection((DOMAIN, 443), timeout=5) as sock:
            with context.wrap_socket(sock, server_hostname=DOMAIN) as tls:
                return hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest()
    except (OSError, ssl.SSLError):
        return None


def save_state(value, path=STATE):
    path = Path(path)
    if path.exists():
        require_private_file(path)
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    if path.parent.is_symlink():
        raise CertificateError('State directory must not be a symlink')
    descriptor, temporary = tempfile.mkstemp(prefix='.state-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            json.dump(value, stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def read_state(path=STATE):
    if not Path(path).exists():
        return {}
    require_private_file(path)
    value = json.loads(Path(path).read_text(encoding='utf-8'))
    if not isinstance(value, dict):
        raise CertificateError('Invalid certificate synchronization state')
    return value


def synchronize(certificate, state, *, upload=upload_certificate, fingerprint=live_fingerprint,
                persist=save_state, now=time.time):
    current = fingerprint()
    if current == certificate.fingerprint:
        if state.get('verifiedFingerprint') != current:
            state.update(domain=DOMAIN, verifiedFingerprint=current, verifiedAt=now())
            persist(state)
        return 'verified'
    # After an acknowledgement, wait for CDN propagation before any resubmission.
    if state.get('acceptedFingerprint') == certificate.fingerprint and now() - state.get('acceptedAt', 0) < 900:
        return 'pending'
    request_id = upload(certificate)
    state.update(domain=DOMAIN, acceptedFingerprint=certificate.fingerprint, acceptedAt=now(), requestId=request_id)
    persist(state)
    return 'pending'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'sync', 'status'])
    parser.add_argument('--wait-seconds', type=int, default=0)
    args = parser.parse_args()
    if not 0 <= args.wait_seconds <= 180:
        raise CertificateError('wait-seconds must be between 0 and 180')
    if os.name != 'posix' or os.geteuid() != 0:
        raise CertificateError('Run this deploy hook as root on the certificate server')
    renewed = os.environ.get('RENEWED_LINEAGE')
    if renewed and Path(renewed) != LINEAGE:
        print('Unrelated certificate: no CDN operation')
        return 0
    import fcntl
    STATE.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (STATE.parent / 'sync.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('Another certificate sync is running')
            return 0
        certificate = read_certificate()
        if args.action == 'check':
            print(json.dumps({'domain': DOMAIN, 'fingerprint': certificate.fingerprint, 'certificateValid': True}))
            return 0
        if args.action == 'status':
            print(json.dumps({'domain': DOMAIN, 'localFingerprint': certificate.fingerprint,
                              'edgeFingerprint': live_fingerprint(), 'state': read_state()}))
            return 0
        state = read_state()
        result = synchronize(certificate, state)
        deadline = time.monotonic() + args.wait_seconds
        while result == 'pending' and time.monotonic() < deadline:
            time.sleep(min(5, max(0, deadline - time.monotonic())))
            result = synchronize(certificate, state)
        print(json.dumps({'domain': DOMAIN, 'fingerprint': certificate.fingerprint, 'result': result}))
        return 0 if result == 'verified' else 3


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        # Never render raw provider errors, HTTP payloads, credentials or keys.
        message = str(error) if isinstance(error, CertificateError) else type(error).__name__
        print('CDN_CERT_ERROR: ' + message, file=sys.stderr)
        sys.exit(1)
