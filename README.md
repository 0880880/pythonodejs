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
```

<details>
<summary>More Examples</summary>

### 🔄 Integration with NPM

```bash
npm init -y
npm install lodash express axios
```

```python
from pythonodejs import Node

node = Node()

result = node.eval("""
const _ = require('lodash');
const numbers = [1, 2, 3, 4, 5];
_.sum(numbers);
""")
print(f"Sum: {result}")

node.eval("""
const express = require('express');
const app = express();

app.get('/', (req, res) => {
  res.send('Hello from Express running in Python!');
});

app.listen(3000, () => {
  console.log('Express server running on port 3000');
});
""")
```

### 🔧 Advanced Usage

**Working with JS Objects**

```python
person = node.eval("""
const person = {
  name: 'John',
  age: 30,
  greet: function() {
    return `Hello, my name is ${this.name}`;
  }
};
person;
""")
print(person['name'])
print(person['greet']())
```

**Passing Python → JS**

```python
process_data = node.eval("""
function processData(data) {
  return {
    processed: true,
    items: data.items.map(x => x * 2),
    message: `Processed ${data.items.length} items`
  };
}
processData;
""")

result = process_data({'items': [1, 2, 3, 4, 5]})
print(result)
```

</details>

## ✅ ToDo

* [x] Stable release
* [ ] Better ES module loading
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
