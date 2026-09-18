import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { createServer } from 'node:http';
import { extname, resolve, sep } from 'node:path';

const args = process.argv.slice(2);
const positional = [];
for (let index = 0; index < args.length; index += 1) {
    const arg = args[index];
    if (arg === '--host' || arg === '--port') {
        index += 1;
    } else if (!arg.startsWith('--')) {
        positional.push(arg);
    }
}

function option(name, fallback) {
    const inline = args.find((arg) => arg.startsWith(`${name}=`));
    if (inline) {
        return inline.slice(name.length + 1);
    }

    const index = args.indexOf(name);
    return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
}

const root = resolve(process.cwd(), positional[0] ?? 'build');
const host = option('--host', '127.0.0.1');
const port = Number.parseInt(option('--port', '4000'), 10);

if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(`Invalid port: ${option('--port', '4000')}`);
}

try {
    if (!(await stat(root)).isDirectory()) {
        throw new Error('not a directory');
    }
} catch {
    console.error(`Static export not found: ${root}`);
    console.error('Build it first with "npm run build:hosted" or "npm run build:mock".');
    process.exit(1);
}

const MIME_TYPES = new Map([
    ['.css', 'text/css; charset=utf-8'],
    ['.gif', 'image/gif'],
    ['.html', 'text/html; charset=utf-8'],
    ['.ico', 'image/x-icon'],
    ['.jpeg', 'image/jpeg'],
    ['.jpg', 'image/jpeg'],
    ['.js', 'text/javascript; charset=utf-8'],
    ['.json', 'application/json; charset=utf-8'],
    ['.map', 'application/json; charset=utf-8'],
    ['.png', 'image/png'],
    ['.svg', 'image/svg+xml'],
    ['.txt', 'text/plain; charset=utf-8'],
    ['.webp', 'image/webp'],
    ['.woff', 'font/woff'],
    ['.woff2', 'font/woff2'],
]);

function safeResolve(relativePath) {
    const target = resolve(root, relativePath);
    if (target !== root && !target.startsWith(`${root}${sep}`)) {
        return null;
    }
    return target;
}

async function regularFile(path) {
    try {
        return (await stat(path)).isFile();
    } catch {
        return false;
    }
}

async function directory(path) {
    try {
        return (await stat(path)).isDirectory();
    } catch {
        return false;
    }
}

function sendFile(request, response, path) {
    const extension = extname(path).toLowerCase();
    response.statusCode = 200;
    response.setHeader('Content-Type', MIME_TYPES.get(extension) ?? 'application/octet-stream');
    response.setHeader(
        'Cache-Control',
        extension === '.html' ? 'no-cache' : 'public, max-age=3600',
    );

    if (request.method === 'HEAD') {
        response.end();
        return;
    }

    const stream = createReadStream(path);
    stream.on('error', () => {
        if (!response.headersSent) {
            response.statusCode = 500;
        }
        response.end();
    });
    stream.pipe(response);
}

const server = createServer(async (request, response) => {
    if (request.method !== 'GET' && request.method !== 'HEAD') {
        response.writeHead(405, { Allow: 'GET, HEAD' });
        response.end('Method Not Allowed');
        return;
    }

    let pathname;
    try {
        pathname = decodeURIComponent(new URL(request.url ?? '/', 'http://localhost').pathname);
    } catch {
        response.writeHead(400);
        response.end('Bad Request');
        return;
    }

    const relativePath = pathname.replace(/^\/+/, '');
    const directPath = safeResolve(relativePath);
    if (!directPath) {
        response.writeHead(403);
        response.end('Forbidden');
        return;
    }

    if (pathname !== '/' && !pathname.endsWith('/') && await directory(directPath)) {
        response.writeHead(308, { Location: `${pathname}/` });
        response.end();
        return;
    }

    const candidates = pathname === '/'
        ? [safeResolve('index.html')]
        : [
            directPath,
            pathname.endsWith('/') ? safeResolve(`${relativePath}index.html`) : null,
        ];

    for (const candidate of candidates) {
        if (candidate && await regularFile(candidate)) {
            sendFile(request, response, candidate);
            return;
        }
    }

    // Browser navigation to an unknown extensionless route uses the same
    // fallback as hosted deployments. Router then canonicalizes it to /global/.
    const acceptsHtml = request.headers.accept?.includes('text/html') ?? false;
    if (!extname(pathname) && acceptsHtml) {
        const fallback = safeResolve('index.html');
        if (fallback && await regularFile(fallback)) {
            sendFile(request, response, fallback);
            return;
        }
    }

    response.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
    response.end('Not Found');
});

server.listen(port, host, () => {
    console.log(`Serving ${root}`);
    console.log(`http://${host}:${port}`);
});
