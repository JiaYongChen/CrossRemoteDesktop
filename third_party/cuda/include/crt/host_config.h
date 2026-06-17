/*
 * Minimal host_config.h for MSVC host-only CUDA header compilation.
 */

#ifndef __CRT_HOST_CONFIG_H__
#define __CRT_HOST_CONFIG_H__

#if defined(_MSC_VER) && !defined(__CUDACC__)

#define __CUDACC_VER__ 12000
#define __CUDACC_VER_MAJOR__ 12
#define __CUDACC_VER_MINOR__ 0

#endif /* _MSC_VER && !__CUDACC__ */

#endif /* __CRT_HOST_CONFIG_H__ */
