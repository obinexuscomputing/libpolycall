const http = require('http');

const options = {
  hostname: 'localhost',
  port: 8080,
  headers: {
    'Content-Type': 'application/json'
  }
};
// Test POST request
const postData = JSON.stringify({
  title: 'Test Book',
  author: 'Test Author'
});

const postReq = http.request({
  ...options,
  path: '/books',
  method: 'POST'
}, (res) => {
  let data = '';
  
  res.on('data', chunk => {
    data += chunk;
  });
  
  res.on('end', () => {
    console.log('Created book:', JSON.parse(data));
    
    // After creating book, test GET request
    testGet();
  });
});

postReq.on('error', (error) => {
  console.error('Error:', error);
});

postReq.write(postData);
postReq.end();

// Test GET request
function testGet() {
  const getReq = http.request({
    ...options,
    path: '/books',
    method: 'GET'
  }, (res) => {
    let data = '';
    
    res.on('data', chunk => {
      data += chunk;
    });
    
    res.on('end', () => {
      console.log('Books list:', JSON.parse(data));
    });
  });

  getReq.on('error', (error) => {
    console.error('Error:', error);
  });

  getReq.end();
}