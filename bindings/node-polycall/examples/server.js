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

// Initialize state machine
const stateMachine = new StateMachine({
    allowSelfTransitions: false,
    validateStateChange: true,
    recordHistory: true
});

// Initialize states
try {
    const states = {
        init: stateMachine.addState('INIT', { endpoint: '/init' }),
        ready: stateMachine.addState('READY', { endpoint: '/ready' }),
        running: stateMachine.addState('RUNNING', { endpoint: '/running' }),
        error: stateMachine.addState('ERROR', { endpoint: '/error' })
    };

    stateMachine.addTransition('INIT', 'READY');
    stateMachine.addTransition('READY', 'RUNNING');
    stateMachine.addTransition('RUNNING', 'ERROR');
} catch (error) {
    console.error('Failed to initialize state machine:', error);
    process.exit(1);
}

// Create PolyCall client
const polyCallClient = new PolyCallClient({ 
    endpoint: networkEndpoint, 
    router,
    stateMachine
});

// Data store
const store = {
    books: new Map(),
    users: new Map(),
    lendings: new Map()
};

// Register routes
router.addRoute('/books', {
    get: async (ctx) => {
        return Array.from(store.books.values());
    },
    post: async (ctx) => {
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

// CORS headers
const corsHeaders = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Access-Control-Max-Age': '86400'
};

// Create HTTP server
const server = http.createServer(async (req, res) => {
    // Handle CORS preflight
    if (req.method === 'OPTIONS') {
        res.writeHead(204, corsHeaders);
        res.end();
        return;
    }

    try {
        let data = '';
        
        req.on('data', chunk => {
            data += chunk;
        });

        req.on('end', async () => {
            try {
                const requestData = data ? JSON.parse(data) : {};
                
                // Handle the request using the router
                const method = req.method.toLowerCase();
                const path = req.url;
                
                const context = {
                    method,
                    path,
                    data: requestData,
                    headers: req.headers
                };

                const handler = router.findRoute(path)?.[method];
                
                if (!handler) {
                    throw new Error(`No handler found for ${method} ${path}`);
                }

                const result = await handler(context);

                res.writeHead(200, { 
                    'Content-Type': 'application/json',
                    ...corsHeaders
                });
                res.end(JSON.stringify(result));

            } catch (error) {
                console.error('Request error:', error);
                res.writeHead(500, { 
                    'Content-Type': 'application/json',
                    ...corsHeaders 
                });
                res.end(JSON.stringify({ error: error.message }));
            }
        });

    } catch (error) {
        console.error('Server error:', error);
        res.writeHead(500, { 
            'Content-Type': 'application/json',
            ...corsHeaders 
        });
        res.end(JSON.stringify({ error: 'Internal server error' }));
    }
});

// Error handler
server.on('error', (error) => {
    console.error('Server error:', error);
    try {
        stateMachine.executeTransition('ERROR');
    } catch (err) {
        console.error('State transition error:', err);
    }
});

// Start server
const port = 8080;
server.listen(port, () => {
    console.log(`Library server running at http://localhost:${port}`);
    try {
        stateMachine.executeTransition('READY');
    } catch (error) {
        console.error('Failed to transition to ready state:', error);
    }
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('\nShutting down gracefully...');
    server.close(() => {
        polyCallClient.disconnect();
        console.log('Server stopped');
        process.exit(0);
    });
});