// GhostRider PoW hashing for the functional test framework.
//
// This is a *binding* over Raptoreum Core's own implementation, not a
// reimplementation: it compiles src/hash_selection.cpp and the crypto
// primitives directly, so the hash it returns is by construction the hash the
// node computes in CBlockHeader::ComputeHash().
//
// That matters because GhostRider selects its algorithm sequence from the
// previous block hash (see HashSelection), so a separate implementation can be
// correct for some headers and wrong for others -- a failure mode that spot
// checks do not catch.

#include <Python.h>

#include <hash.h>
#include <uint256.h>

#include <cstring>

PyDoc_STRVAR(getPoWHash_doc,
"getPoWHash(header) -> bytes\n\n"
"Return the 32-byte GhostRider proof-of-work hash of a block header.\n"
"`header` must be at least 36 bytes; bytes 4..36 are the previous block hash,\n"
"which selects the algorithm sequence. Normally an 80-byte serialised header.\n"
"The result is in internal (little-endian) byte order, matching the node.");

static PyObject *ghostrider_getPoWHash(PyObject *self, PyObject *args)
{
    const char *input = nullptr;
    Py_ssize_t len = 0;

    if (!PyArg_ParseTuple(args, "y#:getPoWHash", &input, &len)) {
        return nullptr;
    }
    if (len < 36) {
        PyErr_SetString(PyExc_ValueError,
                        "header too short: need at least 36 bytes to read the previous block hash");
        return nullptr;
    }

    uint256 prev_block_hash;
    std::memcpy(prev_block_hash.begin(), input + 4, 32);

    uint256 result;
    Py_BEGIN_ALLOW_THREADS
    result = HashGR(input, input + len, prev_block_hash);
    Py_END_ALLOW_THREADS

    return PyBytes_FromStringAndSize(reinterpret_cast<const char *>(result.begin()), 32);
}

static PyMethodDef ghostrider_methods[] = {
    {"getPoWHash", ghostrider_getPoWHash, METH_VARARGS, getPoWHash_doc},
    {nullptr, nullptr, 0, nullptr},
};

static struct PyModuleDef ghostrider_module = {
    PyModuleDef_HEAD_INIT,
    "raptoreum_hash",
    "GhostRider proof-of-work hashing, bound to Raptoreum Core's own implementation.",
    -1,
    ghostrider_methods,
    nullptr, nullptr, nullptr, nullptr,
};

PyMODINIT_FUNC PyInit_raptoreum_hash(void)
{
    return PyModule_Create(&ghostrider_module);
}
