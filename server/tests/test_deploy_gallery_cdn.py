import runpy
import unittest
from pathlib import Path


class GalleryCdnDeployTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.deploy = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/deploy_xora.py'))

    def config(self, **extra):
        return self.deploy['validate_config']({
            'host': '192.0.2.1', 'user': 'root', 'domain': 'config.example.com',
            'admin_domain': 'manager.example.com', 'email_from': 'XORA <no-reply@example.com>', **extra,
        })

    def test_optional_origin_preserves_existing_deployment_environment(self):
        original = self.deploy['environment'](self.config())
        self.assertNotIn('GALLERY_CDN_ORIGIN', original)
        self.assertEqual(self.deploy['environment'](self.config(gallery_cdn_origin='')), original)
        configured = self.deploy['environment'](self.config(gallery_cdn_origin='https://assets.example.com'))
        self.assertEqual(configured.pop('GALLERY_CDN_ORIGIN'), 'https://assets.example.com')
        self.assertEqual(configured, original)

    def test_origin_rejects_paths_credentials_and_config_injection(self):
        for value in [None, False, 'http://assets.example.com', 'https://assets.example.com/',
                      'https://assets.example.com:443', 'https://user@assets.example.com',
                      'https://a..example.com', 'https://-bad.example.com',
                      'https://assets.example.com\nEVIL=1']:
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                self.config(gallery_cdn_origin=value)

    def test_acme_webroot_is_served_without_shared_cache_on_both_tls_hosts(self):
        nginx = self.deploy['templates'](self.config())['nginx.conf']
        self.assertEqual(nginx.count('location ^~ /.well-known/acme-challenge/'), 2)
        self.assertEqual(nginx.count('root /var/lib/letsencrypt;'), 2)
        self.assertEqual(nginx.count('add_header Cache-Control "no-store" always;'), 2)
        self.assertEqual(nginx.count('proxy_pass http://127.0.0.1:3000;'), 2)


if __name__ == '__main__':
    unittest.main()
