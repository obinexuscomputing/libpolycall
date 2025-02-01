const http = require('http');

const options = {
    hostname: 'localhost',
    port: 8080,
    headers: {
        'Content-Type': 'application/json'
    }
};

// Improved request helper function
function makeRequest(method, path, data = null) {
    return new Promise((resolve, reject) => {
        const requestOptions = {
            ...options,
            path,
            method
        };

        const req = http.request(requestOptions, (res) => {
            let responseData = '';
            
            res.on('data', chunk => {
                responseData += chunk;
            });
            
            res.on('end', () => {
                try {
                    const parsedData = JSON.parse(responseData);
                    console.log(`${method} ${path} Response:`, parsedData);
                    resolve(parsedData);
                } catch (error) {
                    reject(new Error(`Failed to parse response: ${error.message}`));
                }
            });
        });

        req.on('error', (error) => {
            console.error(`${method} ${path} Error:`, error.message);
            reject(error);
        });

        if (data) {
            req.write(JSON.stringify(data));
        }
        req.end();
    });
}

// Test POST request
async function testPost() {
    const bookData = {
        title: 'Test Book',
        author: 'Test Author'
    };

    try {
        console.log('\nTesting POST /books...');
        const result = await makeRequest('POST', '/books', bookData);
        return result;
    } catch (error) {
        console.error('POST test failed:', error.message);
        throw error;
    }
}

// Test GET request
async function testGet() {
    try {
        console.log('\nTesting GET /books...');
        const result = await makeRequest('GET', '/books');
        return result;
    } catch (error) {
        console.error('GET test failed:', error.message);
        throw error;
    }
}

// Run all tests
async function runTests() {
    try {
        // First create a book
        const createdBook = await testPost();
        console.log('Created book successfully:', createdBook);

        // Then get all books
        const books = await testGet();
        console.log('Retrieved books successfully:', books);

    } catch (error) {
        console.error('Test suite failed:', error.message);
    }
}

// Run the tests
runTests();