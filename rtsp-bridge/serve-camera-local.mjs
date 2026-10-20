import http from 'node:http';
import net from 'node:net';
import {pathToFileURL} from 'node:url';

export function createCameraProxy(camera = '192.0.2.10', cameraPort = 80) {
    if (!net.isIP(camera)) throw new Error('Camera address must be an IP address');

    function permitted(request, server) {
        const port = server.address().port;
        const host = request.headers.host;
        if (host !== `localhost:${port}` && host !== `127.0.0.1:${port}`) return false;
        if (!request.url.startsWith('/') || request.url.startsWith('//')) return false;
        const origin = request.headers.origin;
        return !origin || origin === `http://${host}`;
    }

    function upstreamRequest(request, upgrade = false) {
        return http.request({
            hostname: camera, port: cameraPort, path: request.url, method: request.method,
            headers: {...request.headers, connection: upgrade ? 'Upgrade' : 'close'},
            maxHeaderSize: 16384,
        });
    }

    const server = http.createServer({maxHeaderSize: 8192}, (request, response) => {
        if (!permitted(request, server)) {
            response.writeHead(403).end('Local same-origin access required');
            return;
        }
        const upstream = upstreamRequest(request);
        upstream.setTimeout(45000, () => upstream.destroy(new Error('Camera timeout')));
        upstream.on('response', cameraResponse => {
            response.writeHead(cameraResponse.statusCode, cameraResponse.headers);
            cameraResponse.on('error', () => response.destroy());
            cameraResponse.pipe(response);
        });
        upstream.on('error', () => {
            if (!response.headersSent) response.writeHead(502).end('Camera unavailable');
            else response.destroy();
        });
        request.on('aborted', () => upstream.destroy());
        request.on('error', () => upstream.destroy());
        response.on('close', () => {
            if (!response.writableFinished) upstream.destroy();
        });
        request.pipe(upstream);
    });

    const connections = new Set();
    server.on('connection', socket => {
        connections.add(socket);
        socket.on('close', () => connections.delete(socket));
    });
    server.closeCameraConnections = () => {
        for (const socket of connections) socket.destroy();
    };

    server.on('upgrade', (request, socket, head) => {
        let target;
        try {
            target = new URL(request.url, 'http://localhost');
        } catch {
            socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
            return;
        }
        const volume = target.searchParams.get('volume');
        const liveAudio = target.pathname === '/api/audio/live' &&
            [...target.searchParams].length === 1 && volume !== null &&
            Number.isInteger(Number(volume)) && Number(volume) >= -30 && Number(volume) <= 0 &&
            String(Number(volume)) === volume;
        if (!permitted(request, server) || request.method !== 'GET' ||
            (request.url !== '/api/ws?src=preview' && !liveAudio) ||
            request.headers.origin !== `http://${request.headers.host}` ||
            request.headers.upgrade?.toLowerCase() !== 'websocket') {
            socket.end('HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n');
            return;
        }
        const upstream = upstreamRequest(request, true);
        socket.on('error', () => upstream.destroy());
        socket.on('close', () => upstream.destroy());
        upstream.setTimeout(10000, () => upstream.destroy());
        upstream.on('error', () => socket.destroy());
        upstream.on('response', cameraResponse => {
            cameraResponse.resume();
            socket.end(`HTTP/1.1 ${cameraResponse.statusCode} Camera Response\r\nConnection: close\r\n\r\n`);
        });
        upstream.on('upgrade', (cameraResponse, cameraSocket, cameraHead) => {
            cameraSocket.setTimeout(0);
            const headers = [];
            for (let index = 0; index < cameraResponse.rawHeaders.length; index += 2) {
                headers.push(`${cameraResponse.rawHeaders[index]}: ${cameraResponse.rawHeaders[index + 1]}`);
            }
            socket.write(`HTTP/1.1 101 Switching Protocols\r\n${headers.join('\r\n')}\r\n\r\n`);
            if (cameraHead.length) socket.write(cameraHead);
            if (head.length) cameraSocket.write(head);
            cameraSocket.on('error', () => socket.destroy());
            cameraSocket.on('close', () => socket.destroy());
            socket.on('close', () => cameraSocket.destroy());
            cameraSocket.pipe(socket);
            socket.pipe(cameraSocket);
        });
        upstream.end();
    });
    return server;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const camera = process.argv[2] || '192.0.2.10';
    const server = createCameraProxy(camera);
    server.on('error', error => {
        console.error(error.message);
        process.exitCode = 1;
    });
    server.listen(8800, '127.0.0.1', () => {
        console.log(`Camera interface: http://localhost:8800/ (camera ${camera})`);
    });
    for (const signal of ['SIGINT', 'SIGTERM']) {
        process.on(signal, () => {
            server.close();
            server.closeCameraConnections();
        });
    }
}