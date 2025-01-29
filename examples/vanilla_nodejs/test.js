const PolyCallClient = require('./polycall_client');

async function test() {
    const client = new PolyCallClient();

    // Set up event handlers
    client.on('handshake', (sequence) => {
        console.log('Handshake complete, running tests...');
        
        // Test various commands
        setTimeout(() => client.sendCommand('status'), 1000);
        setTimeout(() => client.sendCommand('list_states'), 2000);
        setTimeout(() => client.sendCommand('list_endpoints'), 3000);
    });

    client.on('response', (data) => {
        console.log('Server response:', data);
    });

    client.on('serverError', (error) => {
        console.error('Server error:', error);
    });

    client.on('error', (error) => {
        console.error('Client error:', error);
    });

    // Connect to server
    console.log('Connecting to PolyCall server...');
    client.connect();

    // Clean shutdown
    process.on('SIGINT', () => {
        console.log('\nShutting down...');
        client.disconnect();
        process.exit();
    });
}

test().catch(console.error);