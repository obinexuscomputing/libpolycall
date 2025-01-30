const http = require('http');
const PolyCallClient = require('../src/modules/PolyCallClient');
const Router = require('../src/modules/Router');
const StateMachine = require('../src/modules/StateMachine');
const State = require('../src/modules/State');
const NetworkEndpoint = require('../src/modules/NetworkEndpoint');
const { ProtocolHandler } = require('../src/modules/ProtocolHandler');

// Initialize PolyCall components
const stateMachine = new StateMachine();
const router = new Router();
const networkEndpoint = new NetworkEndpoint({ port: 8080 });
const protocolHandler = new ProtocolHandler();

// Initialize state machine
try {
    // Define states with explicit string names
    const initState = new State('INITIAL', { endpoint: '/initial' });
    const readyState = new State('READY', { endpoint: '/ready' });
    const runningState = new State('RUNNING', { endpoint: '/running' });
    const errorState = new State('ERROR', { endpoint: '/error' });

    // Add states to state machine
    stateMachine.addState(initState);
    stateMachine.addState(readyState);
    stateMachine.addState(runningState);
    stateMachine.addState(errorState);

    // Define valid transitions
    stateMachine.addTransition(initState.name, readyState.name);
    stateMachine.addTransition(readyState.name, runningState.name);
    stateMachine.addTransition(runningState.name, errorState.name);

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


// Initialize state machine with proper string state names
const states = {
    INIT: new State('INIT', { endpoint: '/init' }),
    READY: new State('READY', { endpoint: '/ready' }),
    RUNNING: new State('RUNNING', { endpoint: '/running' }),
    ERROR: new State('ERROR', { endpoint: '/error' })
};

// Add states and transitions
Object.values(states).forEach(state => {
    stateMachine.addState(state);
});

// Add valid state transitions
stateMachine.addTransition(states.INIT.name, states.READY.name);
stateMachine.addTransition(states.READY.name, states.RUNNING.name);
stateMachine.addTransition(states.RUNNING.name, states.ERROR.name);

// Simple in-memory data store
const store = {
  books: new Map(),
  users: new Map(),
  lendings: new Map()
};

// Authentication middleware
const authMiddleware = async (req, res, next) => {
    // Simple auth check - in production use proper auth
    const authToken = req.headers['authorization'];
    if (!authToken) {
        res.writeHead(401, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Unauthorized' }));
        return;
    }
    next();
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


router.addRoute('/users', {
    GET: async (ctx) => {
        return Array.from(store.users.values());
    },
    POST: async (ctx) => {
        const user = ctx.data;
        if (!user.email || !user.password) {
            throw new Error('User must have email and password');
        }
        const id = Date.now().toString();
        user.id = id;
        user.createdAt = new Date();
        store.users.set(id, user);
        return user;
    }
});

router.addRoute('/lendings', {
    GET: async (ctx) => {
        return Array.from(store.lendings.values());
    },
    POST: async (ctx) => {
        const { userId, bookId } = ctx.data;
        if (!userId || !bookId) {
            throw new Error('Lending must have userId and bookId');
        }
        
        const user = store.users.get(userId);
        const book = store.books.get(bookId);
        
        if (!user || !book) {
            throw new Error('User or book not found');
        }

        const lending = {
            id: Date.now().toString(),
            userId,
            bookId,
            lendDate: new Date(),
            dueDate: new Date(Date.now() + 14 * 24 * 60 * 60 * 1000) // 14 days
        };
        
        store.lendings.set(lending.id, lending);
        return lending;
    }
});

// Protocol handlers
protocolHandler.on('handshake', ({ sequence }) => {
    console.log('Handshake received:', sequence);
    try {
        stateMachine.executeTransition(states.INIT.name, states.READY.name);
    } catch (error) {
        console.error('Handshake error:', error);
    }
});

protocolHandler.on('command', async ({ command, data }) => {
    try {
        const response = await router.handleRequest(command, 'POST', data);
        protocolHandler.sendMessage(
            MESSAGE_TYPES.RESPONSE,
            response,
            PROTOCOL_FLAGS.RELIABLE
        );
    } catch (error) {
        protocolHandler.sendMessage(
            MESSAGE_TYPES.ERROR,
            error.message,
            PROTOCOL_FLAGS.RELIABLE
        );
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
                  'Access-Control-Allow-Methods': 'GET, POST'
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
// Error handler
server.on('error', (error) => {
    console.error('Server error:', error);
    try {
        stateMachine.executeTransition(states.RUNNING.name, states.ERROR.name);
    } catch (err) {
        console.error('State transition error:', err);
    }
});

// Start server
const port = 8080;
server.listen(port, () => {
    console.log(`Library server running at http://localhost:${port}`);
    try {
        // Transition to ready state
        stateMachine.executeTransition('INITIAL', 'READY');
    } catch (error) {
        console.error('Failed to transition to ready state:', error);
    }
});


// Handle shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down gracefully...');
  server.close(() => {
      polyCallClient.disconnect();
      console.log('Server stopped');
      process.exit(0);
  });
});