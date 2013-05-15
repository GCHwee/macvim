/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */
/*
 * Python extensions by Paul Moore.
 * Changes for Unix by David Leonard.
 *
 * This consists of four parts:
 * 1. Python interpreter main program
 * 2. Python output stream: writes output via [e]msg().
 * 3. Implementation of the Vim module for Python
 * 4. Utility functions for handling the interface between Vim and Python.
 */

#include "vim.h"

#include <limits.h>

/* Python.h defines _POSIX_THREADS itself (if needed) */
#ifdef _POSIX_THREADS
# undef _POSIX_THREADS
#endif

#if defined(_WIN32) && defined(HAVE_FCNTL_H)
# undef HAVE_FCNTL_H
#endif

#ifdef _DEBUG
# undef _DEBUG
#endif

#ifdef HAVE_STDARG_H
# undef HAVE_STDARG_H	/* Python's config.h defines it as well. */
#endif
#ifdef _POSIX_C_SOURCE
# undef _POSIX_C_SOURCE	/* pyconfig.h defines it as well. */
#endif
#ifdef _XOPEN_SOURCE
# undef _XOPEN_SOURCE	/* pyconfig.h defines it as well. */
#endif

#include <Python.h>
#if defined(MACOS) && !defined(MACOS_X_UNIX)
# include "macglue.h"
# include <CodeFragments.h>
#endif
#undef main /* Defined in python.h - aargh */
#undef HAVE_FCNTL_H /* Clash with os_win32.h */

#if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02050000
# define PY_SSIZE_T_CLEAN
#endif

static void init_structs(void);

#define PyBytes_FromString PyString_FromString
#define PyBytes_Check PyString_Check

/* No-op conversion functions, use with care! */
#define PyString_AsBytes(obj) (obj)
#define PyString_FreeBytes(obj)

#if !defined(FEAT_PYTHON) && defined(PROTO)
/* Use this to be able to generate prototypes without python being used. */
# define PyObject Py_ssize_t
# define PyThreadState Py_ssize_t
# define PyTypeObject Py_ssize_t
struct PyMethodDef { Py_ssize_t a; };
# define PySequenceMethods Py_ssize_t
#endif

#if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02070000
# define PY_USE_CAPSULE
#endif

#if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02050000
# define PyInt Py_ssize_t
# define PyInquiry lenfunc
# define PyIntArgFunc ssizeargfunc
# define PyIntIntArgFunc ssizessizeargfunc
# define PyIntObjArgProc ssizeobjargproc
# define PyIntIntObjArgProc ssizessizeobjargproc
# define Py_ssize_t_fmt "n"
#else
# define PyInt int
# define lenfunc inquiry
# define PyInquiry inquiry
# define PyIntArgFunc intargfunc
# define PyIntIntArgFunc intintargfunc
# define PyIntObjArgProc intobjargproc
# define PyIntIntObjArgProc intintobjargproc
# define Py_ssize_t_fmt "i"
#endif

/* Parser flags */
#define single_input	256
#define file_input	257
#define eval_input	258

#if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x020300F0
  /* Python 2.3: can invoke ":python" recursively. */
# define PY_CAN_RECURSE
#endif

# if defined(DYNAMIC_PYTHON) || defined(PROTO)
#  ifndef DYNAMIC_PYTHON
#   define HINSTANCE long_u		/* for generating prototypes */
#  endif

# ifndef WIN3264
#  include <dlfcn.h>
#  define FARPROC void*
#  define HINSTANCE void*
#  if defined(PY_NO_RTLD_GLOBAL) && defined(PY3_NO_RTLD_GLOBAL)
#   define load_dll(n) dlopen((n), RTLD_LAZY)
#  else
#   define load_dll(n) dlopen((n), RTLD_LAZY|RTLD_GLOBAL)
#  endif
#  define close_dll dlclose
#  define symbol_from_dll dlsym
# else
#  define load_dll vimLoadLib
#  define close_dll FreeLibrary
#  define symbol_from_dll GetProcAddress
# endif

/* This makes if_python.c compile without warnings against Python 2.5
 * on Win32 and Win64. */
# undef PyRun_SimpleString
# undef PyRun_String
# undef PyArg_Parse
# undef PyArg_ParseTuple
# undef Py_BuildValue
# undef Py_InitModule4
# undef Py_InitModule4_64
# undef PyObject_CallMethod

/*
 * Wrapper defines
 */
