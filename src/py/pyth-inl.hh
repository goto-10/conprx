// Copyright 2014 the Neutrino authors (see AUTHORS).
// Licensed under the Apache License, Version 2.0 (see LICENSE).

// Python-related template method implementations.

#ifndef _PYTH_INL
#define _PYTH_INL

#include "pyth.hh"

namespace condrv {

template <typename T>
T &Python::incref(T &obj) {
#ifdef IS_GCC
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
  Py_INCREF(&obj);
#ifdef IS_GCC
#  pragma GCC diagnostic pop
#endif
  return obj;
}

// Initializing structs without passing all the field initializers yields a
// warning with strict warnings so we disable those selectively around the
// constructor that uses them.

#ifdef IS_GCC
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif // IS_GCC

template <typename T>
PyType<T>::PyType() {
  // Python appears to require inline initialization of type objects so we have
  // to do a little song and dance here. Wa-hey!
  PyTypeObject prototype = {PyObject_HEAD_INIT(NULL)};
  *static_cast<PyTypeObject*>(this) = prototype;
  Python::incref(*this);
  this->tp_basicsize = sizeof(T);
  this->tp_flags = Py_TPFLAGS_DEFAULT;
  memset(&this->sequence_methods_, 0, sizeof(this->sequence_methods_));
}

#ifdef IS_GCC
#  pragma GCC diagnostic pop
#endif // IS_GCC


template <typename T>
PyType<T> &PyType<T>::set_name(const char *name) {
  this->tp_name = name;
  this->tp_doc = name;
  return *this;
}

template <typename T>
PyType<T> &PyType<T>::set_new() {
  this->tp_new = T::create;
  return *this;
}

template <typename T>
PyType<T> &PyType<T>::set_dealloc() {
  this->tp_dealloc = T::dispose;
  return *this;
}

template <typename T>
PyType<T> &PyType<T>::set_str() {
  this->tp_str = T::to_string;
  return *this;
}

template <typename T>
PyType<T> &PyType<T>::set_repr() {
  this->tp_repr = T::to_representation;
  return *this;
}

template <typename T>
PyType<T> &PyType<T>::set_len() {
  this->sequence_methods_.sq_length = T::length;
  this->tp_as_sequence = &this->sequence_methods_;
  return *this;
}

template <typename T>
PyType<T> &PyType<T>::set_item() {
  this->sequence_methods_.sq_item = T::get_item;
  this->tp_as_sequence = &this->sequence_methods_;
  return *this;
}

template <typename T>
PyType<T> &PyType<T>::set_methods() {
  this->tp_methods = T::methods;
  return *this;
}

template <typename T>
PyType<T> &PyType<T>::set_members() {
  this->tp_members = T::members;
  return *this;
}

template <typename T>
bool PyType<T>::complete() {
  return PyType_Ready(this) >= 0;
}

template <typename T>
bool PyType<T>::is_instance(PyObject *obj) {
  return PyObject_IsInstance(obj, reinterpret_cast<PyObject*>(this));
}

template <typename T>
T *PyType<T>::cast(PyObject *obj) {
  if (is_instance(obj)) {
    return static_cast<T*>(obj);
  } else {
    PyErr_SetString(PyExc_TypeError, "Cast failed");
    return NULL;
  }
}

template <typename T>
PyType<T> &PyType<T>::bind(PyObject *module, const char *name) {
  PyModule_AddObject(module, name, reinterpret_cast<PyObject*>(this));
  return *this;
}

} // condrv

#endif // _PYTH_INL
