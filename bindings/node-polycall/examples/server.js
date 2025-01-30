const http = require('http');
const PolyCallClient = require('../src/modules/PolyCallClient');
const Router = require('../src/modules/Router');
const StateMachine = require('../src/modules/StateMachine');
const State = require('../src/modules/State');
const NetworkEndpoint = require('../src/modules/NetworkEndpoint');
const { ProtocolHandler, PROTOCOL_CONSTANTS, MESSAGE_TYPES, PROTOCOL_FLAGS } = require('../src/modules/ProtocolHandler');

// Initialize PolyCall components
const stateMachine = new StateMachine();
const router = new Router();
const networkEndpoint = new NetworkEndpoint({ port: 8080 });
const polyCallClient = new PolyCallClient({ endpoint: networkEndpoint, router, stateMachine });

// Bind state machine to router
router.bindStateMachine(stateMachine);

// Create HTTP server
const server = http.createServer(async (req, res) => {
    try {
        const result = await router.handleRequest(req.url, req.method, req);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(result));
    } catch (error) {
        res.writeHead(500, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: error.message }));
    }
});

// Start the server
server.listen(8080, () => {
    console.log('Server running at http://localhost:8080/');
});