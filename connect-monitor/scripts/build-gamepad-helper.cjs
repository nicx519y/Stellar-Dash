const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
if (process.platform !== 'win32') process.exit(0);
const root = path.resolve(__dirname, '..');
function run(args, env = process.env, allowFailure = false) {
  const started = Date.now();
  console.log(`[gamepad-helper] cmake ${args.join(' ')}`);
  const result = spawnSync('cmake', args, { cwd: root, env, stdio: 'inherit', windowsHide: true, timeout: 600000 });
  console.log(`[gamepad-helper] ${Date.now() - started} ms`);
  if (result.error || result.status !== 0) {
    if (allowFailure) return false;
    console.error(result.error || `cmake exited ${result.status}`); process.exit(1);
  }
  return true;
}
let build = '.native-build/gamepad-helper', env = process.env, binary = 'Release/xora-gamepad-helper.exe';
const nmake = '.native-build/gamepad-helper-nmake';
const vsCache=path.join(root,build,'CMakeCache.txt');
const incompleteVsCache=fs.existsSync(vsCache) && !fs.readFileSync(vsCache,'utf8').includes('CMAKE_CXX_COMPILER:FILEPATH=');
if (fs.existsSync(path.join(root, nmake, 'CMakeCache.txt')) || incompleteVsCache ||
    !run(['-S', 'native/gamepad-helper', '-B', build, '-A', 'x64'], env, true)) {
  // Desktop SDK files can be installed without MSBuild's optional UWP props.
  // Use the same detected MSVC/SDK through vcvars; do not override SDK checks.
  const cache = fs.readFileSync(path.join(root, build, 'CMakeCache.txt'), 'utf8');
  const installation = cache.match(/^CMAKE_GENERATOR_INSTANCE:INTERNAL=(.+)$/m)?.[1].trim();
  if (!installation) throw Error('MSVC not found; install the Visual Studio C++ desktop workload');
  const vcvars = path.join(installation, 'VC/Auxiliary/Build/vcvars64.bat');
  const result = spawnSync('cmd.exe', ['/d', '/s', '/c', `""${vcvars}" >nul && set"`],
    { encoding: 'utf8', windowsHide: true, windowsVerbatimArguments:true, timeout: 30000 });
  if (result.status !== 0) throw Error('Could not initialize MSVC desktop environment');
  env = { ...process.env };
  for (const line of result.stdout.split(/\r?\n/)) {
    const at = line.indexOf('=');
    if (at > 0) env[line.slice(0, at)] = line.slice(at + 1);
  }
  build = nmake; binary = 'xora-gamepad-helper.exe';
  run(['-S', 'native/gamepad-helper', '-B', build, '-G', 'NMake Makefiles', '-DCMAKE_BUILD_TYPE=Release'], env);
}
run(['--build', build, '--config', 'Release'], env);
fs.mkdirSync(path.join(root, 'dist/native'), { recursive: true });
fs.copyFileSync(path.join(root, build, binary), path.join(root, 'dist/native/xora-gamepad-helper.exe'));
