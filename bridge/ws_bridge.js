const net = require('net');
const { WebSocketServer } = require('ws');
const protobuf = require('protobufjs');

const TCP_HOST = '127.0.0.1';
const TCP_PORT = 30003;
const WS_PORT = 8080;

// Load protobuf schema
protobuf.load('../proto/adsb.proto').then(root => {
    const AircraftRecord = root.lookupType('adsb.AircraftRecord');

    const wss = new WebSocketServer({ port: WS_PORT });
    console.log(`[WsBridge] WebSocket server on ws://localhost:${WS_PORT}`);

    const wsClients = new Set();

    wss.on('connection', ws => {
        wsClients.add(ws);
        console.log(`[WsBridge] Browser connected (total: ${wsClients.size})`);

        ws.on('close', () => {
            wsClients.delete(ws);
            console.log(`[WsBridge] Browser disconnected (total: ${wsClients.size})`);
        });
    });

    // Connect to TCP server
    function connectTcp() {
        const client = new net.Socket();
        let buffer = Buffer.alloc(0);

        client.connect(TCP_PORT, TCP_HOST, () => {
            console.log(`[WsBridge] Connected to TCP server on ${TCP_HOST}:${TCP_PORT}`);
        });

        client.on('data', chunk => {
            buffer = Buffer.concat([buffer, chunk]);

            // Read length-prefixed messages
            while (buffer.length >= 4) {
                const msgLen = buffer.readUInt32BE(0);
                if (buffer.length < 4 + msgLen) break;

                const msgBuf = buffer.slice(4, 4 + msgLen);
                buffer = buffer.slice(4 + msgLen);

                try {
                    const record = AircraftRecord.decode(msgBuf);
                    const json = JSON.stringify(AircraftRecord.toObject(record));

                    for (const ws of wsClients) {
                        if (ws.readyState === 1) {
                            ws.send(json);
                        }
                    }
                } catch (err) {
                    console.error('[WsBridge] Decode error:', err.message);
                }
            }
        });

        client.on('close', () => {
            console.log('[WsBridge] TCP connection closed, reconnecting in 2s...');
            setTimeout(connectTcp, 2000);
        });

        client.on('error', err => {
            console.error('[WsBridge] TCP error:', err.message);
        });
    }

    connectTcp();
});