# define PyArg_Parse dll_PyArg_Parse
# define PyArg_ParseTuple dll_PyArg_ParseTuple
# define PyMem_Free dll_PyMem_Free
# define PyMem_Malloc dll_PyMem_Malloc
# define PyDict_SetItemString dll_PyDict_SetItemString
# define PyErr_BadArgument dll_PyErr_BadArgument
# define PyErr_Clear dll_PyErr_Clear
# define PyErr_PrintEx dll_PyErr_PrintEx
# define PyErr_NoMemory dll_PyErr_NoMemory
# define PyErr_Occurred dll_PyErr_Occurred
# define PyErr_SetNone dll_PyErr_SetNone
# define PyErr_SetString dll_PyErr_SetString
# define PyErr_SetObject dll_PyErr_SetObject
# define PyEval_InitThreads dll_PyEval_InitThreads
# define PyEval_RestoreThread dll_PyEval_RestoreThread
# define PyEval_SaveThread dll_PyEval_SaveThread
# ifdef PY_CAN_RECURSE
#  define PyGILState_Ensure dll_PyGILState_Ensure
#  define PyGILState_Release dll_PyGILState_Release
# endif
# define PyInt_AsLong dll_PyInt_AsLong
# define PyInt_FromLong dll_PyInt_FromLong
# define PyLong_AsLong dll_PyLong_AsLong
# define PyLong_FromLong dll_PyLong_FromLong
# define PyBool_Type (*dll_PyBool_Type)
# define PyInt_Type (*dll_PyInt_Type)
# define PyLong_Type (*dll_PyLong_Type)
# define PyList_GetItem dll_PyList_GetItem
# define PyList_Append dll_PyList_Append
# define PyList_New dll_PyList_New
# define PyList_SetItem dll_PyList_SetItem
# define PyList_Size dll_PyList_Size
# define PyList_Type (*dll_PyList_Type)
# define PySequence_Check dll_PySequence_Check
# define PySequence_Size dll_PySequence_Size
# define PySequence_GetItem dll_PySequence_GetItem
# define PyTuple_Size dll_PyTuple_Size
# define PyTuple_GetItem dll_PyTuple_GetItem
# define PyTuple_Type (*dll_PyTuple_Type)
# define PyImport_ImportModule dll_PyImport_ImportModule
# define PyDict_New dll_PyDict_New
# define PyDict_GetItemString dll_PyDict_GetItemString
# define PyDict_Next dll_PyDict_Next
# ifdef PyMapping_Items
#  define PY_NO_MAPPING_ITEMS
# else
#  define PyMapping_Items dll_PyMapping_Items
# endif
# define PyObject_CallMethod dll_PyObject_CallMethod
# define PyMapping_Check dll_PyMapping_Check
# define PyIter_Next dll_PyIter_Next
# define PyModule_GetDict dll_PyModule_GetDict
# define PyRun_SimpleString dll_PyRun_SimpleString
# define PyRun_String dll_PyRun_String
# define PyString_AsString dll_PyString_AsString
# define PyString_AsStringAndSize dll_PyString_AsStringAndSize
# define PyString_FromString dll_PyString_FromString
# define PyString_FromStringAndSize dll_PyString_FromStringAndSize
# define PyString_Size dll_PyString_Size
# define PyString_Type (*dll_PyString_Type)
# define PyUnicode_Type (*dll_PyUnicode_Type)
# undef PyUnicode_AsEncodedString
# define PyUnicode_AsEncodedString py_PyUnicode_AsEncodedString
# define PyFloat_AsDouble dll_PyFloat_AsDouble
# define PyFloat_FromDouble dll_PyFloat_FromDouble
# define PyFloat_Type (*dll_PyFloat_Type)
# define PyImport_AddModule (*dll_PyImport_AddModule)
# define PySys_SetObject dll_PySys_SetObject
# define PySys_SetArgv dll_PySys_SetArgv
# define PyType_Type (*dll_PyType_Type)
# define PyType_Ready (*dll_PyType_Ready)
# define Py_BuildValue dll_Py_BuildValue
# define Py_FindMethod dll_Py_FindMethod
# define Py_InitModule4 dll_Py_InitModule4
# define Py_SetPythonHome dll_Py_SetPythonHome
# define Py_Initialize dll_Py_Initialize
# define Py_Finalize dll_Py_Finalize
# define Py_IsInitialized dll_Py_IsInitialized
# define _PyObject_New dll__PyObject_New
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02070000
#  define _PyObject_NextNotImplemented (*dll__PyObject_NextNotImplemented)
# endif
# define _Py_NoneStruct (*dll__Py_NoneStruct)
# define _Py_ZeroStruct (*dll__Py_ZeroStruct)
# define _Py_TrueStruct (*dll__Py_TrueStruct)
# define PyObject_Init dll__PyObject_Init
# define PyObject_GetIter dll_PyObject_GetIter
# define PyObject_IsTrue dll_PyObject_IsTrue
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02020000
#  define PyType_IsSubtype dll_PyType_IsSubtype
# endif
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02030000
#  define PyObject_Malloc dll_PyObject_Malloc
#  define PyObject_Free dll_PyObject_Free
# endif
# ifdef PY_USE_CAPSULE
#  define PyCapsule_New dll_PyCapsule_New
#  define PyCapsule_GetPointer dll_PyCapsule_GetPointer
# else
#  define PyCObject_FromVoidPtr dll_PyCObject_FromVoidPtr
#  define PyCObject_AsVoidPtr dll_PyCObject_AsVoidPtr
# endif

/*
 * Pointers for dynamic link
 */
static int(*dll_PyArg_Parse)(PyObject *, char *, ...);
static int(*dll_PyArg_ParseTuple)(PyObject *, char *, ...);
static int(*dll_PyMem_Free)(void *);
static void* (*dll_PyMem_Malloc)(size_t);
static int(*dll_PyDict_SetItemString)(PyObject *dp, char *key, PyObject *item);
static int(*dll_PyErr_BadArgument)(void);
static void(*dll_PyErr_Clear)(void);
static void(*dll_PyErr_PrintEx)(int);
static PyObject*(*dll_PyErr_NoMemory)(void);
static PyObject*(*dll_PyErr_Occurred)(void);
static void(*dll_PyErr_SetNone)(PyObject *);
static void(*dll_PyErr_SetString)(PyObject *, const char *);
static void(*dll_PyErr_SetObject)(PyObject *, PyObject *);
static void(*dll_PyEval_InitThreads)(void);
static void(*dll_PyEval_RestoreThread)(PyThreadState *);
static PyThreadState*(*dll_PyEval_SaveThread)(void);
# ifdef PY_CAN_RECURSE
static PyGILState_STATE	(*dll_PyGILState_Ensure)(void);
static void (*dll_PyGILState_Release)(PyGILState_STATE);
# endif
static long(*dll_PyInt_AsLong)(PyObject *);
static PyObject*(*dll_PyInt_FromLong)(long);
static long(*dll_PyLong_AsLong)(PyObject *);
static PyObject*(*dll_PyLong_FromLong)(long);
static PyTypeObject* dll_PyBool_Type;
static PyTypeObject* dll_PyInt_Type;
static PyTypeObject* dll_PyLong_Type;
static PyObject*(*dll_PyList_GetItem)(PyObject *, PyInt);
static PyObject*(*dll_PyList_Append)(PyObject *, PyObject *);
static PyObject*(*dll_PyList_New)(PyInt size);
static int(*dll_PyList_SetItem)(PyObject *, PyInt, PyObject *);
static PyInt(*dll_PyList_Size)(PyObject *);
static PyTypeObject* dll_PyList_Type;
static int (*dll_PySequence_Check)(PyObject *);
static PyInt(*dll_PySequence_Size)(PyObject *);
static PyObject*(*dll_PySequence_GetItem)(PyObject *, PyInt);
static PyInt(*dll_PyTuple_Size)(PyObject *);
static PyObject*(*dll_PyTuple_GetItem)(PyObject *, PyInt);
static PyTypeObject* dll_PyTuple_Type;
static PyObject*(*dll_PyImport_ImportModule)(const char *);
static PyObject*(*dll_PyDict_New)(void);
static PyObject*(*dll_PyDict_GetItemString)(PyObject *, const char *);
static int (*dll_PyDict_Next)(PyObject *, Py_ssize_t *, PyObject **, PyObject **);
# ifndef PY_NO_MAPPING_ITEMS
static PyObject* (*dll_PyMapping_Items)(PyObject *);
# endif
static PyObject* (*dll_PyObject_CallMethod)(PyObject *, char *, PyObject *);
static int (*dll_PyMapping_Check)(PyObject *);
static PyObject* (*dll_PyIter_Next)(PyObject *);
static PyObject*(*dll_PyModule_GetDict)(PyObject *);
static int(*dll_PyRun_SimpleString)(char *);
static PyObject *(*dll_PyRun_String)(char *, int, PyObject *, PyObject *);
static char*(*dll_PyString_AsString)(PyObject *);
static int(*dll_PyString_AsStringAndSize)(PyObject *, char **, int *);
static PyObject*(*dll_PyString_FromString)(const char *);
static PyObject*(*dll_PyString_FromStringAndSize)(const char *, PyInt);
static PyInt(*dll_PyString_Size)(PyObject *);
static PyTypeObject* dll_PyString_Type;
static PyTypeObject* dll_PyUnicode_Type;
static PyObject *(*py_PyUnicode_AsEncodedString)(PyObject *, char *, char *);
static double(*dll_PyFloat_AsDouble)(PyObject *);
static PyObject*(*dll_PyFloat_FromDouble)(double);
static PyTypeObject* dll_PyFloat_Type;
static int(*dll_PySys_SetObject)(char *, PyObject *);
static int(*dll_PySys_SetArgv)(int, char **);
static PyTypeObject* dll_PyType_Type;
static int (*dll_PyType_Ready)(PyTypeObject *type);
static PyObject*(*dll_Py_BuildValue)(char *, ...);
static PyObject*(*dll_Py_FindMethod)(struct PyMethodDef[], PyObject *, char *);
static PyObject*(*dll_Py_InitModule4)(char *, struct PyMethodDef *, char *, PyObject *, int);
static PyObject*(*dll_PyImport_AddModule)(char *);
static void(*dll_Py_SetPythonHome)(char *home);
static void(*dll_Py_Initialize)(void);
static void(*dll_Py_Finalize)(void);
static int(*dll_Py_IsInitialized)(void);
static PyObject*(*dll__PyObject_New)(PyTypeObject *, PyObject *);
static PyObject*(*dll__PyObject_Init)(PyObject *, PyTypeObject *);
static PyObject* (*dll_PyObject_GetIter)(PyObject *);
static int (*dll_PyObject_IsTrue)(PyObject *);
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02070000
static iternextfunc dll__PyObject_NextNotImplemented;
# endif
static PyObject* dll__Py_NoneStruct;
static PyObject* _Py_ZeroStruct;
static PyObject* dll__Py_TrueStruct;
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02020000
static int (*dll_PyType_IsSubtype)(PyTypeObject *, PyTypeObject *);
# endif
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02030000
static void* (*dll_PyObject_Malloc)(size_t);
static void (*dll_PyObject_Free)(void*);
# endif
# ifdef PY_USE_CAPSULE
static PyObject* (*dll_PyCapsule_New)(void *, char *, PyCapsule_Destructor);
static void* (*dll_PyCapsule_GetPointer)(PyObject *, char *);
# else
static PyObject* (*dll_PyCObject_FromVoidPtr)(void *cobj, void (*destr)(void *));
static void* (*dll_PyCObject_AsVoidPtr)(PyObject *);
# endif

