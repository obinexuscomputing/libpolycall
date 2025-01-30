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
const protocolHandler = new ProtocolHandler();
const polyCallClient = new PolyCallClient({ 
  endpoint: networkEndpoint, 
  router,
  stateMachine
});

// Initialize state machine
const states = {
  INIT: new State('INIT'),
  READY: new State('READY'),
  RUNNING: new State('RUNNING')
};

// Add states and transitions
Object.values(states).forEach(state => {
  stateMachine.addState(state);
});

stateMachine.addTransition('INIT', 'READY');
stateMachine.addTransition('READY', 'RUNNING');

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
    const id = Date.now().toString();
    user.id = id;
    store.users.set(id, user);
    return user;
  }
});

// Protocol handlers
protocolHandler.on('handshake', ({ sequence }) => {
  console.log('Handshake received:', sequence);
  stateMachine.executeTransition('INIT', 'READY');
});

protocolHandler.on('command', ({ command, data }) => {
  try {
    const response = router.handleRequest(command, 'POST', data);
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
    const buffers = [];
    
    req.on('data', chunk => {
      buffers.push(chunk);
    });

    req.on('end', async () => {
      const data = Buffer.concat(buffers).toString();
      const requestData = data ? JSON.parse(data) : {};

      // Create protocol message
      const message = protocolHandler.createMessage(
        MESSAGE_TYPES.COMMAND,
        {
          path: req.url,
          method: req.method,
          data: requestData
        },
        PROTOCOL_FLAGS.RELIABLE
      );

      // Process request using PolyCall
      const result = await router.handleRequest(req.url, req.method, requestData);

      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(result));
    });

  } catch (error) {
    res.writeHead(500);
    res.end(JSON.stringify({ error: error.message }));
  }
});

// Error handler
server.on('error', (error) => {
  console.error('Server error:', error);
  stateMachine.executeTransition('ERROR');
});

// Start server
const port = 8080;
server.listen(port, () => {
  console.log(`Library server running at http://localhost:${port}`);
  stateMachine.executeTransition('READY', 'RUNNING');
});

// Handle shutdown
process.on('SIGINT', () => {
  console.log('Shutting down...');
  server.close(() => {
    polyCallClient.disconnect();
    process.exit(0);
  });
});