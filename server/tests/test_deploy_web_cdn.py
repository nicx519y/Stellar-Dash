import importlib.util
import json
import gzip
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from unittest.mock import MagicMock

SPEC = importlib.util.spec_from_file_location('deploy_web_cdn', Path(__file__).parents[1] / 'tools/deploy_xora.py')
deploy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deploy)
ORIGIN = 'https://assets.example.com'
CONFIG = {'host': 'host.example.com', 'user': 'root', 'domain': 'config.example.com',
          'email_from': 'XORA <no-reply@example.com>', 'web_cdn_origin': ORIGIN}


class WebCdnDeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.host = self.root / 'host'
        self.host.mkdir()
        self.repo = self.root / 'repo'
        self.web = self.repo / 'application/www/build'
        self.version = 'a' * 32
        for name in ('server/src/server.js', 'server/scripts/account-role.js', 'server/package.json',
                     'server/package-lock.json', 'common/uimg-jpeg.cjs', 'common/xora-resource-codec.cjs',
                     'resources/xora/key-static.xora-resource.json', 'resources/default-switch-mapping.json'):
            p = self.repo / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('{}')
        for name in ('_next/static/app.js', 'fonts/test.woff2', 'images/logo.svg', '_next/static/private.map'):
            p = self.web / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(name)
        self.set_version(self.version)

    def set_version(self, version):
        metadata = {'schema': 1, 'origin': ORIGIN, 'version': version}
        (self.web / 'cdn-assets.json').write_text(json.dumps(metadata))
        for name in deploy.PAGES:
            p = self.web / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('<html><script src="' + deploy.cdn_asset_base(metadata) + '/_next/static/app.js"></script></html>')

    def release(self):
        archive = deploy.build_package(self.repo, self.root / 'packages', skip_build=True, cdn_origin=ORIGIN)
        manifest = deploy.inspect_bundle(archive)
        target = self.host / manifest['release']
        target.mkdir()
        deploy.inspect_bundle(archive, target)
        return target

    def test_cdn_build_requires_matching_metadata_origin_and_files(self):
        deploy.verify_export(self.web)
        with self.assertRaisesRegex(RuntimeError, 'CDN origin differs'):
            deploy.build_package(self.repo, self.root / 'bad', skip_build=True)
        (self.web / '_next/static/app.js').unlink()
        with self.assertRaisesRegex(RuntimeError, 'missing chunk'):
            deploy.verify_export(self.web)

    def test_atomic_publication_retains_old_versions_and_excludes_html_and_maps(self):
        first = self.release()
        with patch.object(deploy, 'ROOT', self.host):
            deploy.publish_web_assets(first, CONFIG)
            deploy.publish_web_assets(first, CONFIG)  # Identical retries are safe.
            self.set_version('b' * 32)
            second = self.release()
            deploy.publish_web_assets(second, CONFIG)
        for version in [self.version, 'b' * 32]:
            base = self.host / 'web-assets' / version
            catalog = json.loads((base / 'manifest.json').read_text())
            self.assertEqual(set(catalog['files']), {'_next/static/app.js', 'fonts/test.woff2', 'images/logo.svg'})
            self.assertEqual((base / 'files/_next/static/app.js').read_text(), '_next/static/app.js')

    def test_published_version_cannot_be_overwritten_or_reused_after_corruption(self):
        first = self.release()
        with patch.object(deploy, 'ROOT', self.host):
            deploy.publish_web_assets(first, CONFIG)
            published = self.host / 'web-assets' / self.version / 'files/_next/static/app.js'
            published.write_text('changed')
            with self.assertRaisesRegex(RuntimeError, 'was modified'):
                deploy.publish_web_assets(first, CONFIG)
            (self.web / '_next/static/app.js').write_text('new content')
            second = self.release()
            with self.assertRaisesRegex(RuntimeError, 'different content'):
                deploy.publish_web_assets(second, CONFIG)

    def test_origin_mismatch_is_rejected_before_publication(self):
        release = self.release()
        with patch.object(deploy, 'ROOT', self.host), self.assertRaisesRegex(RuntimeError, 'origin mismatch'):
            deploy.publish_web_assets(release, {**CONFIG, 'web_cdn_origin': 'https://other.example.com'})
        self.assertFalse((self.host / 'web-assets').exists())

    def test_cdn_mode_adds_only_public_asset_environment(self):
        config = deploy.validate_config(CONFIG)
        env = deploy.environment(config)
        self.assertEqual(env['WEB_CONFIG_CDN_ORIGIN'], ORIGIN)
        self.assertEqual(env['WEB_CONFIG_ORIGINS'], 'https://config.example.com')
        self.assertIn('WEB_CONFIG_CDN_ASSET_DIR', env)
        for invalid in ['http://assets.example.com', ORIGIN + '/', ORIGIN + '/path', None]:
            with self.assertRaises(RuntimeError):
                deploy.validate_config({**CONFIG, 'web_cdn_origin': invalid})

    def test_legacy_chunk_retention_is_hash_checked_and_never_includes_html(self):
        release = self.release()
        with patch.object(deploy, 'ROOT', self.host):
            deploy.retain_legacy_web_assets(release)
            deploy.retain_legacy_web_assets(release)
            root = self.host / 'web-assets/legacy'
            self.assertEqual((root / '_next/static/app.js').read_text(), '_next/static/app.js')
            self.assertFalse((root / 'index.html').exists())
            (root / '_next/static/app.js').write_text('corrupted')
            with self.assertRaisesRegex(RuntimeError, 'collision'):
                deploy.retain_legacy_web_assets(release)

    def test_cdn_transport_compression_is_decoded_before_integrity_check(self):
        release = self.release()
        with patch.object(deploy, 'ROOT', self.host):
            deploy.publish_web_assets(release, CONFIG)
            def compressed(request, **kwargs):
                self.assertEqual(request.get_header('Accept-encoding'), 'gzip')
                relative = request.full_url.split('/cdn/web/' + self.version + '/')[1]
                data = (release / 'webconfig' / relative).read_bytes()
                response = MagicMock()
                response.status = 200
                response.headers = {'Content-Encoding': 'gzip', 'Access-Control-Allow-Origin': '*',
                                    'Cross-Origin-Resource-Policy': 'cross-origin'}
                response.read.return_value = gzip.compress(data)
                response.__enter__.return_value = response
                return response
            with patch.object(deploy.urllib.request, 'urlopen', side_effect=compressed):
                deploy.probe_web_cdn(CONFIG, release)


if __name__ == '__main__':
    unittest.main()
