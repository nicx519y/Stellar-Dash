"""Pure host deployment tests: no SSH, systemd, mail, real state or hardware."""
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('deploy_xora', Path(__file__).parents[1] / 'tools/deploy_xora.py')
deploy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deploy)

CONFIG = {'host': 'host.example.com', 'user': 'root', 'domain': 'config.example.com',
          'email_from': 'XORA <no-reply@auth.example.com>'}


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config = deploy.validate_config(CONFIG)

    def fixture(self):
        repo = self.root / 'repo'
        for name in ('server/src/server.js', 'server/scripts/account-role.js', 'server/package.json',
                     'server/package-lock.json', 'common/uimg-jpeg.cjs',
                     'common/xora-resource-codec.cjs', 'resources/xora/key-static.xora-resource.json',
                     'resources/default-switch-mapping.json'):
            file = repo / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('{}')
        web = repo / 'application/www/build'
        chunk = web / '_next/static/chunk.js'
        chunk.parent.mkdir(parents=True)
        chunk.write_text('console.log("hosted")')
        for page in deploy.PAGES:
            file = web / page
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('<html><script src="/_next/static/chunk.js"></script></html>')
        for name in ('server/.env', 'server/data/secret.sqlite3', 'server/uploads/private.zip', 'server/node_modules/private.js'):
            file = repo / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('PRIVATE')
        return repo

    def package(self):
        return deploy.build_package(self.fixture(), self.root / 'packages', skip_build=True)

    def rewrite(self, archive, change):
        result = self.root / 'mutated.tar.gz'
        with tarfile.open(archive) as source, tarfile.open(result, 'w:gz') as output:
            entries = [(m, source.extractfile(m).read()) for m in source.getmembers()]
            for member, data in change(entries):
                member.size = len(data)
                output.addfile(member, io.BytesIO(data))
        return result

    def test_package_contains_complete_runtime_and_excludes_private_state(self):
        archive = self.package()
        destination = self.root / 'extract'
        destination.mkdir()
        manifest = deploy.inspect_bundle(archive, destination)
        self.assertIn('common/uimg-jpeg.cjs', manifest['files'])
        self.assertIn('common/xora-resource-codec.cjs', manifest['files'])
        self.assertIn('resources/xora/key-static.xora-resource.json', manifest['files'])
        self.assertIn('resources/default-switch-mapping.json', manifest['files'])
        self.assertIn('server/scripts/account-role.js', manifest['files'])
        self.assertFalse(any('PRIVATE' in p.read_text() for p in destination.rglob('*') if p.is_file()))
        deploy.verify_export(destination / 'webconfig')
        self.assertTrue(archive.with_suffix('.gz.sha256').is_file())

    def test_mock_and_missing_chunk_are_rejected_before_packaging(self):
        repo = self.fixture()
        chunk = repo / 'application/www/build/_next/static/chunk.js'
        chunk.write_text('MOCK DEVICE')
        with self.assertRaisesRegex(RuntimeError, 'Mock code'):
            deploy.build_package(repo, self.root / 'packages', True)
        chunk.unlink()
        with self.assertRaisesRegex(RuntimeError, 'missing chunk'):
            deploy.build_package(repo, self.root / 'packages', True)

    def test_corrupted_payload_rejected(self):
        archive = self.package()
        def corrupt(entries):
            return [(m, b'corrupt' if m.name == 'common/uimg-jpeg.cjs' else data) for m, data in entries]
        with self.assertRaisesRegex(RuntimeError, 'Hash mismatch'):
            deploy.inspect_bundle(self.rewrite(archive, corrupt))

    def test_default_mapping_must_be_present_even_with_a_consistent_manifest(self):
        archive = self.package()
        def remove_default(entries):
            result = []
            for member, data in entries:
                if member.name == 'resources/default-switch-mapping.json':
                    continue
                if member.name == 'manifest.json':
                    manifest = json.loads(data)
                    del manifest['files']['resources/default-switch-mapping.json']
                    data = json.dumps(manifest).encode()
                result.append((member, data))
            return result
        with self.assertRaisesRegex(RuntimeError, 'Incomplete deployment bundle'):
            deploy.inspect_bundle(self.rewrite(archive, remove_default))

    def test_traversal_symlink_duplicate_and_extra_entries_rejected(self):
        archive = self.package()
        for name, kind, message in (('../escape', tarfile.REGTYPE, 'Unsafe'),
                                     ('server/src/link.js', tarfile.SYMTYPE, 'regular files'),
                                     ('common/uimg-jpeg.cjs', tarfile.REGTYPE, 'Duplicate'),
                                     ('server/.env', tarfile.REGTYPE, 'Unsafe')):
            with self.subTest(name=name, kind=kind):
                def mutate(entries):
                    member = tarfile.TarInfo(name)
                    member.type = kind
                    member.linkname = '/etc/passwd' if kind == tarfile.SYMTYPE else ''
                    return entries + [(member, b'')]
                with self.assertRaisesRegex(RuntimeError, message):
                    deploy.inspect_bundle(self.rewrite(archive, mutate))

    def test_config_rejects_shell_and_template_injection(self):
        for key, value in [('domain', 'foo.com;touch /tmp/x'), ('host', '-oProxyCommand=x'),
                           ('email_from', 'XORA <a@b.com>\nKEY=value'), ('node', '/usr/bin/node --eval x'),
                           ('port', 80), ('ssh_port', True), ('unknown', 'value')]:
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                deploy.validate_config({**CONFIG, key: value})

    def test_rendered_environment_matches_runtime_and_ssh_keeps_host_checks(self):
        env_file = self.root / 'server.env'
        env_file.write_text(deploy.templates(self.config)['server.env'])
        self.assertEqual(deploy.read_env(env_file), deploy.environment(self.config))
        args = deploy.ssh_args(self.config)
        self.assertIn('StrictHostKeyChecking=yes', args)
        self.assertNotIn('StrictHostKeyChecking=no', args)

    def test_failed_build_never_packages_stale_export(self):
        repo = self.fixture()
        with patch.object(deploy, 'run', side_effect=RuntimeError('build failed')):
            with self.assertRaisesRegex(RuntimeError, 'build failed'):
                deploy.build_package(repo, self.root / 'packages')
        self.assertFalse((self.root / 'packages').exists())

    def test_split_domains_keep_both_api_origins_and_separate_entry_routes(self):
        config = deploy.validate_config({**CONFIG, 'admin_domain': 'manager.example.com'})
        env = deploy.environment(config)
        self.assertEqual(env['USER_AUTH_PUBLIC_ORIGIN'], 'https://config.example.com')
        self.assertEqual(env['USER_AUTH_ALLOWED_ORIGINS'], 'https://config.example.com,https://manager.example.com')
        self.assertEqual(env['WEB_CONFIG_ORIGINS'], env['USER_AUTH_ALLOWED_ORIGINS'])
        nginx = deploy.templates(config)['nginx.conf']
        self.assertEqual(nginx.count('proxy_pass http://127.0.0.1:3000;'), 2)
        self.assertIn('/live/config.example.com/fullchain.pem', nginx)
        self.assertIn('/live/manager.example.com/fullchain.pem', nginx)
        self.assertIn('return 302 https://manager.example.com$request_uri;', nginx)
        self.assertIn('return 302 https://config.example.com$request_uri;', nginx)
        self.assertIn('return 302 https://manager.example.com/admin/users/;', nginx)

    def test_public_probe_checks_both_domains_and_redirects(self):
        config = deploy.validate_config({**CONFIG, 'admin_domain': 'manager.example.com'})
        def redirect(base, path, expected, **kwargs):
            self.assertEqual(expected, 302)
            self.assertFalse(kwargs['follow_redirects'])
            locations = {('https://config.example.com', '/admin/users/'): 'https://manager.example.com/admin/users/',
                         ('https://manager.example.com', '/'): 'https://manager.example.com/admin/users/',
                         ('https://manager.example.com', '/global/'): 'https://config.example.com/global/'}
            return b'', {'Location': locations[(base, path)]}
        with patch.object(deploy, 'probe') as probe, patch.object(deploy, 'response', side_effect=redirect):
            deploy.probe_sites(config, 'release')
            self.assertEqual([call.args[0] for call in probe.call_args_list],
                             ['https://config.example.com', 'https://manager.example.com'])
            self.assertTrue(all(not p.startswith('admin/') for p in probe.call_args_list[0].args[2]))
            self.assertTrue(all(p.startswith(('admin/', 'auth/')) for p in probe.call_args_list[1].args[2]))

    def test_old_single_domain_config_remains_supported(self):
        self.assertEqual(self.config['admin_domain'], self.config['domain'])
        self.assertEqual(deploy.templates(self.config)['nginx.conf'].count('listen 443 ssl;'), 1)

    def activation(self, failure):
        old = self.root / 'old'
        new = self.root / 'new'
        calls = []
        def command(args, **kwargs):
            calls.append(args)
            if failure == 'backup' and args[0] == 'tar':
                raise RuntimeError('backup failed')
            return ''
        with patch.object(deploy, 'current_release', return_value=old), \
             patch.object(deploy, 'BACKUPS', self.root), \
             patch.object(deploy, 'run', side_effect=command), \
             patch.object(deploy.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)), \
             patch.object(deploy, 'switch_to') as switch, \
             patch.object(deploy, 'wait_ready', side_effect=RuntimeError('startup failed') if failure == 'startup' else None), \
             patch.object(deploy, 'probe_sites', side_effect=RuntimeError('TLS failed') if failure == 'public' else None):
            if failure:
                with self.assertRaises(RuntimeError):
                    deploy.activate(self.config, new)
            else:
                deploy.activate(self.config, new)
            return calls, switch.call_count

    def test_backup_failure_restarts_old_without_switch(self):
        calls, switches = self.activation('backup')
        self.assertEqual(switches, 0)
        self.assertEqual(calls[-1], ['systemctl', 'start', deploy.SERVICE])

    def test_startup_failure_stops_candidate_without_restoring_database(self):
        calls, switches = self.activation('startup')
        self.assertEqual(switches, 1)
        self.assertEqual(calls[-1], ['systemctl', 'stop', deploy.SERVICE])
        self.assertEqual(sum(args[0] == 'tar' for args in calls), 1)
        self.assertIn('-czf', next(args for args in calls if args[0] == 'tar'))

    def test_public_failure_keeps_locally_healthy_service_running(self):
        calls, switches = self.activation('public')
        self.assertEqual(switches, 1)
        self.assertEqual(calls[-1], ['systemctl', 'enable', deploy.SERVICE])

    def test_activation_backs_up_before_starting_candidate(self):
        calls, switches = self.activation(None)
        self.assertEqual(switches, 1)
        self.assertLess(next(i for i, a in enumerate(calls) if a[0] == 'tar'),
                        calls.index(['systemctl', 'start', deploy.SERVICE]))

    def test_foreign_listener_is_never_stopped_or_replaced(self):
        with patch.object(deploy, 'current_release', return_value=None), \
             patch.object(deploy.subprocess, 'run', return_value=subprocess.CompletedProcess([], 3)), \
             patch.object(deploy.socket, 'socket') as sock, patch.object(deploy, 'run') as command:
            sock.return_value.__enter__.return_value.connect_ex.return_value = 0
            with self.assertRaisesRegex(RuntimeError, 'another service'):
                deploy.activate(self.config, self.root / 'candidate')
            command.assert_not_called()

    def test_owned_process_timeout_is_real(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            deploy.run([sys.executable, '-c', 'import time; time.sleep(30)'], timeout=0.1)

    def test_rollback_requires_explicit_compatibility_before_network(self):
        config = self.root / 'config.json'
        config.write_text(json.dumps(CONFIG))
        with patch.object(deploy, 'remote_call') as remote, patch.object(deploy.shutil, 'which', return_value='tool'):
            with self.assertRaisesRegex(RuntimeError, 'database compatibility'):
                deploy.main(['--config', str(config), 'rollback', '--release', '20261005-100000-abcdef12'])
            remote.assert_not_called()


if __name__ == '__main__':
    unittest.main()
