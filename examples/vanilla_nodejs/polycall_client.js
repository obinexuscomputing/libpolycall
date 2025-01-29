// root/example/noderaw_example/polycall_client.js
const net = require('net');
const EventEmitter = require('events');

// Protocol constants
const PROTOCOL_VERSION = 1;
const MESSAGE_TYPES = {
    HANDSHAKE: 0x01,
    AUTH: 0x02,
    COMMAND: 0x03,
    RESPONSE: 0x04,
    ERROR: 0x05,
    HEARTBEAT: 0x06
};

class PolyCallClient extends EventEmitter {
    constructor() {
        super();
        this.socket = null;
        this.connected = false;
        this.sequence = 1;
    }

    // Connect to PolyCall server
    connect(port = 8080, host = 'localhost') {
        this.socket = new net.Socket();

        this.socket.on('connect', () => {
            console.log('Connected to PolyCall server');
            this.connected = true;
            this.sendHandshake();
        });

        this.socket.on('data', (data) => {
            this.handleMessage(data);
        });

        this.socket.on('close', () => {
            console.log('Connection closed');
            this.connected = false;
            this.emit('disconnected');
        });

        this.socket.on('error', (err) => {
            console.error('Socket error:', err);
            this.emit('error', err);
        });

        this.socket.connect(port, host);
    }

    // Create message header
    createHeader(type, payloadLength) {
        const header = Buffer.alloc(16); // 16 byte header
        header.writeUInt8(PROTOCOL_VERSION, 0); // Version
        header.writeUInt8(type, 1); // Message type
        header.writeUInt16LE(0, 2); // Flags
        header.writeUInt32LE(this.sequence++, 4); // Sequence number
        header.writeUInt32LE(payloadLength, 8); // Payload length
        header.writeUInt32LE(0, 12); // Checksum (placeholder)
        return header;
    }

    // Calculate checksum
    calculateChecksum(data) {
        let checksum = 0;
        for (let i = 0; i < data.length; i++) {
            checksum = ((checksum << 5) | (checksum >>> 27)) + data[i];
        }
        return checksum >>> 0; // Convert to unsigned 32-bit
    }

    // Send handshake message
    sendHandshake() {
        const payload = Buffer.alloc(8);
        payload.writeUInt32LE(0x504C43, 0); // Magic number "PLC"
        payload.writeUInt32LE(0, 4); // Reserved

        const header = this.createHeader(MESSAGE_TYPES.HANDSHAKE, payload.length);
        const message = Buffer.concat([header, payload]);
        
        // Calculate and set checksum
        const checksum = this.calculateChecksum(payload);
        message.writeUInt32LE(checksum, 12);

        this.socket.write(message);
    }

    // Send command to server
    sendCommand(command) {
        if (!this.connected) {
            throw new Error('Not connected to server');
        }

        const payload = Buffer.from(command);
        const header = this.createHeader(MESSAGE_TYPES.COMMAND, payload.length);
        const message = Buffer.concat([header, payload]);
        
        // Calculate and set checksum
        const checksum = this.calculateChecksum(payload);
        message.writeUInt32LE(checksum, 12);

        this.socket.write(message);
    }

    // Handle incoming messages
    handleMessage(data) {
        if (data.length < 16) {
            console.error('Invalid message: too short');
            return;
        }

        const header = {
            version: data.readUInt8(0),
            type: data.readUInt8(1),
            flags: data.readUInt16LE(2),
            sequence: data.readUInt32LE(4),
            payloadLength: data.readUInt32LE(8),
            checksum: data.readUInt32LE(12)
        };

        const payload = data.slice(16, 16 + header.payloadLength);
        
        // Verify checksum
        const calculatedChecksum = this.calculateChecksum(payload);
        if (calculatedChecksum !== header.checksum) {
            console.error('Checksum mismatch');
            return;
        }

        // Handle different message types
        switch (header.type) {
            case MESSAGE_TYPES.HANDSHAKE:
                this.emit('handshake', header.sequence);
                break;
            case MESSAGE_TYPES.RESPONSE:
                this.emit('response', payload.toString());
                break;
            case MESSAGE_TYPES.ERROR:
                this.emit('serverError', payload.toString());
                break;
            default:
                console.log('Received message type:', header.type);
        }
    }

    // Close connection
    disconnect() {
        if (this.socket) {
            this.socket.end();
            this.socket = null;
            this.connected = false;
        }
    }
}

// Usage example
async function main() {
    const client = new PolyCallClient();

    client.on('handshake', (sequence) => {
        console.log('Handshake successful, sequence:', sequence);
        // After successful handshake, you can send commands
        client.sendCommand('status');
    });

    client.on('response', (data) => {
        console.log('Received response:', data);
    });

    client.on('error', (err) => {
        console.error('Client error:', err);
    });

    // Connect to the PolyCall server
    client.connect();

    // Clean up on process exit
    process.on('SIGINT', () => {
        console.log('\nDisconnecting...');
        client.disconnect();
        process.exit();
    });
}

if (require.main === module) {
    main().catch(console.error);
}

module.exports = PolyCallClient;