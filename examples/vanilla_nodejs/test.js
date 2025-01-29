// root/example/noderaw_example/test.js
const PolyCallClient = require('./polycall_client');

async function test() {
    const client = new PolyCallClient({
        reconnect: true,
        heartbeatInterval: 5000,
        responseTimeout: 5000
    });

    // Event handlers
    client.on('connected', () => {
        console.log('Client connected to server');
    });

    client.on('handshake', (sequence) => {
        console.log('Handshake complete, sequence:', sequence);
        runTests(client);
    });

    client.on('authenticated', () => {
        console.log('Client authenticated');
    });

    client.on('response', ({sequence, data}) => {
        console.log('Response received:', {sequence, data});
    });

    client.on('serverError', ({sequence, error}) => {
        console.error('Server error:', {sequence, error});
    });

    client.on('error', (error) => {
        console.error('Client error:', error);
    });

    client.on('disconnected', () => {
        console.log('Client disconnected');
    });

    // Connect to server
    console.log('Connecting to server...');
    client.connect();

    // Clean shutdown
    process.on('SIGINT', async () => {
        console.log('\nShutting down...');
        client.disconnect();
        process.exit();
    });
}

async function runTests(client) {
    try {
        console.log('\nRunning tests...');

        // Test status command
        console.log('\nTesting status command:');
        await client.sendCommand('status');

        // Test state listing
        console.log('\nTesting state listing:');
        await client.sendCommand('list_states');

        // Test endpoint listing
        console.log('\nTesting endpoint listing:');
        await client.sendCommand('list_endpoints');

        // Test client listing
        console.log('\nTesting client listing:');
        await client.sendCommand('list_clients');

        console.log('\nTests completed.');
    } catch (error) {
        console.error('Test error:', error);
    }
}

// Run tests if this is the main module
if (require.main === module) {
    test().catch(console.error);
}