static HINSTANCE hinstPython = 0; /* Instance of python.dll */

/* Imported exception objects */
static PyObject *imp_PyExc_AttributeError;
static PyObject *imp_PyExc_IndexError;
static PyObject *imp_PyExc_KeyError;
static PyObject *imp_PyExc_KeyboardInterrupt;
static PyObject *imp_PyExc_TypeError;
static PyObject *imp_PyExc_ValueError;

# define PyExc_AttributeError imp_PyExc_AttributeError
# define PyExc_IndexError imp_PyExc_IndexError
# define PyExc_KeyError imp_PyExc_KeyError
# define PyExc_KeyboardInterrupt imp_PyExc_KeyboardInterrupt
# define PyExc_TypeError imp_PyExc_TypeError
# define PyExc_ValueError imp_PyExc_ValueError

/*
 * Table of name to function pointer of python.
 */
# define PYTHON_PROC FARPROC
static struct
{
    char *name;
    PYTHON_PROC *ptr;
} python_funcname_table[] =
{
#ifndef PY_SSIZE_T_CLEAN
    {"PyArg_Parse", (PYTHON_PROC*)&dll_PyArg_Parse},
    {"PyArg_ParseTuple", (PYTHON_PROC*)&dll_PyArg_ParseTuple},
    {"Py_BuildValue", (PYTHON_PROC*)&dll_Py_BuildValue},
#else
    {"_PyArg_Parse_SizeT", (PYTHON_PROC*)&dll_PyArg_Parse},
    {"_PyArg_ParseTuple_SizeT", (PYTHON_PROC*)&dll_PyArg_ParseTuple},
    {"_Py_BuildValue_SizeT", (PYTHON_PROC*)&dll_Py_BuildValue},
#endif
    {"PyMem_Free", (PYTHON_PROC*)&dll_PyMem_Free},
    {"PyMem_Malloc", (PYTHON_PROC*)&dll_PyMem_Malloc},
    {"PyDict_SetItemString", (PYTHON_PROC*)&dll_PyDict_SetItemString},
    {"PyErr_BadArgument", (PYTHON_PROC*)&dll_PyErr_BadArgument},
    {"PyErr_Clear", (PYTHON_PROC*)&dll_PyErr_Clear},
    {"PyErr_PrintEx", (PYTHON_PROC*)&dll_PyErr_PrintEx},
    {"PyErr_NoMemory", (PYTHON_PROC*)&dll_PyErr_NoMemory},
    {"PyErr_Occurred", (PYTHON_PROC*)&dll_PyErr_Occurred},
    {"PyErr_SetNone", (PYTHON_PROC*)&dll_PyErr_SetNone},
    {"PyErr_SetString", (PYTHON_PROC*)&dll_PyErr_SetString},
    {"PyErr_SetObject", (PYTHON_PROC*)&dll_PyErr_SetObject},
    {"PyEval_InitThreads", (PYTHON_PROC*)&dll_PyEval_InitThreads},
    {"PyEval_RestoreThread", (PYTHON_PROC*)&dll_PyEval_RestoreThread},
    {"PyEval_SaveThread", (PYTHON_PROC*)&dll_PyEval_SaveThread},
# ifdef PY_CAN_RECURSE
    {"PyGILState_Ensure", (PYTHON_PROC*)&dll_PyGILState_Ensure},
    {"PyGILState_Release", (PYTHON_PROC*)&dll_PyGILState_Release},
# endif
    {"PyInt_AsLong", (PYTHON_PROC*)&dll_PyInt_AsLong},
    {"PyInt_FromLong", (PYTHON_PROC*)&dll_PyInt_FromLong},
    {"PyLong_AsLong", (PYTHON_PROC*)&dll_PyLong_AsLong},
    {"PyLong_FromLong", (PYTHON_PROC*)&dll_PyLong_FromLong},
    {"PyBool_Type", (PYTHON_PROC*)&dll_PyBool_Type},
    {"PyInt_Type", (PYTHON_PROC*)&dll_PyInt_Type},
    {"PyLong_Type", (PYTHON_PROC*)&dll_PyLong_Type},
    {"PyList_GetItem", (PYTHON_PROC*)&dll_PyList_GetItem},
    {"PyList_Append", (PYTHON_PROC*)&dll_PyList_Append},
    {"PyList_New", (PYTHON_PROC*)&dll_PyList_New},
    {"PyList_SetItem", (PYTHON_PROC*)&dll_PyList_SetItem},
    {"PyList_Size", (PYTHON_PROC*)&dll_PyList_Size},
    {"PyList_Type", (PYTHON_PROC*)&dll_PyList_Type},
    {"PySequence_GetItem", (PYTHON_PROC*)&dll_PySequence_GetItem},
    {"PySequence_Size", (PYTHON_PROC*)&dll_PySequence_Size},
    {"PySequence_Check", (PYTHON_PROC*)&dll_PySequence_Check},
    {"PyTuple_GetItem", (PYTHON_PROC*)&dll_PyTuple_GetItem},
    {"PyTuple_Size", (PYTHON_PROC*)&dll_PyTuple_Size},
    {"PyTuple_Type", (PYTHON_PROC*)&dll_PyTuple_Type},
    {"PyImport_ImportModule", (PYTHON_PROC*)&dll_PyImport_ImportModule},
    {"PyDict_GetItemString", (PYTHON_PROC*)&dll_PyDict_GetItemString},
    {"PyDict_Next", (PYTHON_PROC*)&dll_PyDict_Next},
    {"PyDict_New", (PYTHON_PROC*)&dll_PyDict_New},
# ifndef PY_NO_MAPPING_ITEMS
    {"PyMapping_Items", (PYTHON_PROC*)&dll_PyMapping_Items},
# endif
    {"PyObject_CallMethod", (PYTHON_PROC*)&dll_PyObject_CallMethod},
    {"PyMapping_Check", (PYTHON_PROC*)&dll_PyMapping_Check},
    {"PyIter_Next", (PYTHON_PROC*)&dll_PyIter_Next},
    {"PyModule_GetDict", (PYTHON_PROC*)&dll_PyModule_GetDict},
    {"PyRun_SimpleString", (PYTHON_PROC*)&dll_PyRun_SimpleString},
    {"PyRun_String", (PYTHON_PROC*)&dll_PyRun_String},
    {"PyString_AsString", (PYTHON_PROC*)&dll_PyString_AsString},
    {"PyString_AsStringAndSize", (PYTHON_PROC*)&dll_PyString_AsStringAndSize},
    {"PyString_FromString", (PYTHON_PROC*)&dll_PyString_FromString},
    {"PyString_FromStringAndSize", (PYTHON_PROC*)&dll_PyString_FromStringAndSize},
    {"PyString_Size", (PYTHON_PROC*)&dll_PyString_Size},
    {"PyString_Type", (PYTHON_PROC*)&dll_PyString_Type},
    {"PyUnicode_Type", (PYTHON_PROC*)&dll_PyUnicode_Type},
    {"PyFloat_Type", (PYTHON_PROC*)&dll_PyFloat_Type},
    {"PyFloat_AsDouble", (PYTHON_PROC*)&dll_PyFloat_AsDouble},
    {"PyFloat_FromDouble", (PYTHON_PROC*)&dll_PyFloat_FromDouble},
    {"PyImport_AddModule", (PYTHON_PROC*)&dll_PyImport_AddModule},
    {"PySys_SetObject", (PYTHON_PROC*)&dll_PySys_SetObject},
    {"PySys_SetArgv", (PYTHON_PROC*)&dll_PySys_SetArgv},
    {"PyType_Type", (PYTHON_PROC*)&dll_PyType_Type},
    {"PyType_Ready", (PYTHON_PROC*)&dll_PyType_Ready},
    {"Py_FindMethod", (PYTHON_PROC*)&dll_Py_FindMethod},
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02050000 \
	&& SIZEOF_SIZE_T != SIZEOF_INT
    {"Py_InitModule4_64", (PYTHON_PROC*)&dll_Py_InitModule4},
# else
    {"Py_InitModule4", (PYTHON_PROC*)&dll_Py_InitModule4},
# endif
    {"Py_SetPythonHome", (PYTHON_PROC*)&dll_Py_SetPythonHome},
    {"Py_Initialize", (PYTHON_PROC*)&dll_Py_Initialize},
    {"Py_Finalize", (PYTHON_PROC*)&dll_Py_Finalize},
    {"Py_IsInitialized", (PYTHON_PROC*)&dll_Py_IsInitialized},
    {"_PyObject_New", (PYTHON_PROC*)&dll__PyObject_New},
    {"PyObject_Init", (PYTHON_PROC*)&dll__PyObject_Init},
    {"PyObject_GetIter", (PYTHON_PROC*)&dll_PyObject_GetIter},
    {"PyObject_IsTrue", (PYTHON_PROC*)&dll_PyObject_IsTrue},
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02070000
    {"_PyObject_NextNotImplemented", (PYTHON_PROC*)&dll__PyObject_NextNotImplemented},
# endif
    {"_Py_NoneStruct", (PYTHON_PROC*)&dll__Py_NoneStruct},
    {"_Py_ZeroStruct", (PYTHON_PROC*)&dll__Py_ZeroStruct},
    {"_Py_TrueStruct", (PYTHON_PROC*)&dll__Py_TrueStruct},
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02020000
    {"PyType_IsSubtype", (PYTHON_PROC*)&dll_PyType_IsSubtype},
# endif
# if defined(PY_VERSION_HEX) && PY_VERSION_HEX >= 0x02030000
    {"PyObject_Malloc", (PYTHON_PROC*)&dll_PyObject_Malloc},
    {"PyObject_Free", (PYTHON_PROC*)&dll_PyObject_Free},
# endif
# ifdef PY_USE_CAPSULE
    {"PyCapsule_New", (PYTHON_PROC*)&dll_PyCapsule_New},
    {"PyCapsule_GetPointer", (PYTHON_PROC*)&dll_PyCapsule_GetPointer},
# else
    {"PyCObject_FromVoidPtr", (PYTHON_PROC*)&dll_PyCObject_FromVoidPtr},
    {"PyCObject_AsVoidPtr", (PYTHON_PROC*)&dll_PyCObject_AsVoidPtr},
# endif
    {"", NULL},
};

