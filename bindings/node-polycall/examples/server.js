const http = require('http');
const Router = require('../src/modules/Router');
const StateMachine = require('../src/modules/StateMachine');
const State = require('../src/modules/State');

// Initialize components
const router = new Router();
const stateMachine = new StateMachine();

// Data store
const store = {
    books: new Map(),
    users: new Map(),
    lendings: new Map()
};

// Define route handlers
const bookHandlers = {
    GET: async (ctx) => {
        const books = Array.from(store.books.values());
        return { success: true, data: books };
    },
    POST: async (ctx) => {
        const book = ctx.data;
        if (!book.title || !book.author) {
            throw new Error('Book must have title and author');
        }
        const id = Date.now().toString();
        const newBook = {
            id,
            title: book.title,
            author: book.author,
            createdAt: new Date()
        };
        store.books.set(id, newBook);
        return { success: true, data: newBook };
    }
};

// Register routes
router.addRoute('/books', bookHandlers);

// CORS middleware
const corsMiddleware = async (req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    
    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return true;
    }
    return false;
};

// Request parser
const parseRequest = async (req) => {
    return new Promise((resolve, reject) => {
        let data = '';
        
        req.on('data', chunk => {
            data += chunk;
        });
        
        req.on('end', () => {
            try {
                const parsed = {
                    method: req.method,
                    url: req.url,
                    data: data ? JSON.parse(data) : {}
                };
                resolve(parsed);
            } catch (error) {
                reject(new Error('Invalid request data: ' + error.message));
            }
        });
        
        req.on('error', reject);
    });
};

// Create HTTP server
const server = http.createServer(async (req, res) => {
    try {
        // Handle CORS
        const corsHandled = await corsMiddleware(req, res);
        if (corsHandled) return;

        // Parse request
        const { method, url, data } = await parseRequest(req);

        // Handle request through router
        const result = await router.handleRequest(url, method, data);

        // Send response
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(result));

    } catch (error) {
        console.error('Request error:', error);
        const statusCode = error.message.includes('No route found') ? 404 : 500;
        res.writeHead(statusCode, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ 
            success: false, 
            error: error.message 
        }));
    }
});

// Start server
const port = 8080;
server.listen(port, () => {
    console.log(`Server running at http://localhost:${port}`);
    // Print registered routes for debugging
    router.printRoutes();
});

// Handle shutdown
process.on('SIGINT', () => {
    console.log('\nShutting down gracefully...');
    server.close(() => {
        console.log('Server stopped');
        process.exit(0);
    });
});