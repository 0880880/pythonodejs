# pythonodejs 🐍➕🟢

[![PyPI version](https://img.shields.io/pypi/v/pythonodejs.svg)](https://pypi.org/project/pythonodejs/)
[![Python versions](https://img.shields.io/pypi/pyversions/pythonodejs.svg)](https://pypi.org/project/pythonodejs/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Downloads](https://img.shields.io/pypi/dm/pythonodejs.svg)](https://pypi.org/project/pythonodejs/)

**Embed Node.js natively in Python with seamless integration.**

## 📋 Overview

`pythonodejs` lets you harness the power of the Node.js ecosystem directly within your Python applications. Run JavaScript code, use NPM packages, and leverage the best of both worlds in a single application.

## 🚀 Installation

```bash
pip install pythonodejs
```

## 🔍 Quick Example

```python
from pythonodejs import Node

# Initialize Node.js environment
node = Node()

# Execute JavaScript directly
node.eval("console.log('Hello from Node.js in Python!')")
# > Hello from Node.js in Python!

# Create and return JavaScript functions
read_file = node.eval("""
const fs = require('fs');

function readFile(filePath) {
    return fs.readFileSync(filePath, 'utf8');
}

readFile; // Return the function
""")

# Call JavaScript functions from Python
content = read_file("example.txt")
print(content)

# pythonodejs automatically handles cleanup
```

## 🔄 Integration with NPM

`pythonodejs` works seamlessly with NPM packages. Simply initialize an NPM project in your working directory and install the packages you need:

```bash
# Initialize an NPM project
npm init -y

# Install required packages
npm install lodash express axios

# Now use these packages directly in your Python code
```

Then in your Python application:

```python
from pythonodejs import Node

node = Node()

# Use lodash from NPM
result = node.eval("""
const _ = require('lodash');
const numbers = [1, 2, 3, 4, 5];
_.sum(numbers);
""")
print(f"Sum: {result}")  # Sum: 15

# Use Express to create a simple server
node.eval("""
const express = require('express');
const app = express();

app.get('/', (req, res) => {
  res.send('Hello from Express running in Python!');
});

app.listen(3000, () => {
  console.log('Express server running on port 3000');
}); // Execution will be stopped if node goes out of scope/python execution ends
""")

# pythonodejs will automatically use the node_modules directory
```

## 🔧 Advanced Usage

### Working with JavaScript objects

```python
from pythonodejs import Node

node = Node()

# Create a JavaScript object
person = node.eval("""
const person = {
  name: 'John',
  age: 30,
  greet: function() {
    return `Hello, my name is ${this.name}`;
  }
};
person;  // Return the object
""")

# Access properties
print(person['name'])  # John
print(person['age'])   # 30

# Call methods
greeting = person['greet']()
print(greeting)  # Hello, my name is John
```

### Passing Python values to JavaScript

```python
from pythonodejs import Node

node = Node()

# Define a JavaScript function
process_data = node.eval("""
function processData(data) {
  return {
    processed: true,
    items: data.items.map(x => x * 2),
    message: `Processed ${data.items.length} items`
  };
}
processData;  // Return the function
""")

# Call with Python dict
result = process_data({
  'items': [1, 2, 3, 4, 5]
})

print(result)  
# {'processed': True, 'items': [2, 4, 6, 8, 10], 'message': 'Processed 5 items'}
```

## ✅ ToDo

* [x] Stable release
* [ ] Fix function context
* [ ] ES Module loading
* [ ] BIGINT support
* [ ] Async/await bridging between Python ↔ JS
* [ ] Automatic npm install on dependency detection
* [ ] Better error logging

## 🏗️ Building from Source

1. Install **SConstruct** and **Clang**
2. Run `pip install -r requirements.txt`
3. Run `scons`

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📜 License

This project is licensed under the [MIT License](LICENSE).

## 🙏 Special Thanks

A special thanks to [M-logique](https://github.com/M-logique) for their contribution to this project.
