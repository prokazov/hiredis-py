#include "pack.h"
#include <hiredis/hiredis.h>
#include <hiredis/sdsalloc.h>

PyObject *
pack_command(PyObject *cmd)
{
    assert(cmd);
    PyObject *result = NULL;

    if (cmd == NULL || !PyTuple_Check(cmd))
    {
        PyErr_SetString(PyExc_TypeError,
                        "The argument must be a tuple of str, int, float or bytes.");
        return NULL;
    }

    int tokens_number = PyTuple_Size(cmd);
    sds *tokens = s_malloc(sizeof(sds) * tokens_number);
    if (tokens == NULL)
    {
        return PyErr_NoMemory();
    }
    
    memset(tokens, 0, sizeof(sds) * tokens_number);

    size_t *lengths = hi_malloc(sizeof(size_t) * tokens_number);
    if (lengths == NULL)
    {
        sds_free(tokens);
        return PyErr_NoMemory();
    }

    Py_ssize_t len = 0;
    
    for (Py_ssize_t i = 0; i < PyTuple_Size(cmd); i++)
    {
        PyObject *item = PyTuple_GetItem(cmd, i);

        if (PyBytes_Check(item))
        {
            char *bytes = NULL;
            Py_buffer buffer;
            PyObject_GetBuffer(item, &buffer, PyBUF_SIMPLE);
            PyBytes_AsStringAndSize(item, &bytes, &len);
            tokens[i] = sdsempty();
            tokens[i] = sdscpylen(tokens[i], bytes, len);
            lengths[i] = buffer.len;
            PyBuffer_Release(&buffer);
        }
        else if (PyUnicode_Check(item))
        {
            const char *bytes = PyUnicode_AsUTF8AndSize(item, &len);
            if (bytes == NULL)
            {
                // PyUnicode_AsUTF8AndSize sets an exception.
                goto cleanup;
            }

            tokens[i] = sdsnewlen(bytes, len);
            lengths[i] = len;
        }
        else if (PyMemoryView_Check(item))
        {
            Py_buffer *p_buf = PyMemoryView_GET_BUFFER(item);
            tokens[i] = sdsnewlen(p_buf->buf, p_buf->len);
            lengths[i] = p_buf->len;
        }
        else
        {
            if (PyLong_CheckExact(item) || PyFloat_Check(item))
            {
                PyObject *repr = PyObject_Repr(item);
                const char *bytes = PyUnicode_AsUTF8AndSize(repr, &len);

                tokens[i] = sdsnewlen(bytes, len);
                lengths[i] = len;
                Py_DECREF(repr);
            }
            else
            {
                PyErr_SetString(PyExc_TypeError,
                                "A tuple item must be str, int, float or bytes.");
                goto cleanup;
            }
        }
    }

    char *resp_bytes = NULL;

    len = redisFormatCommandArgv(&resp_bytes, tokens_number, (const char **)tokens, lengths);

    if (len == -1)
    {
        PyErr_SetString(PyExc_RuntimeError,
                        "Failed to serialize the command.");
        goto cleanup;
    }

    result = PyBytes_FromStringAndSize(resp_bytes, len);
    hi_free(resp_bytes);
cleanup:
    sdsfreesplitres(tokens, tokens_number);
    hi_free(lengths);
    return result;
}