/*
 * Free python.dll
 */
    static void
end_dynamic_python(void)
{
    if (hinstPython)
    {
	close_dll(hinstPython);
	hinstPython = 0;
    }
}

/*
 * Load library and get all pointers.
 * Parameter 'libname' provides name of DLL.
 * Return OK or FAIL.
 */
    static int
python_runtime_link_init(char *libname, int verbose)
{
    int i;
    void *ucs_as_encoded_string;

#if !(defined(PY_NO_RTLD_GLOBAL) && defined(PY3_NO_RTLD_GLOBAL)) && defined(UNIX) && defined(FEAT_PYTHON3)
    /* Can't have Python and Python3 loaded at the same time.
     * It cause a crash, because RTLD_GLOBAL is needed for
     * standard C extension libraries of one or both python versions. */
    if (python3_loaded())
    {
	if (verbose)
	    EMSG(_("E836: This Vim cannot execute :python after using :py3"));
	return FAIL;
    }
#endif

    if (hinstPython)
	return OK;
    hinstPython = load_dll(libname);
    if (!hinstPython)
    {
	if (verbose)
	    EMSG2(_(e_loadlib), libname);
	return FAIL;
    }

    for (i = 0; python_funcname_table[i].ptr; ++i)
    {
	if ((*python_funcname_table[i].ptr = symbol_from_dll(hinstPython,
			python_funcname_table[i].name)) == NULL)
	{
	    close_dll(hinstPython);
	    hinstPython = 0;
	    if (verbose)
		EMSG2(_(e_loadfunc), python_funcname_table[i].name);
	    return FAIL;
	}
    }

    /* Load unicode functions separately as only the ucs2 or the ucs4 functions
     * will be present in the library. */
    ucs_as_encoded_string = symbol_from_dll(hinstPython,
					     "PyUnicodeUCS2_AsEncodedString");
    if (ucs_as_encoded_string == NULL)
	ucs_as_encoded_string = symbol_from_dll(hinstPython,
					     "PyUnicodeUCS4_AsEncodedString");
    if (ucs_as_encoded_string != NULL)
	py_PyUnicode_AsEncodedString = ucs_as_encoded_string;
    else
    {
	close_dll(hinstPython);
	hinstPython = 0;
	if (verbose)
	    EMSG2(_(e_loadfunc), "PyUnicode_UCSX_*");
	return FAIL;
    }

    return OK;
}

