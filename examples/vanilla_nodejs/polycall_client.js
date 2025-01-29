const net = require('net');
const EventEmitter = require('events');

// Protocol constants
const PROTOCOL_VERSION = 1;
const PROTOCOL_MAGIC = 0x504C43; // "PLC"
const HEADER_SIZE = 16;

const MESSAGE_TYPES = {
    HANDSHAKE: 0x01,
    AUTH: 0x02,
    COMMAND: 0x03,
    RESPONSE: 0x04,
    ERROR: 0x05,
    HEARTBEAT: 0x06
};

const PROTOCOL_FLAGS = {
    NONE: 0x00,
    ENCRYPTED: 0x01,
    COMPRESSED: 0x02,
    URGENT: 0x04,
    RELIABLE: 0x08
};

class PolyCallClient extends EventEmitter {
    constructor(options = {}) {
        super();
        this.options = {
            reconnect: true,
            heartbeatInterval: 5000,
            responseTimeout: 5000,
            maxRetries: 3,
            ...options
        };

        this.socket = null;
        this.connected = false;
        this.authenticated = false;
        this.sequence = 1;
        this.pendingResponses = new Map();
        this.heartbeatTimer = null;
        this.reconnectAttempts = 0;
        this.messageQueue = [];
    }

    // Connect to PolyCall server
    connect(port = 8080, host = 'localhost') {
        this.port = port;
        this.host = host;
        this.establishConnection();
    }

    // Establish socket connection
    establishConnection() {
        this.socket = new net.Socket();

        this.socket.on('connect', () => {
            console.log('Connected to PolyCall server');
            this.connected = true;
            this.reconnectAttempts = 0;
            this.emit('connected');
            this.startHeartbeat();
            this.sendHandshake();
            this.processQueuedMessages();
        });

        this.socket.on('data', (data) => {
            this.handleMessage(data);
        });

        this.socket.on('close', () => {
            this.handleDisconnect();
        });

        this.socket.on('error', (err) => {
            console.error('Socket error:', err);
            this.emit('error', err);
        });

        this.socket.connect(this.port, this.host);
    }

    // Create message header
    createHeader(type, payloadLength, flags = PROTOCOL_FLAGS.NONE) {
        const header = Buffer.alloc(HEADER_SIZE);
        header.writeUInt8(PROTOCOL_VERSION, 0); // Version
        header.writeUInt8(type, 1); // Message type
        header.writeUInt16LE(flags, 2); // Flags
        header.writeUInt32LE(this.sequence++, 4); // Sequence number
        header.writeUInt32LE(payloadLength, 8); // Payload length
        header.writeUInt32LE(0, 12); // Checksum placeholder
        return header;
    }

    // Calculate message checksum
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
        payload.writeUInt32LE(PROTOCOL_MAGIC, 0);
        payload.writeUInt32LE(0, 4); // Reserved

        const header = this.createHeader(
            MESSAGE_TYPES.HANDSHAKE,
            payload.length,
            PROTOCOL_FLAGS.RELIABLE
        );
        
