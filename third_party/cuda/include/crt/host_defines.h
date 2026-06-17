/*
 * Minimal host_defines.h for MSVC host-only CUDA header compilation.
 * Provides the macros that NVCC defines as built-in keywords when
 * CUDA headers are included from non-NVCC host code.
 */

#ifndef __CRT_HOST_DEFINES_H__
#define __CRT_HOST_DEFINES_H__

#if defined(_MSC_VER) && !defined(__CUDACC__)

#define __device_builtin__
#define __host__
#define __device__
#define __global__
#define __constant__
#define __shared__
#define __inline__
#define __align__(n) __declspec(align(n))
#define __builtin_align__(n) __declspec(align(n))
#define __cudart_builtin__
#define __CUDACC__
#define CUDARTAPI __stdcall
#define CUDARTAPI_DEPRECATED

#endif /* _MSC_VER && !__CUDACC__ */

#endif /* __CRT_HOST_DEFINES_H__ */
