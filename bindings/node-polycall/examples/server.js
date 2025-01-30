const http = require('http');
const PolyCallClient = require('../src/modules/PolyCallClient');
const Router = require('../src/modules/Router');
const StateMachine = require('../src/modules/StateMachine');
const State = require('../src/modules/State');
const NetworkEndpoint = require('../src/modules/NetworkEndpoint');
const { ProtocolHandler } = require('../src/modules/ProtocolHandler');

// Initialize components
const router = new Router();
const networkEndpoint = new NetworkEndpoint({ port: 8080 });
const protocolHandler = new ProtocolHandler();

// Initialize state machine with proper state names
const stateMachine = new StateMachine({
    allowSelfTransitions: false,
    validateStateChange: true,
    recordHistory: true
});

try {
    // Create states with explicit string names
    const states = {
        init: stateMachine.addState('INIT', { endpoint: '/init' }),
        ready: stateMachine.addState('READY', { endpoint: '/ready' }),
        running: stateMachine.addState('RUNNING', { endpoint: '/running' }),
        error: stateMachine.addState('ERROR', { endpoint: '/error' })
    };

    // Add transitions between states
    stateMachine.addTransition('INIT', 'READY');
    stateMachine.addTransition('READY', 'RUNNING');
    stateMachine.addTransition('RUNNING', 'ERROR');
} catch (error) {
    console.error('Failed to initialize state machine:', error);
    process.exit(1);
}

// Create PolyCall client with initialized components
const polyCallClient = new PolyCallClient({ 
    endpoint: networkEndpoint, 
    router,
    stateMachine
});

// Simple in-memory data store
const store = {
    books: new Map(),
    users: new Map(),
    lendings: new Map()
};

// Route handlers
router.addRoute('/books', {
    GET: async (ctx) => {
        return Array.from(store.books.values());
    },
    POST: async (ctx) => {
        const book = ctx.data;
        if (!book.title || !book.author) {
            throw new Error('Book must have title and author');
        }
        const id = Date.now().toString();
        book.id = id;
        store.books.set(id, book);
        return book;
    }
});

// Create HTTP server
const server = http.createServer(async (req, res) => {
    try {
        let data = '';
        
        req.on('data', chunk => {
            data += chunk;
        });

        req.on('end', async () => {
            try {
                const requestData = data ? JSON.parse(data) : {};
                const result = await router.handleRequest(req.url, req.method, requestData);

                res.writeHead(200, { 
                    'Content-Type': 'application/json',
                    'Access-Control-Allow-Origin': '*',
                    'Access-Control-Allow-Methods': 'GET, POST',
                    'Access-Control-Allow-Headers': 'Content-Type'
                });
                res.end(JSON.stringify(result));

            } catch (error) {
                console.error('Request error:', error);
                res.writeHead(500, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: error.message }));
            }
        });

    } catch (error) {
        console.error('Server error:', error);
        res.writeHead(500, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Internal server error' }));
    }
});

// Start server
const port = 8080;
server.listen(port, () => {
    console.log(`Library server running at http://localhost:${port}`);
    try {
        // Transition from INIT to READY
        stateMachine.executeTransition('READY');
    } catch (error) {
        console.error('Failed to transition to ready state:', error);
    }
});

// Handle graceful shutdown
process.on('SIGINT', () => {
    console.log('\nShutting down gracefully...');
    server.close(() => {
        polyCallClient.disconnect();
        console.log('Server stopped');
        process.exit(0);
    });
});