/*
 * If python is enabled (there is installed python on Windows system) return
 * TRUE, else FALSE.
 */
    int
python_enabled(int verbose)
{
    return python_runtime_link_init(DYNAMIC_PYTHON_DLL, verbose) == OK;
}

/*
 * Load the standard Python exceptions - don't import the symbols from the
 * DLL, as this can cause errors (importing data symbols is not reliable).
 */
    static void
get_exceptions(void)
{
    PyObject *exmod = PyImport_ImportModule("exceptions");
    PyObject *exdict = PyModule_GetDict(exmod);
    imp_PyExc_AttributeError = PyDict_GetItemString(exdict, "AttributeError");
    imp_PyExc_IndexError = PyDict_GetItemString(exdict, "IndexError");
    imp_PyExc_KeyError = PyDict_GetItemString(exdict, "KeyError");
    imp_PyExc_KeyboardInterrupt = PyDict_GetItemString(exdict, "KeyboardInterrupt");
    imp_PyExc_TypeError = PyDict_GetItemString(exdict, "TypeError");
    imp_PyExc_ValueError = PyDict_GetItemString(exdict, "ValueError");
    Py_XINCREF(imp_PyExc_AttributeError);
    Py_XINCREF(imp_PyExc_IndexError);
    Py_XINCREF(imp_PyExc_KeyError);
    Py_XINCREF(imp_PyExc_KeyboardInterrupt);
    Py_XINCREF(imp_PyExc_TypeError);
    Py_XINCREF(imp_PyExc_ValueError);
    Py_XDECREF(exmod);
}
#endif /* DYNAMIC_PYTHON */

static PyObject *BufferNew (buf_T *);
static PyObject *WindowNew(win_T *);
static PyObject *DictionaryNew(dict_T *);
static PyObject *LineToString(const char *);

static int initialised = 0;
#define PYINITIALISED initialised

#define DICTKEY_GET(err) \
    if (!PyString_Check(keyObject)) \
    { \
	PyErr_SetString(PyExc_TypeError, _("only string keys are allowed")); \
	return err; \
    } \
    if (PyString_AsStringAndSize(keyObject, (char **) &key, NULL) == -1) \
	return err;

#define DICTKEY_UNREF
#define DICTKEY_DECL

#define DESTRUCTOR_FINISH(self) Py_DECREF(self);

#define WIN_PYTHON_REF(win) win->w_python_ref
#define BUF_PYTHON_REF(buf) buf->b_python_ref
#define TAB_PYTHON_REF(tab) tab->tp_python_ref

static PyObject *OutputGetattr(PyObject *, char *);
static PyObject *BufferGetattr(PyObject *, char *);
static PyObject *WindowGetattr(PyObject *, char *);
static PyObject *TabPageGetattr(PyObject *, char *);
static PyObject *RangeGetattr(PyObject *, char *);
static PyObject *DictionaryGetattr(PyObject *, char*);
static PyObject *ListGetattr(PyObject *, char *);
static PyObject *FunctionGetattr(PyObject *, char *);

/*
 * Include the code shared with if_python3.c
 */
#include "if_py_both.h"


/******************************************************
 * Internal function prototypes.
 */

static PyObject *globals;

static void PythonIO_Flush(void);
static int PythonIO_Init(void);
static int PythonMod_Init(void);

/* Utility functions for the vim/python interface
 * ----------------------------------------------
 */

static int SetBufferLineList(buf_T *, PyInt, PyInt, PyObject *, PyInt *);


/******************************************************
 * 1. Python interpreter main program.
 */

#if PYTHON_API_VERSION < 1007 /* Python 1.4 */
typedef PyObject PyThreadState;
#endif

#ifdef PY_CAN_RECURSE
static PyGILState_STATE pygilstate = PyGILState_UNLOCKED;
#else
static PyThreadState *saved_python_thread = NULL;
#endif

/*
 * Suspend a thread of the Python interpreter, other threads are allowed to
 * run.
 */
    static void
Python_SaveThread(void)
{
#ifdef PY_CAN_RECURSE
    PyGILState_Release(pygilstate);
#else
    saved_python_thread = PyEval_SaveThread();
#endif
}

/*
 * Restore a thread of the Python interpreter, waits for other threads to
 * block.
 */
    static void
Python_RestoreThread(void)
{
#ifdef PY_CAN_RECURSE
    pygilstate = PyGILState_Ensure();
#else
    PyEval_RestoreThread(saved_python_thread);
    saved_python_thread = NULL;
#endif
}

    void
python_end()
{
    static int recurse = 0;

    /* If a crash occurs while doing this, don't try again. */
    if (recurse != 0)
	return;

    ++recurse;

#ifdef DYNAMIC_PYTHON
    if (hinstPython && Py_IsInitialized())
    {
	Python_RestoreThread();	    /* enter python */
	Py_Finalize();
    }
    end_dynamic_python();
#else
    if (Py_IsInitialized())
    {
	Python_RestoreThread();	    /* enter python */
	Py_Finalize();
    }
#endif

    --recurse;
}

#if (defined(DYNAMIC_PYTHON) && defined(FEAT_PYTHON3)) || defined(PROTO)
    int
python_loaded()
{
    return (hinstPython != 0);
}
#endif

    static int
