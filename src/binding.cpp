#include <Python.h>

// Dummy init to make it a valid Python extension
static PyModuleDef mod = {PyModuleDef_HEAD_INIT, "node", NULL, -1, NULL};

PyMODINIT_FUNC PyInit_pythonodejs(void) { return PyModule_Create(&mod); }