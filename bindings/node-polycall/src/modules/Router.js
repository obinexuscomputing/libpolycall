// Router.js - PolyCall Router Implementation
const EventEmitter = require('events');
const { URL } = require('url');

class Router extends EventEmitter {
    constructor(options = {}) {
        super();
        this.routes = new Map();
        this.middleware = [];
        this.stateEndpoints = new Map();
        this.options = {
            baseUrl: 'http://localhost',
            caseSensitive: false,
            strict: false,
            ...options
        };
    }

  // Route registration with method handling
  addRoute(path, handler, methods = ['GET']) {
    if (typeof path !== 'string' || !path.startsWith('/')) {
        throw new Error('Path must be a string starting with /');
    }

    // Convert handler object to function if needed
    const routeHandler = typeof handler === 'object' 
        ? this._createMethodHandler(handler)
        : handler;

    const route = {
        path: this.normalizePath(path),
        handler: routeHandler,
        methods: new Set(methods.map(m => m.toUpperCase())),
        middleware: []
    };

    this.routes.set(route.path, route);
    return this;
}

    // State endpoint mapping
    mapStateToEndpoint(state, endpoint) {
        if (!endpoint.startsWith('/')) {
            endpoint = `/${endpoint}`;
        }
        this.stateEndpoints.set(state.name, this.normalizePath(endpoint));
        
        // Add default routes for state operations
        this.addRoute(endpoint, async (ctx) => {
            return { state: state.verify() };
        }, ['GET']);

        this.addRoute(`${endpoint}/lock`, async (ctx) => {
            await state.lock();
            return { status: 'locked' };
        }, ['POST']);

        this.addRoute(`${endpoint}/unlock`, async (ctx) => {
            await state.unlock();
            return { status: 'unlocked' };
        }, ['POST']);

        return this;
    }
  // Create method-specific handler
  _createMethodHandler(handlers) {
    return async (ctx) => {
        const method = ctx.method.toUpperCase();
        const handler = handlers[method];
        
        if (!handler) {
            throw new Error(`Method ${method} not allowed`);
        }
        
        return handler(ctx);
    };
}
    // Middleware registration
    use(middleware) {
        if (typeof middleware !== 'function') {
            throw new Error('Middleware must be a function');
        }
        this.middleware.push(middleware);
        return this;
    }
;
         // Request handling
    async handleRequest(path, method = 'GET', data = {}) {
        const normalizedPath = this.normalizePath(path);
        const route = this.findRoute(normalizedPath);

        if (!route) {
            throw new Error(`No route found for path: ${path}`);
        }

        const context = {
            path: normalizedPath,
            method: method.toUpperCase(),
            params: this.extractParams(route.path, normalizedPath),
            query: this.parseQueryString(path),
            data,
            state: {},
            router: this
        };

        try {
            // Execute middleware chain
            await this.executeMiddlewareChain([...this.middleware, ...route.middleware], context);
            
            // Execute route handler
            const result = await route.handler(context);
            return result;
        } catch (error) {
            this.emit('error', error, context);
            throw error;
        }
    }
    
    // Route matching
    findRoute(path) {
        // First try exact match
        if (this.routes.has(path)) {
            return this.routes.get(path);
        }

        // Then try pattern matching
        for (const [routePath, route] of this.routes) {
            if (this.matchPath(routePath, path)) {
                return route;
            }
        }

        return null;
    }

     // Path matching
     matchPath(routePath, requestPath) {
        const routeParts = routePath.split('/').filter(Boolean);
        const requestParts = requestPath.split('/').filter(Boolean);

        if (routeParts.length !== requestParts.length) {
            return false;
        }

        for (let i = 0; i < routeParts.length; i++) {
            const routePart = routeParts[i];
            const requestPart = requestParts[i];

            if (routePart.startsWith(':')) {
                continue; // Parameter match
            }

            if (this.options.caseSensitive) {
                if (routePart !== requestPart) return false;
            } else {
                if (routePart.toLowerCase() !== requestPart.toLowerCase()) return false;
            }
        }

        return true;
    }


  // Parameter extraction
  extractParams(routePath, requestPath) {
    const params = {};
    const routeParts = routePath.split('/').filter(Boolean);
    const requestParts = requestPath.split('/').filter(Boolean);

    for (let i = 0; i < routeParts.length; i++) {
        if (routeParts[i].startsWith(':')) {
            const paramName = routeParts[i].slice(1);
            params[paramName] = requestParts[i];
        }
    }

    return params;
}

 // Parse query string
 parseQueryString(path) {
    try {
        const url = new URL(path, this.options.baseUrl);
        const params = {};
        url.searchParams.forEach((value, key) => {
            params[key] = value;
        });
        return params;
    } catch {
        return {};
    }
}

 // Path normalization
 normalizePath(path) {
    path = path.trim();
    if (!path.startsWith('/')) {
        path = '/' + path;
    }
    if (this.options.strict) {
        if (path.length > 1 && path.endsWith('/')) {
            path = path.slice(0, -1);
        }
    }
    return path;
}

 // Middleware execution
    async executeMiddlewareChain(middlewares, context) {
        let index = 0;
        const next = async () => {
            if (index >= middlewares.length) return;
            const middleware = middlewares[index++];
            await middleware(context, next);
        };
        await next();
    }
  
    // Get route handler - this was missing
    getRoute(path) {
        const normalizedPath = this.normalizePath(path);
        const route = this.findRoute(normalizedPath);
        
        if (!route) {
            return null;
        }

        return route.handler;
    }


    getStateEndpoints() {
        return Array.from(this.stateEndpoints.entries()).map(([state, endpoint]) => ({
            state,
            endpoint
        }));
    }

    clearRoutes() {
        this.routes.clear();
        this.stateEndpoints.clear();
        this.middleware = [];
    }

   // Debug routes
   printRoutes() {
    console.log('\nRegistered Routes:');
    for (const [path, route] of this.routes) {
        console.log(`${Array.from(route.methods).join(',')} ${path}`);
    }
}

    // State machine integration helpers
    bindStateMachine(stateMachine) {
        // Add routes for state machine operations
        this.addRoute('/states', async (ctx) => {
            return {
                current: stateMachine.getCurrentState()?.name,
                states: stateMachine.getStateNames()
            };
        });

        // Add route for state transitions
        this.addRoute('/transition/:state', async (ctx) => {
            const targetState = ctx.params.state;
            await stateMachine.executeTransition(targetState);
            return {
                success: true,
                currentState: stateMachine.getCurrentState().name
            };
        }, ['POST']);

        // Add routes for state inspection
        this.addRoute('/states/:state', async (ctx) => {
            const state = stateMachine.getState(ctx.params.state);
            return state.verify();
        });

        // Map all states to endpoints
        for (const stateName of stateMachine.getStateNames()) {
            const state = stateMachine.getState(stateName);
            this.mapStateToEndpoint(state, `/states/${stateName}`);
        }

        return this;
    }

    // Error handling middleware
    errorHandler() {
        return async (ctx, next) => {
            try {
                await next();
            } catch (error) {
                this.emit('error', error, ctx);
                throw error;
            }
        };
    }

    // Logging middleware
    loggingMiddleware() {
        return async (ctx, next) => {
            const start = Date.now();
            await next();
            const duration = Date.now() - start;
            this.emit('request', {
                method: ctx.method,
                path: ctx.path,
                duration
            });
        };
    }
}

module.exports = Router;