Python_Init(void)
{
    if (!initialised)
    {
#ifdef DYNAMIC_PYTHON
	if (!python_enabled(TRUE))
	{
	    EMSG(_("E263: Sorry, this command is disabled, the Python library could not be loaded."));
	    goto fail;
	}
#endif

#ifdef PYTHON_HOME
	Py_SetPythonHome(PYTHON_HOME);
#endif

	init_structs();

#if !defined(MACOS) || defined(MACOS_X_UNIX)
	Py_Initialize();
#else
	PyMac_Initialize();
#endif
	/* Initialise threads, and below save the state using
	 * PyEval_SaveThread.  Without the call to PyEval_SaveThread, thread
	 * specific state (such as the system trace hook), will be lost
	 * between invocations of Python code. */
	PyEval_InitThreads();
#ifdef DYNAMIC_PYTHON
	get_exceptions();
#endif

	if (PythonIO_Init())
	    goto fail;

	if (PythonMod_Init())
	    goto fail;

	globals = PyModule_GetDict(PyImport_AddModule("__main__"));

	/* Remove the element from sys.path that was added because of our
	 * argv[0] value in PythonMod_Init().  Previously we used an empty
	 * string, but depending on the OS we then get an empty entry or
	 * the current directory in sys.path. */
	PyRun_SimpleString("import sys; sys.path = filter(lambda x: x != '/must>not&exist', sys.path)");

	/* lock is created and acquired in PyEval_InitThreads() and thread
	 * state is created in Py_Initialize()
	 * there _PyGILState_NoteThreadState() also sets gilcounter to 1
	 * (python must have threads enabled!)
	 * so the following does both: unlock GIL and save thread state in TLS
	 * without deleting thread state
	 */
#ifndef PY_CAN_RECURSE
	saved_python_thread =
#endif
	    PyEval_SaveThread();

	initialised = 1;
    }

    return 0;

fail:
    /* We call PythonIO_Flush() here to print any Python errors.
     * This is OK, as it is possible to call this function even
     * if PythonIO_Init() has not completed successfully (it will
     * not do anything in this case).
     */
    PythonIO_Flush();
    return -1;
}

/*
 * External interface
 */
    static void
DoPythonCommand(exarg_T *eap, const char *cmd, typval_T *rettv)
{
#ifndef PY_CAN_RECURSE
    static int		recursive = 0;
#endif
#if defined(MACOS) && !defined(MACOS_X_UNIX)
    GrafPtr		oldPort;
#endif
#if defined(HAVE_LOCALE_H) || defined(X_LOCALE)
    char		*saved_locale;
#endif

#ifndef PY_CAN_RECURSE
    if (recursive)
    {
	EMSG(_("E659: Cannot invoke Python recursively"));
	return;
    }
    ++recursive;
#endif

#if defined(MACOS) && !defined(MACOS_X_UNIX)
    GetPort(&oldPort);
    /* Check if the Python library is available */
    if ((Ptr)PyMac_Initialize == (Ptr)kUnresolvedCFragSymbolAddress)
	goto theend;
#endif
    if (Python_Init())
	goto theend;

    if (rettv == NULL)
    {
	RangeStart = eap->line1;
	RangeEnd = eap->line2;
    }
    else
    {
	RangeStart = (PyInt) curwin->w_cursor.lnum;
	RangeEnd = RangeStart;
    }
    Python_Release_Vim();	    /* leave vim */

#if defined(HAVE_LOCALE_H) || defined(X_LOCALE)
    /* Python only works properly when the LC_NUMERIC locale is "C". */
    saved_locale = setlocale(LC_NUMERIC, NULL);
    if (saved_locale == NULL || STRCMP(saved_locale, "C") == 0)
	saved_locale = NULL;
    else
    {
	/* Need to make a copy, value may change when setting new locale. */
	saved_locale = (char *)vim_strsave((char_u *)saved_locale);
	(void)setlocale(LC_NUMERIC, "C");
    }
#endif

    Python_RestoreThread();	    /* enter python */

    if (rettv == NULL)
	PyRun_SimpleString((char *)(cmd));
    else
    {
	PyObject	*r;

	r = PyRun_String((char *)(cmd), Py_eval_input, globals, globals);
	if (r == NULL)
	{
	    if (PyErr_Occurred() && !msg_silent)
		PyErr_PrintEx(0);
	    EMSG(_("E858: Eval did not return a valid python object"));
	}
	else
	{
	    if (ConvertFromPyObject(r, rettv) == -1)
		EMSG(_("E859: Failed to convert returned python object to vim value"));
	    Py_DECREF(r);
	}
	PyErr_Clear();
    }

    Python_SaveThread();	    /* leave python */

#if defined(HAVE_LOCALE_H) || defined(X_LOCALE)
    if (saved_locale != NULL)
    {
	(void)setlocale(LC_NUMERIC, saved_locale);
	vim_free(saved_locale);
    }
#endif

    Python_Lock_Vim();		    /* enter vim */
    PythonIO_Flush();
#if defined(MACOS) && !defined(MACOS_X_UNIX)
    SetPort(oldPort);
#endif

theend:
#ifndef PY_CAN_RECURSE
    --recursive;
#endif
    return;
}

/*
 * ":python"
 */
    void
ex_python(exarg_T *eap)
{
    char_u *script;

    script = script_get(eap, eap->arg);
    if (!eap->skip)
    {
	if (script == NULL)
	    DoPythonCommand(eap, (char *)eap->arg, NULL);
	else
	    DoPythonCommand(eap, (char *)script, NULL);
    }
    vim_free(script);
}

#define BUFFER_SIZE 1024

/*
 * ":pyfile"
 */
    void
ex_pyfile(exarg_T *eap)
{
    static char buffer[BUFFER_SIZE];
    const char *file = (char *)eap->arg;
    char *p;

    /* Have to do it like this. PyRun_SimpleFile requires you to pass a
     * stdio file pointer, but Vim and the Python DLL are compiled with
     * different options under Windows, meaning that stdio pointers aren't
     * compatible between the two. Yuk.
     *
     * Put the string "execfile('file')" into buffer. But, we need to
     * escape any backslashes or single quotes in the file name, so that
     * Python won't mangle the file name.
     */
    strcpy(buffer, "execfile('");
    p = buffer + 10; /* size of "execfile('" */

    while (*file && p < buffer + (BUFFER_SIZE - 3))
    {
	if (*file == '\\' || *file == '\'')
	    *p++ = '\\';
	*p++ = *file++;
    }

    /* If we didn't finish the file name, we hit a buffer overflow */
    if (*file != '\0')
	return;

    /* Put in the terminating "')" and a null */
    *p++ = '\'';
    *p++ = ')';
    *p++ = '\0';

    /* Execute the file */
    DoPythonCommand(eap, buffer, NULL);
}

/******************************************************
 * 2. Python output stream: writes output via [e]msg().
 */

/* Implementation functions
 */

    static PyObject *
OutputGetattr(PyObject *self, char *name)
{
    if (strcmp(name, "softspace") == 0)
	return PyInt_FromLong(((OutputObject *)(self))->softspace);

    return Py_FindMethod(OutputMethods, self, name);
}

/***************/

    static int
PythonIO_Init(void)
{
    /* Fixups... */
    PyType_Ready(&OutputType);

    return PythonIO_Init_io();
}

/******************************************************
 * 3. Implementation of the Vim module for Python
 */

static PyObject *ConvertToPyObject(typval_T *);
static int ConvertFromPyObject(PyObject *, typval_T *);

/* Window type - Implementation functions
 * --------------------------------------
 */

#define WindowType_Check(obj) ((obj)->ob_type == &WindowType)

/* Buffer type - Implementation functions
 * --------------------------------------
 */

#define BufferType_Check(obj) ((obj)->ob_type == &BufferType)

static PyInt BufferAssItem(PyObject *, PyInt, PyObject *);
static PyInt BufferAssSlice(PyObject *, PyInt, PyInt, PyObject *);

