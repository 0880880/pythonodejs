# pythonodejs
Embed Node.js natively in Python.

```
pip install pythonodejs
```

## Usage
```py
from pythonodejs import Node

node = Node()
node.eval("console.log('Hello, world');")
# > Hello world

readFile = node.eval("""
const fs = require('fs');

function readFile(filePath) {
    return fs.readFileSync(filePath, 'utf8');
}

readFile; // Returns readFile.
""")

print(readFile("hello.txt"))
# Calls readFile function

...

# pythonode automatically calls dispose.
```

## Building
1. Install **SConstruct** and **Clang**
2. Run `pip install -r requirements.txt`
3. Run `scons`

## License

This project is licensed under the [MIT License](LICENSE).

See the [LICENSE](LICENSE) file for more details.

## Special Thanks

A special thanks to [M-logique](https://github.com/M-logique) for their contribution to this project.
