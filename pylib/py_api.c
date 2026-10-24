#include "book.h"
#include "ds.h"
#include "ison.h"
#include "node_net.h"
#include "router.h"
#include "wallet.h"
#include "zot.h"
#include <netinet/in.h>
#include <stdint.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

typedef struct {
  PyObject_HEAD;
  node_context_t ctx;
  int            inited;
} thrift_py_object;

static void thrift_py_object_dealloc(thrift_py_object *self) {
  if (self->inited)
    close_book(&self->ctx.book);
  self->inited = 0;
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static int thrift_py_object_init(thrift_py_object *self, PyObject *args,
                                 PyObject *kwargs) {
  if (self->inited) {
    PyErr_SetString(PyExc_RuntimeError, "already initialised");
    return -1;
  }

  char *password          = nullptr;
  int   create_if_missing = 0;
  memset(&self->ctx, 0, sizeof(self->ctx));

  self->ctx.user_keystore = init_keystore();
  if (!self->ctx.user_keystore) {
    PyErr_NoMemory();
    return -1;
  }

  if (!PyArg_ParseTupleAndKeywords(
          args, kwargs, "ziz|zzp",
          (char *[]){"address", "port", "password", "thrift_home",
                     "bootstrap_path", "create_if_missing", nullptr},
          &self->ctx.config.address, &self->ctx.config.port, &password,
          &self->ctx.config.thrift_path, &self->ctx.config.bootstrap_path,
          &create_if_missing)) {
    return -1;
  }

  if (!self->ctx.config.address) {
    PyErr_SetString(PyExc_ValueError, "address is required");
    return -1;
  }

  if (!password) {
    PyErr_SetString(PyExc_ValueError, "password is required");
    return -1;
  }

  self->ctx.config.address        = zstrdup(self->ctx.config.address);
  self->ctx.config.thrift_path    = zstrdup(self->ctx.config.thrift_path);
  self->ctx.config.bootstrap_path = zstrdup(self->ctx.config.bootstrap_path);

  self->ctx.book.lock_fd = -1;

  bool loaded_book;
  {
    Py_BEGIN_ALLOW_THREADS {
      loaded_book = load_book(&self->ctx.book, &self->ctx);
    }
    Py_END_ALLOW_THREADS
  }
  if (!loaded_book) {
    PyErr_SetString(PyExc_RuntimeError, "book loading failed");
    return -1;
  }

  // check if ks exists
  bool ks_exists = keystore_exists(&self->ctx);
  if (!ks_exists && !create_if_missing) {
    close_book(&self->ctx.book);
    PyErr_SetString(PyExc_RuntimeError,
                    "keystore not found and create_if_missing is False");
    return -1;
  }

  bool wallet_creation_failed = false;
  {
    Py_BEGIN_ALLOW_THREADS {
      wallet_creation_failed =
          !ks_exists &&
          !create_wallet(self->ctx.user_keystore, password, &self->ctx);
    }
    Py_END_ALLOW_THREADS
  }
  // create ks if it doesn't
  if (wallet_creation_failed) {
    close_book(&self->ctx.book);
    PyErr_SetString(PyExc_RuntimeError, "wallet creation failed");
    return -1;
  }

  bool loading_ks_failed = false;
  {
    Py_BEGIN_ALLOW_THREADS {
      loading_ks_failed = ks_exists && !load_keystore(self->ctx.user_keystore,
                                                      password, &self->ctx);
    }
    Py_END_ALLOW_THREADS
  }
  // load ks otherwise
  if (loading_ks_failed) {
    close_book(&self->ctx.book);
    PyErr_SetString(PyExc_RuntimeError, "keystore loading failed");
    return -1;
  }

  self->inited = 1;
  return 0;
}

static PyObject *thrift_py_object_get_wallet_address(thrift_py_object *self) {
  const char *addr = get_address_from_keystore(self->ctx.user_keystore);
  if (!addr) {
    PyErr_SetString(PyExc_RuntimeError, "no address in keystore");
    return nullptr;
  }
  return PyUnicode_FromString(addr);
}

static bool thrift_py_client_request(node_context_t *ctx, char **errmsg,
                                     char *method, char *path, char *body,
                                     uint64_t body_len, char *response,
                                     uint64_t  resp_cap,
                                     uint64_t *response_len) {

  int                server_fd = -1;
  struct sockaddr_in addr;
  int                addr_len = 0;
  if (!create_socket(&server_fd, ctx->config.address, ctx->config.port, true,
                     &addr, &addr_len)) {
    *errmsg = "Socket creation failed";
    return false;
  }

  bool request_ok = http_client_request(server_fd, method, path, body, body_len,
                                        response, resp_cap, response_len);
  shutdown(server_fd, SHUT_RDWR);
  close(server_fd);
  if (!request_ok) {
    *errmsg = "HTTP request failed";
    return false;
  }

  return true;
}

static bool map_cb(char *key, map_value *value, void *state);

static PyObject *json_value_to_py(json_value *value) {
  if (!value)
    Py_RETURN_NONE;

  switch (json_value_type(value)) {
  case NULLS:
    Py_RETURN_NONE;
  case BOOLEANS:
    return PyBool_FromLong(json_value_data(value).boolean ? 1 : 0);
  case INTEGERS:
    return PyLong_FromLongLong((long long)json_value_data(value).integer);
  case FLOATS:
    return PyFloat_FromDouble(json_value_data(value).number);
  case TEXT:
    return PyUnicode_FromString(json_value_data(value).string);
  case LIST: {
    array_t  *arr    = json_value_data(value).ptr;
    uint32_t  n_elem = array_length(arr);
    PyObject *list   = PyList_New((Py_ssize_t)n_elem);
    if (!list)
      return nullptr;
    for (uint32_t i = 0; i < n_elem; ++i) {
      PyObject *item = json_value_to_py(array_get_ptr(arr, i));
      if (!item) {
        Py_DECREF(list);
        return nullptr;
      }
      PyList_SET_ITEM(list, i, item);
    }
    return list;
  }
  case DICT: {
    hash_map *map  = json_value_data(value).ptr;
    PyObject *dict = PyDict_New();
    if (!dict)
      return nullptr;

    if (!hash_map_iterate(map, map_cb, dict)) {
      Py_DECREF(dict);
      return nullptr;
    }
    return dict;
  }
  default:
    Py_RETURN_NONE;
  }
}

static bool map_cb(char *key, map_value *value, void *state) {
  PyObject *dict     = state;
  PyObject *py_value = json_value_to_py(value);
  if (!py_value) {
    return false;
  }
  if (PyDict_SetItemString(dict, key, py_value) < 0) {
    Py_DECREF(py_value);
    return false;
  }
  Py_DECREF(py_value);
  return true;
}

static PyObject *thrift_py_http_json(node_context_t *ctx, const char *method,
                                     const char *path, const char *body,
                                     uint64_t body_len) {
  char    *errmsg = "request failed";
  char     response[BUFFER_BIG * 2];
  uint64_t response_len = 0;

  if (!thrift_py_client_request(ctx, &errmsg, (char *)method, (char *)path,
                                (char *)body, body_len, response,
                                sizeof(response), &response_len)) {
    PyErr_SetString(PyExc_RuntimeError, errmsg);
    return nullptr;
  }

  response[response_len] = 0;
  char *body_start       = http_find_body(response, response_len);
  if (!body_start)
    body_start = response;

  json_value *json = json_parse_string(body_start);
  if (!json) {
    PyErr_SetString(PyExc_RuntimeError, "invalid JSON from server");
    return nullptr;
  }

  PyObject *pyobj = json_value_to_py(json);
  return pyobj;
}

static PyObject *thrift_py_object_status(thrift_py_object *self) {
  return thrift_py_http_json(&self->ctx, "GET", "/status", nullptr, 0);
}

static PyObject *thrift_py_object_peers(thrift_py_object *self) {
  return thrift_py_http_json(&self->ctx, "GET", "/peers", nullptr, 0);
}

static PyObject *thrift_py_object_balances(thrift_py_object *self) {
  return thrift_py_http_json(&self->ctx, "GET", "/balances", nullptr, 0);
}

static PyObject *thrift_py_object_submit_tx(thrift_py_object *self,
                                            PyObject *args, PyObject *kwargs) {
  char *txn;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "z",
                                   (char *[]){"signed_tx", nullptr}, &txn)) {
    return nullptr;
  }
  if (!txn) {
    PyErr_SetString(PyExc_ValueError, "signed_tx is required");
    return nullptr;
  }
  return thrift_py_http_json(&self->ctx, "POST", "/add_tx", txn, strlen(txn));
}