/* Line range type - Implementation functions
 * --------------------------------------
 */

#define RangeType_Check(obj) ((obj)->ob_type == &RangeType)

static PyInt RangeAssItem(PyObject *, PyInt, PyObject *);
static PyInt RangeAssSlice(PyObject *, PyInt, PyInt, PyObject *);

/* Current objects type - Implementation functions
 * -----------------------------------------------
 */

static PySequenceMethods BufferAsSeq = {
    (PyInquiry)		BufferLength,	    /* sq_length,    len(x)   */
    (binaryfunc)	0,		    /* BufferConcat, sq_concat, x+y */
    (PyIntArgFunc)	0,		    /* BufferRepeat, sq_repeat, x*n */
    (PyIntArgFunc)	BufferItem,	    /* sq_item,      x[i]     */
    (PyIntIntArgFunc)	BufferSlice,	    /* sq_slice,     x[i:j]   */
    (PyIntObjArgProc)	BufferAssItem,	    /* sq_ass_item,  x[i]=v   */
    (PyIntIntObjArgProc) BufferAssSlice,    /* sq_ass_slice, x[i:j]=v */
    (objobjproc)	0,
#if PY_MAJOR_VERSION >= 2
    (binaryfunc)	0,
    0,
#endif
};

/* Buffer object - Implementation
 */

    static PyObject *
BufferGetattr(PyObject *self, char *name)
{
    PyObject *r;

    if (CheckBuffer((BufferObject *)(self)))
	return NULL;

    r = BufferAttr((BufferObject *)(self), name);
    if (r || PyErr_Occurred())
	return r;
    else
	return Py_FindMethod(BufferMethods, self, name);
}

/******************/

    static PyInt
BufferAssItem(PyObject *self, PyInt n, PyObject *val)
{
    return RBAsItem((BufferObject *)(self), n, val, 1, -1, NULL);
}

    static PyInt
BufferAssSlice(PyObject *self, PyInt lo, PyInt hi, PyObject *val)
{
    return RBAsSlice((BufferObject *)(self), lo, hi, val, 1, -1, NULL);
}

static PySequenceMethods RangeAsSeq = {
    (PyInquiry)		RangeLength,	      /* sq_length,    len(x)   */
    (binaryfunc)	0, /* RangeConcat, */ /* sq_concat,    x+y      */
    (PyIntArgFunc)	0, /* RangeRepeat, */ /* sq_repeat,    x*n      */
    (PyIntArgFunc)	RangeItem,	      /* sq_item,      x[i]     */
    (PyIntIntArgFunc)	RangeSlice,	      /* sq_slice,     x[i:j]   */
    (PyIntObjArgProc)	RangeAssItem,	      /* sq_ass_item,  x[i]=v   */
    (PyIntIntObjArgProc) RangeAssSlice,	      /* sq_ass_slice, x[i:j]=v */
    (objobjproc)	0,
#if PY_MAJOR_VERSION >= 2
    (binaryfunc)	0,
    0,
#endif
};

/* Line range object - Implementation
 */

    static PyObject *
RangeGetattr(PyObject *self, char *name)
{
    if (strcmp(name, "start") == 0)
	return Py_BuildValue(Py_ssize_t_fmt, ((RangeObject *)(self))->start - 1);
    else if (strcmp(name, "end") == 0)
	return Py_BuildValue(Py_ssize_t_fmt, ((RangeObject *)(self))->end - 1);
    else
	return Py_FindMethod(RangeMethods, self, name);
}

/****************/

    static PyInt
RangeAssItem(PyObject *self, PyInt n, PyObject *val)
{
    return RBAsItem(((RangeObject *)(self))->buf, n, val,
		     ((RangeObject *)(self))->start,
		     ((RangeObject *)(self))->end,
		     &((RangeObject *)(self))->end);
}

    static PyInt
RangeAssSlice(PyObject *self, PyInt lo, PyInt hi, PyObject *val)
{
    return RBAsSlice(((RangeObject *)(self))->buf, lo, hi, val,
		      ((RangeObject *)(self))->start,
		      ((RangeObject *)(self))->end,
		      &((RangeObject *)(self))->end);
}

/* TabPage object - Implementation
 */

    static PyObject *
TabPageGetattr(PyObject *self, char *name)
{
    PyObject *r;

    if (CheckTabPage((TabPageObject *)(self)))
	return NULL;

    r = TabPageAttr((TabPageObject *)(self), name);
    if (r || PyErr_Occurred())
	return r;
    else
	return Py_FindMethod(TabPageMethods, self, name);
}

/* Window object - Implementation
 */

    static PyObject *
WindowGetattr(PyObject *self, char *name)
{
    PyObject *r;

    if (CheckWindow((WindowObject *)(self)))
	return NULL;

    r = WindowAttr((WindowObject *)(self), name);
    if (r || PyErr_Occurred())
	return r;
    else
	return Py_FindMethod(WindowMethods, self, name);
}

/* Tab page list object - Definitions
 */

static PySequenceMethods TabListAsSeq = {
    (PyInquiry)		TabListLength,	    /* sq_length,    len(x)   */
    (binaryfunc)	0,		    /* sq_concat,    x+y      */
    (PyIntArgFunc)	0,		    /* sq_repeat,    x*n      */
    (PyIntArgFunc)	TabListItem,	    /* sq_item,      x[i]     */
    (PyIntIntArgFunc)	0,		    /* sq_slice,     x[i:j]   */
    (PyIntObjArgProc)	0,		    /* sq_ass_item,  x[i]=v   */
    (PyIntIntObjArgProc) 0,		    /* sq_ass_slice, x[i:j]=v */
    (objobjproc)	0,
#if PY_MAJOR_VERSION >= 2
    (binaryfunc)	0,
    0,
#endif
};

/* Window list object - Definitions
 */

static PySequenceMethods WinListAsSeq = {
    (PyInquiry)		WinListLength,	    /* sq_length,    len(x)   */
    (binaryfunc)	0,		    /* sq_concat,    x+y      */
    (PyIntArgFunc)	0,		    /* sq_repeat,    x*n      */
    (PyIntArgFunc)	WinListItem,	    /* sq_item,      x[i]     */
    (PyIntIntArgFunc)	0,		    /* sq_slice,     x[i:j]   */
    (PyIntObjArgProc)	0,		    /* sq_ass_item,  x[i]=v   */
    (PyIntIntObjArgProc) 0,		    /* sq_ass_slice, x[i:j]=v */
    (objobjproc)	0,
#if PY_MAJOR_VERSION >= 2
    (binaryfunc)	0,
    0,
#endif
};

/* External interface
 */

    void
python_buffer_free(buf_T *buf)
{
    if (BUF_PYTHON_REF(buf) != NULL)
    {
	BufferObject *bp = BUF_PYTHON_REF(buf);
	bp->buf = INVALID_BUFFER_VALUE;
	BUF_PYTHON_REF(buf) = NULL;
    }
}

