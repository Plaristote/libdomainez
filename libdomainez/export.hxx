#pragma once

#if defined(LIBDOMAINEZ_STATIC)         // Using static.
#  define LIBDOMAINEZ_SYMEXPORT
#elif defined(LIBDOMAINEZ_STATIC_BUILD) // Building static.
#  define LIBDOMAINEZ_SYMEXPORT
#elif defined(LIBDOMAINEZ_SHARED)       // Using shared.
#  ifdef _WIN32
#    define LIBDOMAINEZ_SYMEXPORT __declspec(dllimport)
#  else
#    define LIBDOMAINEZ_SYMEXPORT
#  endif
#elif defined(LIBDOMAINEZ_SHARED_BUILD) // Building shared.
#  ifdef _WIN32
#    define LIBDOMAINEZ_SYMEXPORT __declspec(dllexport)
#  else
#    define LIBDOMAINEZ_SYMEXPORT
#  endif
#else
#  define LIBDOMAINEZ_SYMEXPORT         // Using static or shared.
#endif
