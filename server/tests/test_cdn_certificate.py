import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import ModuleType
import unittest
from unittest.mock import Mock, patch

SPEC = importlib.util.spec_from_file_location('xora_cdn_certificate', Path(__file__).resolve().parents[1] / 'tools/cdn_certificate.py')
cdn = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = cdn
SPEC.loader.exec_module(cdn)


class CertificateSyncTests(unittest.TestCase):
    def setUp(self):
        self.cert = cdn.Certificate('a' * 64, 'PUBLIC CERTIFICATE', 'PRIVATE KEY')
        self.upload = Mock(return_value='request-123')
        self.persist = Mock()

    def sync(self, state, edge=None, now=1000):
        return cdn.synchronize(self.cert, state, upload=self.upload,
                               fingerprint=lambda: edge, persist=self.persist, now=lambda: now)

    def test_matching_validated_edge_does_not_call_api(self):
        state = {}
        self.assertEqual(self.sync(state, self.cert.fingerprint), 'verified')
        self.upload.assert_not_called()
        self.assertEqual(state['verifiedFingerprint'], self.cert.fingerprint)

    def test_acknowledged_upload_records_fixed_target_and_waits(self):
        state = {}
        self.assertEqual(self.sync(state), 'pending')
        self.assertEqual(state['domain'], 'assets.st-dash.com')
        self.assertEqual(state['requestId'], 'request-123')
        self.assertNotIn('verifiedFingerprint', state)
        self.sync(state, now=1100)
        self.upload.assert_called_once_with(self.cert)

    def test_failed_request_is_not_recorded_as_success(self):
        self.upload.side_effect = cdn.CertificateError('API unavailable')
        state = {}
        with self.assertRaises(cdn.CertificateError):
            self.sync(state)
        self.assertEqual(state, {})
        self.persist.assert_not_called()

    def test_new_certificate_does_not_wait_on_previous_acknowledgement(self):
        state = {'acceptedFingerprint': 'b' * 64, 'acceptedAt': 990}
        self.sync(state)
        self.upload.assert_called_once()
        self.assertEqual(state['acceptedFingerprint'], self.cert.fingerprint)

    def test_expired_propagation_window_retries(self):
        self.sync({'acceptedFingerprint': self.cert.fingerprint, 'acceptedAt': 100}, now=1000)
        self.upload.assert_called_once()

    def test_private_key_omitted_from_debug_representation(self):
        self.assertNotIn('PRIVATE KEY', repr(self.cert))

    def sdk(self, result=b'{"RequestId":"request-123"}'):
        client = Mock()
        client.do_action_with_exception.return_value = result
        factory = Mock(return_value=client)
        request = Mock()
        modules = {}
        for name in ['aliyunsdkcore', 'aliyunsdkcore.client', 'aliyunsdkcdn',
                     'aliyunsdkcdn.request', 'aliyunsdkcdn.request.v20180510',
                     'aliyunsdkcdn.request.v20180510.SetCdnDomainSSLCertificateRequest']:
            modules[name] = ModuleType(name)
        modules['aliyunsdkcore.client'].AcsClient = factory
        modules['aliyunsdkcdn.request.v20180510.SetCdnDomainSSLCertificateRequest'].SetCdnDomainSSLCertificateRequest = Mock(return_value=request)
        return modules, factory, client, request

    def test_upload_uses_verified_https_fixed_domain_and_private_post_body(self):
        modules, factory, client, request = self.sdk()
        with patch.dict(sys.modules, modules), patch.object(cdn, 'read_credentials', return_value={
            'access_key_id': 'dedicated-id', 'access_key_secret': 'dedicated-secret',
        }):
            self.assertEqual(cdn.upload_certificate(self.cert), 'request-123')
        self.assertTrue(factory.call_args.kwargs['verify'])
        self.assertFalse(factory.call_args.kwargs['auto_retry'])
        request.set_endpoint.assert_called_once_with('cdn.aliyuncs.com')
        request.set_protocol_type.assert_called_once_with('https')
        request.set_method.assert_called_once_with('POST')
        request.set_DomainName.assert_called_once_with('assets.st-dash.com')
        request.set_SSLProtocol.assert_called_once_with('on')
        request.add_body_params.assert_any_call('SSLPri', 'PRIVATE KEY')
        request.set_SSLPri.assert_not_called()

    def test_upload_without_acknowledgement_fails(self):
        modules, factory, client, request = self.sdk(b'{}')
        with patch.dict(sys.modules, modules), patch.object(cdn, 'read_credentials', return_value={
            'access_key_id': 'dedicated-id', 'access_key_secret': 'dedicated-secret',
        }), self.assertRaises(cdn.CertificateError):
            cdn.upload_certificate(self.cert)

    def test_raw_sdk_errors_do_not_expose_private_payload(self):
        modules, factory, client, request = self.sdk()
        client.do_action_with_exception.side_effect = RuntimeError('PRIVATE KEY dedicated-secret')
        with patch.dict(sys.modules, modules), patch.object(cdn, 'read_credentials', return_value={
            'access_key_id': 'dedicated-id', 'access_key_secret': 'dedicated-secret',
        }), self.assertRaises(cdn.CertificateError) as caught:
            cdn.upload_certificate(self.cert)
        self.assertNotIn('PRIVATE KEY', str(caught.exception))
        self.assertNotIn('dedicated-secret', str(caught.exception))

    def test_unrelated_renewal_never_reads_or_uploads_certificate(self):
        with patch.object(sys, 'argv', ['cdn_certificate.py', 'sync']), \
             patch.object(cdn.os, 'name', 'posix'), patch.object(cdn.os, 'geteuid', return_value=0, create=True), \
             patch.dict(cdn.os.environ, {'RENEWED_LINEAGE': '/etc/letsencrypt/live/config.st-dash.com'}), \
             patch.object(cdn, 'read_certificate') as read:
            self.assertEqual(cdn.main(), 0)
            read.assert_not_called()

    def test_state_round_trip_uses_private_atomic_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'state.json'
            cdn.save_state({'domain': cdn.DOMAIN}, path)
            self.assertEqual(cdn.read_state(path), {'domain': cdn.DOMAIN})
            self.assertEqual(list(Path(directory).glob('.state-*')), [])

    def test_malformed_credentials_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'key.json'
            for value in [{'access_key_id': 'id', 'access_key_secret': ''},
                          {'access_key_id': 'id', 'access_key_secret': 'secret', 'extra': True}]:
                path.write_text(json.dumps(value))
                path.chmod(0o600)
                with self.assertRaises(cdn.CertificateError):
                    cdn.read_credentials(path)

    def test_ram_policy_contains_only_approved_capability(self):
        path = Path(__file__).resolve().parents[1] / 'tools/cdn-cert-assets-policy.json'
        policy = json.loads(path.read_text())
        self.assertEqual(policy['Statement'], [{
            'Effect': 'Allow', 'Action': 'cdn:SetCdnDomainSSLCertificate',
            'Resource': 'acs:cdn:*:*:domain/assets.st-dash.com',
            'Condition': {'IpAddress': {'acs:SourceIp': '182.92.72.220'}},
        }])


if __name__ == '__main__':
    unittest.main()
