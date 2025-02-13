# libpolycall

A Program First Data-Oriented Program Interface Implementation

## Author
OBINexusComputing - Nnamdi Michael Okpala

## Overview

libpolycall is a polymorphic call library that implements a program-first approach to interface design. Unlike traditional binding-centric approaches, libpolycall treats programs as first-class citizens, with bindings serving merely as code mappings.


## Why LibPolycall
### Program First vs Binding First

Traditional approaches:
- Focus on language-specific bindings
- Require separate implementations for each language
- Tight coupling between implementation and binding

libpolycall's approach:
- Programs drive the implementation
- Bindings are thin code mappings
- Implementation details remain with drivers
- Language agnostic core

## Architecture

libpolycall consists of:
- Core protocol implementation
- State machine management
- Network communication layer  
- Checksum and integrity verification
- Driver system for hardware/platform specifics

### Drivers vs Bindings

**Drivers:**
- Contain implementation-specific details
- Handle hardware/platform interactions
- Maintain their own state
- Implement core protocols

**Bindings:**
- Map language constructs to libpolycall APIs
- No implementation details
- Pure interface translation
- Lightweight and stateless

## Building from Source

### Prerequisites
```bash
# Required packages
sudo apt-get install build-essential cmake libssl-dev
```

### Build Steps
```bash 
# Clone repository
git clone https://gitlab.com/obinexuscomputing/libpolycall.git
cd libpolycall

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make

# Install
sudo make install
```

## Web Development Usage

### Node.js Example
```javascript
const { PolyCallClient } = require('node-polycall');

const client = new PolyCallClient({
  host: 'localhost',
  port: 8080
});

client.on('connected', () => {
  console.log('Connected to libpolycall server');
});

await client.connect();
```

### Browser Example
```javascript
import { PolyCallClient } from '@obinexuscomputing/polycall-web';

const client = new PolyCallClient({
  websocket: true,
  endpoint: 'ws://localhost:8080'
}); 

client.on('state:changed', ({from, to}) => {
  console.log(`State transition: ${from} -> ${to}`);
});
```

## Benefits

- Program-first design enables clean separation of concerns
- Drivers handle implementation details independently
- Bindings remain thin and maintainable
- Platform/language agnostic core protocol
- Strong state management and integrity checks
- Network transport flexibility

## License

MIT License. Copyright (c) OBINexusComputing.

## Contributing

Please read CONTRIBUTING.md for details on submitting pull requests.
