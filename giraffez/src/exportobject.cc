/*
 * Copyright 2016 Capital One Services, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Python.h>

// Teradata Parallel Transporter API
#include <connection.h>
#include <schema.h>

#include "common.h"
#include "connection.h"


typedef struct {
    PyObject_HEAD
    Giraffez::Connection *conn;
} Export;

static void Export_dealloc(Export *self) {
    delete self->conn;
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* Export_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    Export *self;
    self = (Export*)type->tp_alloc(type, 0);
    return (PyObject*)self;
}

static int Export_init(Export *self, PyObject *args, PyObject *kwargs) {
    char *host=NULL, *username=NULL, *password=NULL;
    if (!PyArg_ParseTuple(args, "sss", &host, &username, &password)) {
        return -1;
    }

    self->conn = new Giraffez::Connection(host, username, password);
    self->conn->AddAttribute(TD_SYSTEM_OPERATOR, TD_EXPORT);
    self->conn->AddAttribute(TD_TDP_ID, host);
    self->conn->AddAttribute(TD_USER_NAME, username);
    self->conn->AddAttribute(TD_USER_PASSWORD, password);

    // The min and max for sessions has been hard set to reasonable
    // values that *should* be one-size fits-all.
    //self->conn->AddAttribute(TD_MIN_SESSIONS, 5);
    //self->conn->AddAttribute(TD_MAX_SESSIONS, 32);
    self->conn->AddAttribute(TD_MIN_SESSIONS, 2);
    self->conn->AddAttribute(TD_MAX_SESSIONS, 5);
    self->conn->AddAttribute(TD_MAX_DECIMAL_DIGITS, 38);

    // Charset is set to prefer UTF8.  There may need to be changes to
    // the encoder if UTF8 is for whatever reason not supported, and
    // may cause unexpected behavior.
    self->conn->AddAttribute(TD_CHARSET, TERADATA_CHARSET);
    self->conn->AddAttribute(TD_BUFFER_MODE, "YES");
    self->conn->AddAttribute(TD_BLOCK_SIZE, 64330);
    self->conn->AddAttribute(TD_BUFFER_HEADER_SIZE, 2);
    self->conn->AddAttribute(TD_BUFFER_LENGTH_SIZE, 2);
    self->conn->AddAttribute(TD_BUFFER_MAX_SIZE, TD_ROW_MAX_SIZE);
    self->conn->AddAttribute(TD_BUFFER_TRAILER_SIZE, 0);

    // NoSpool sets the preferred spoolmode to attempt pulling the data
    // directly without spooling into temporary space.  In the event
    // that can't happen the job is still allowed but performs the
    // spooling of the query results as needed.
    self->conn->AddAttribute(TD_SPOOLMODE, "NoSpool");

    // Tenacity hours is set to the lowest allowed value.  In cases like
    // unexpected client disconnects without being shutdown properly,
    // the connection will, at a minimum, get discarded by the server
    // in 1 hour.  This should hopefully help prevent scenarios where
    // lots of dead connections are sitting around on the server
    // because of a malfunctioning client.
    self->conn->AddAttribute(TD_TENACITY_HOURS, 1);

    // Tenacity sleep is set to the lowest allowed value.  This ensures
    // that the connection will retry every second should the export
    // job get queued.
    self->conn->AddAttribute(TD_TENACITY_SLEEP, 1);
    return 0;
}

static PyObject* Export_add_attribute(Export *self, PyObject *args, PyObject *kwargs) {
    TD_Attribute key;
    PyObject *value = NULL;
    if (!PyArg_ParseTuple(args, "iO", &key, &value)) {
        return NULL;
    }
    return self->conn->AddAttribute(key, value);
}

static PyObject* Export_close(Export *self) {
    return self->conn->Terminate();
}

static PyObject* Export_columns(Export *self) {
    return self->conn->Columns();
}

static PyObject* Export_get_buffer(Export *self) {
    return self->conn->GetBuffer();
}

static PyObject* Export_set_encoding(Export *self, PyObject *args) {
    uint32_t new_settings = 0;
    uint32_t settings;
    if (!PyArg_ParseTuple(args, "i", &settings)) {
        return NULL;
    }
    if (settings & ROW_RETURN_MASK) {
        new_settings = (self->conn->encoder->Settings & ~ROW_RETURN_MASK) | settings;
    }
    if (settings & DATETIME_RETURN_MASK) {
        new_settings = (self->conn->encoder->Settings & ~DATETIME_RETURN_MASK) | settings;
    }
    if (settings & DECIMAL_RETURN_MASK) {
        new_settings = (self->conn->encoder->Settings & ~DECIMAL_RETURN_MASK) | settings;
    }
    if (encoder_set_encoding(self->conn->encoder, new_settings) != 0) {
        PyErr_Format(PyExc_ValueError, "Encoder set_encoding failed, bad encoding '0x%06x'.", settings);
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* Export_set_null(Export *self, PyObject *args) {
    PyObject *null = NULL;
    if (!PyArg_ParseTuple(args, "O", &null)) {
        return NULL;
    }
    Py_RETURN_ERROR(encoder_set_null(self->conn->encoder, null));
    Py_RETURN_NONE;
}

static PyObject* Export_set_delimiter(Export *self, PyObject *args) {
    PyObject *delimiter = NULL;
    if (!PyArg_ParseTuple(args, "O", &delimiter)) {
        return NULL;
    }
    Py_RETURN_ERROR(encoder_set_delimiter(self->conn->encoder, delimiter));
    Py_RETURN_NONE;
}

// TODO: ensure that multiple export jobs can run consecutively within
// the same context
static PyObject* Export_initiate(Export *self) {
    if ((self->conn->Initiate()) == NULL) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* Export_set_query(Export *self, PyObject *args, PyObject *kwargs) {
    char *query;
    if (!PyArg_ParseTuple(args, "s", &query)) {
        return NULL;
    }
    return self->conn->SetQuery(query);
}

static PyMethodDef Export_methods[] = {
    {"add_attribute", (PyCFunction)Export_add_attribute, METH_VARARGS, ""},
    {"close", (PyCFunction)Export_close, METH_NOARGS, ""},
    {"columns", (PyCFunction)Export_columns, METH_NOARGS, ""},
    {"get_buffer", (PyCFunction)Export_get_buffer, METH_NOARGS, ""},
    {"initiate", (PyCFunction)Export_initiate, METH_NOARGS, ""},
    {"set_encoding", (PyCFunction)Export_set_encoding, METH_VARARGS, ""},
    {"set_null", (PyCFunction)Export_set_null, METH_VARARGS, ""},
    {"set_delimiter", (PyCFunction)Export_set_delimiter, METH_VARARGS, ""},
    {"set_query", (PyCFunction)Export_set_query, METH_VARARGS, ""},
    {NULL}  /* Sentinel */
};