#if defined(FEAT_WINDOWS) || defined(PROTO)
    void
python_window_free(win_T *win)
{
    if (WIN_PYTHON_REF(win) != NULL)
    {
	WindowObject *wp = WIN_PYTHON_REF(win);
	wp->win = INVALID_WINDOW_VALUE;
	WIN_PYTHON_REF(win) = NULL;
    }
}

    void
python_tabpage_free(tabpage_T *tab)
{
    if (TAB_PYTHON_REF(tab) != NULL)
    {
	TabPageObject *tp = TAB_PYTHON_REF(tab);
	tp->tab = INVALID_TABPAGE_VALUE;
	TAB_PYTHON_REF(tab) = NULL;
    }
}
#endif

static BufMapObject TheBufferMap =
{
    PyObject_HEAD_INIT(&BufMapType)
};

static WinListObject TheWindowList =
{
    PyObject_HEAD_INIT(&WinListType)
    NULL
};

static CurrentObject TheCurrent =
{
    PyObject_HEAD_INIT(&CurrentType)
};

static TabListObject TheTabPageList =
{
    PyObject_HEAD_INIT(&TabListType)
};

    static int
PythonMod_Init(void)
{
    PyObject *mod;
    PyObject *dict;
    PyObject *tmp;
    /* The special value is removed from sys.path in Python_Init(). */
    static char *(argv[2]) = {"/must>not&exist/foo", NULL};

    /* Fixups... */
    PyType_Ready(&IterType);
    PyType_Ready(&BufferType);
    PyType_Ready(&RangeType);
    PyType_Ready(&WindowType);
    PyType_Ready(&TabPageType);
    PyType_Ready(&BufMapType);
    PyType_Ready(&WinListType);
    PyType_Ready(&TabListType);
    PyType_Ready(&CurrentType);
    PyType_Ready(&OptionsType);

    /* Set sys.argv[] to avoid a crash in warn(). */
    PySys_SetArgv(1, argv);

    mod = Py_InitModule4("vim", VimMethods, (char *)NULL, (PyObject *)NULL, PYTHON_API_VERSION);
    dict = PyModule_GetDict(mod);

    VimError = Py_BuildValue("s", "vim.error");

    PyDict_SetItemString(dict, "error", VimError);
    PyDict_SetItemString(dict, "buffers", (PyObject *)(void *)&TheBufferMap);
    PyDict_SetItemString(dict, "current", (PyObject *)(void *)&TheCurrent);
    PyDict_SetItemString(dict, "windows", (PyObject *)(void *)&TheWindowList);
    PyDict_SetItemString(dict, "tabpages", (PyObject *)(void *)&TheTabPageList);
    tmp = DictionaryNew(&globvardict);
    PyDict_SetItemString(dict, "vars",    tmp);
    Py_DECREF(tmp);
    tmp = DictionaryNew(&vimvardict);
    PyDict_SetItemString(dict, "vvars",   tmp);
    Py_DECREF(tmp);
    tmp = OptionsNew(SREQ_GLOBAL, NULL, dummy_check, NULL);
    PyDict_SetItemString(dict, "options", tmp);
    Py_DECREF(tmp);
    PyDict_SetItemString(dict, "VAR_LOCKED",    PyInt_FromLong(VAR_LOCKED));
    PyDict_SetItemString(dict, "VAR_FIXED",     PyInt_FromLong(VAR_FIXED));
    PyDict_SetItemString(dict, "VAR_SCOPE",     PyInt_FromLong(VAR_SCOPE));
    PyDict_SetItemString(dict, "VAR_DEF_SCOPE", PyInt_FromLong(VAR_DEF_SCOPE));

    if (PyErr_Occurred())
	return -1;

    return 0;
}

/*************************************************************************
 * 4. Utility functions for handling the interface between Vim and Python.
 */

/* Convert a Vim line into a Python string.
 * All internal newlines are replaced by null characters.
 *
 * On errors, the Python exception data is set, and NULL is returned.
 */
    static PyObject *
LineToString(const char *str)
{
    PyObject *result;
    PyInt len = strlen(str);
    char *p;

    /* Allocate an Python string object, with uninitialised contents. We
     * must do it this way, so that we can modify the string in place
     * later. See the Python source, Objects/stringobject.c for details.
     */
    result = PyString_FromStringAndSize(NULL, len);
    if (result == NULL)
	return NULL;

    p = PyString_AsString(result);

    while (*str)
    {
	if (*str == '\n')
	    *p = '\0';
	else
	    *p = *str;

	++p;
	++str;
    }

    return result;
}

    static PyObject *
DictionaryGetattr(PyObject *self, char *name)
{
    DictionaryObject	*this = ((DictionaryObject *) (self));

    if (strcmp(name, "locked") == 0)
	return PyInt_FromLong(this->dict->dv_lock);
    else if (strcmp(name, "scope") == 0)
	return PyInt_FromLong(this->dict->dv_scope);

    return Py_FindMethod(DictionaryMethods, self, name);
}

static PySequenceMethods ListAsSeq = {
    (PyInquiry)			ListLength,
    (binaryfunc)		0,
    (PyIntArgFunc)		0,
    (PyIntArgFunc)		ListItem,
    (PyIntIntArgFunc)		ListSlice,
    (PyIntObjArgProc)		ListAssItem,
    (PyIntIntObjArgProc)	ListAssSlice,
    (objobjproc)		0,
#if PY_MAJOR_VERSION >= 2
    (binaryfunc)		ListConcatInPlace,
    0,
#endif
};

    static PyObject *
ListGetattr(PyObject *self, char *name)
{
    if (strcmp(name, "locked") == 0)
	return PyInt_FromLong(((ListObject *)(self))->list->lv_lock);

    return Py_FindMethod(ListMethods, self, name);
}

    static PyObject *
FunctionGetattr(PyObject *self, char *name)
{
    FunctionObject	*this = (FunctionObject *)(self);

    if (strcmp(name, "name") == 0)
	return PyString_FromString((char *)(this->name));
    else
	return Py_FindMethod(FunctionMethods, self, name);
}

    void
do_pyeval (char_u *str, typval_T *rettv)
{
    DoPythonCommand(NULL, (char *) str, rettv);
    switch(rettv->v_type)
    {
	case VAR_DICT: ++rettv->vval.v_dict->dv_refcount; break;
	case VAR_LIST: ++rettv->vval.v_list->lv_refcount; break;
	case VAR_FUNC: func_ref(rettv->vval.v_string);    break;
	case VAR_UNKNOWN:
	    rettv->v_type = VAR_NUMBER;
	    rettv->vval.v_number = 0;
	    break;
    }
}

/* Don't generate a prototype for the next function, it generates an error on
 * newer Python versions. */
#if PYTHON_API_VERSION < 1007 /* Python 1.4 */ && !defined(PROTO)

    char *
Py_GetProgramName(void)
{
    return "vim";
}
#endif /* Python 1.4 */

    void
set_ref_in_python (int copyID)
{
    set_ref_in_py(copyID);
}
