from pythonode import Node

node = Node()
node.eval("console.log('Hello, world');")

readFile = node.eval("""
const fs = require('fs');

function readFile(filePath) {
    return fs.readFileSync(filePath, 'utf8');
}

readFile; // Returns readFile.
""")

a = readFile("simple.txt")