PyTypeObject ExportType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_tpt.Export",                                  /* tp_name */
    sizeof(Export),                                 /* tp_basicsize */
    0,                                              /* tp_itemsize */
    (destructor)Export_dealloc,                     /* tp_dealloc */
    0,                                              /* tp_print */
    0,                                              /* tp_getattr */
    0,                                              /* tp_setattr */
    0,                                              /* tp_compare */
    0,                                              /* tp_repr */
    0,                                              /* tp_as_number */
    0,                                              /* tp_as_sequence */
    0,                                              /* tp_as_mapping */
    0,                                              /* tp_hash */
    0,                                              /* tp_call */
    0,                                              /* tp_str */
    0,                                              /* tp_getattro */
    0,                                              /* tp_setattro */
    0,                                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,       /* tp_flags */
    "Export objects",                               /* tp_doc */
    0,                                              /* tp_traverse */
    0,                                              /* tp_clear */
    0,                                              /* tp_richcompare */
    0,                                              /* tp_weaklistoffset */
    0,                                              /* tp_iter */
    0,                                              /* tp_iternext */
    Export_methods,                                 /* tp_methods */
    0,                                              /* tp_members */
    0,                                              /* tp_getset */
    0,                                              /* tp_base */
    0,                                              /* tp_dict */
    0,                                              /* tp_descr_get */
    0,                                              /* tp_descr_set */
    0,                                              /* tp_dictoffset */
    (initproc)Export_init,                          /* tp_init */
    0,                                              /* tp_alloc */
    Export_new,                                     /* tp_new */
};