        this.sendMessage(header, payload);
    }

    // Send authenticated command
    async sendCommand(command, timeout = this.options.responseTimeout) {
        if (!this.connected) {
            if (this.options.reconnect) {
                this.messageQueue.push({ command, timeout });
                this.establishConnection();
                return;
            }
            throw new Error('Not connected to server');
        }

        const payload = Buffer.from(command);
        const header = this.createHeader(
            MESSAGE_TYPES.COMMAND,
            payload.length,
            PROTOCOL_FLAGS.RELIABLE
        );

        return new Promise((resolve, reject) => {
            const sequence = header.readUInt32LE(4);
            const timer = setTimeout(() => {
                this.pendingResponses.delete(sequence);
                reject(new Error('Command timeout'));
            }, timeout);

            this.pendingResponses.set(sequence, { resolve, reject, timer });
            this.sendMessage(header, payload);
        });
    }

    // Send protocol message
    sendMessage(header, payload) {
        const message = Buffer.concat([header, payload]);
        const checksum = this.calculateChecksum(payload);
        message.writeUInt32LE(checksum, 12); // Set checksum in header
        this.socket.write(message);
    }

    // Handle incoming messages
    handleMessage(data) {
        if (data.length < HEADER_SIZE) {
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

        const payload = data.slice(HEADER_SIZE, HEADER_SIZE + header.payloadLength);
        
        // Verify checksum
        const calculatedChecksum = this.calculateChecksum(payload);
        if (calculatedChecksum !== header.checksum) {
            console.error('Checksum mismatch');
            return;
        }

        this.processMessage(header, payload);
    }

    // Process verified message
    processMessage(header, payload) {
        switch (header.type) {
            case MESSAGE_TYPES.HANDSHAKE:
                this.emit('handshake', header.sequence);
                break;

            case MESSAGE_TYPES.AUTH:
                this.authenticated = true;
                this.emit('authenticated', header.sequence);
                break;

            case MESSAGE_TYPES.RESPONSE: {
                const pending = this.pendingResponses.get(header.sequence);
                if (pending) {
                    clearTimeout(pending.timer);
                    this.pendingResponses.delete(header.sequence);
                    pending.resolve({
                        sequence: header.sequence,
                        data: payload.toString()
                    });
                }
                this.emit('response', {
                    sequence: header.sequence,
                    data: payload.toString()
                });
                break;
            }

            case MESSAGE_TYPES.ERROR: {
                const pending = this.pendingResponses.get(header.sequence);
                if (pending) {
                    clearTimeout(pending.timer);
                    this.pendingResponses.delete(header.sequence);
                    pending.reject(new Error(payload.toString()));
                }
                this.emit('serverError', {
                    sequence: header.sequence,
                    error: payload.toString()
                });
                break;
            }

            case MESSAGE_TYPES.HEARTBEAT:
                this.emit('heartbeat', header.sequence);
                break;

            default:
                console.warn('Unknown message type:', header.type);
        }
    }

    // Handle disconnection
    handleDisconnect() {
        this.connected = false;
        this.authenticated = false;
        this.stopHeartbeat();
        
        // Clear pending responses
        for (const [sequence, pending] of this.pendingResponses) {
            clearTimeout(pending.timer);
            pending.reject(new Error('Connection closed'));
            this.pendingResponses.delete(sequence);
        }

        this.emit('disconnected');

        // Attempt reconnection if enabled
        if (this.options.reconnect && 
            this.reconnectAttempts < this.options.maxRetries) {
            this.reconnectAttempts++;
            setTimeout(() => {
                console.log(`Attempting reconnection (${this.reconnectAttempts}/${this.options.maxRetries})`);
                this.establishConnection();
            }, 1000 * this.reconnectAttempts);
        }
    }

    // Start heartbeat
    startHeartbeat() {
        this.stopHeartbeat();
        this.heartbeatTimer = setInterval(() => {
            if (this.connected) {
                const header = this.createHeader(
                    MESSAGE_TYPES.HEARTBEAT,
                    0,
                    PROTOCOL_FLAGS.NONE
                );
                this.sendMessage(header, Buffer.alloc(0));
            }
        }, this.options.heartbeatInterval);
    }

    // Stop heartbeat
    stopHeartbeat() {
        if (this.heartbeatTimer) {
            clearInterval(this.heartbeatTimer);
            this.heartbeatTimer = null;
        }
    }

    // Process queued messages after reconnection
    processQueuedMessages() {
        while (this.messageQueue.length > 0) {
            const { command, timeout } = this.messageQueue.shift();
            this.sendCommand(command, timeout).catch(console.error);
        }
    }

    // Graceful disconnect
    disconnect() {
        this.options.reconnect = false;
        this.stopHeartbeat();
        
        if (this.socket) {
            this.socket.end();
            this.socket = null;
        }
        
        this.connected = false;
        this.authenticated = false;
        this.messageQueue = [];
    }
}

module.exports = PolyCallClient;