static PyObject *thrift_py_object_sign_tx(thrift_py_object *self,
                                          PyObject *args, PyObject *kwargs) {
  tx_t txn = {get_address_from_keystore(self->ctx.user_keystore), nullptr};

  long long amount = 0;
  long long fee    = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "zL|L",
                                   (char *[]){"to", "amount", "fee", nullptr},
                                   &txn.to, &amount, &fee)) {
    PyErr_SetString(PyExc_RuntimeError, "malformed arguments");
    return nullptr;
  }

  txn.amount = (int64_t)amount;
  txn.fee    = (int64_t)fee;

  if (!txn.from) {
    PyErr_SetString(PyExc_RuntimeError, "keystore not initialised properly");
    return nullptr;
  }
  if (!txn.to) {
    PyErr_SetString(PyExc_RuntimeError, "to address is required");
    return nullptr;
  }

  copy_public_key(self->ctx.user_keystore, &txn);

  if (sign_tx(&txn, self->ctx.user_keystore)) {
    PyErr_SetString(PyExc_RuntimeError, "transaction signing failed");
    return nullptr;
  }
  PyObject *dict = PyDict_New();
  if (!dict)
    return nullptr;
  PyObject *dict_val;
#define SET(k, e)                                                              \
  do {                                                                         \
    dict_val = (e);                                                            \
    if (!dict_val)                                                             \
      goto fail;                                                               \
    if (PyDict_SetItemString(dict, k, dict_val) < 0) {                         \
      Py_DECREF(dict_val);                                                     \
      goto fail;                                                               \
    }                                                                          \
    Py_DECREF(dict_val);                                                       \
  } while (0)
  SET("amount", PyLong_FromLongLong(txn.amount));
  SET("fee", PyLong_FromLongLong(txn.fee));
  SET("from", PyUnicode_FromString(txn.from));
  SET("to", PyUnicode_FromString(txn.to));
  SET("public_key", PyBytes_FromStringAndSize((const char *)txn.public_key,
                                              PUBLIC_KEY_BIN_LEN));
  SET("signature",
      PyBytes_FromStringAndSize((const char *)txn.signature, ED25519_BIN_LEN));
#undef SET
  return dict;
fail:
  Py_DECREF(dict);
  return nullptr;
}

static PyMethodDef thrift_py_object_methods[] = {
    {"status", (PyCFunction)thrift_py_object_status, METH_NOARGS,
     "Get the on-chain status"},
    {"peers", (PyCFunction)thrift_py_object_peers, METH_NOARGS,
     "Get dedicated server peers"},
    {"sign_tx", (PyCFunction)thrift_py_object_sign_tx,
     METH_VARARGS | METH_KEYWORDS, "Create a signed transaction"},
    {"submit_tx", (PyCFunction)thrift_py_object_submit_tx,
     METH_VARARGS | METH_KEYWORDS, "Create a signed transaction"},
    {"balances", (PyCFunction)thrift_py_object_balances, METH_NOARGS,
     "Get on-chain balances"},
    {"wallet", (PyCFunction)thrift_py_object_get_wallet_address, METH_NOARGS,
     "Get dedicated client wallet address"},
    {nullptr}
};

static PyTypeObject thrift_py_type = {
    PyVarObject_HEAD_INIT(nullptr, 0)

        .tp_name  = "thriftproject.Thrift",
    .tp_doc       = "Thrift blockchain ledger API",
    .tp_basicsize = sizeof(thrift_py_object),
    .tp_itemsize  = 0,
    .tp_flags     = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new       = PyType_GenericNew,
    .tp_init      = (initproc)thrift_py_object_init,
    .tp_dealloc   = (destructor)thrift_py_object_dealloc,
    .tp_methods   = thrift_py_object_methods,
};

static struct PyModuleDef thriftmodule = {
    PyModuleDef_HEAD_INIT, "thriftproject", nullptr, -1, nullptr,
};

PyMODINIT_FUNC PyInit_thriftproject(void) {
  PyObject *module;
  if (PyType_Ready(&thrift_py_type) < 0)
    return nullptr;
  module = PyModule_Create(&thriftmodule);
  if (!module)
    return nullptr;

  Py_INCREF(&thrift_py_type);
  if (PyModule_AddObject(module, "Thrift", (PyObject *)&thrift_py_type) < 0) {
    Py_DECREF(&thrift_py_type);
    Py_DECREF(module);
    return nullptr;
  }
  return module;
}
