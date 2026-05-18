/* This generated file is for internal use. Do not include it from headers. */

#ifdef NEVERC_CONFIG_H
#error config.h can only be included once
#else
#define NEVERC_CONFIG_H

/* Bug report URL. */
#define BUG_REPORT_URL "${BUG_REPORT_URL}"

/* Default linker to use. */

/* Default runtime library to use. */
#define NEVERC_DEFAULT_RTLIB "${NEVERC_DEFAULT_RTLIB}"

/* Default unwind library to use. */
#define NEVERC_DEFAULT_UNWINDLIB "${NEVERC_DEFAULT_UNWINDLIB}"

/* Default object copy tool to use. */
#define NEVERC_DEFAULT_OBJECT_COPY_TOOL "${NEVERC_DEFAULT_OBJECT_COPY_TOOL}"

/* Multilib basename for libdir. */
#define NEVERC_INSTALL_LIBDIR_BASENAME "${NEVERC_INSTALL_LIBDIR_BASENAME}"

/* Relative directory for resource files */
#define NEVERC_RESOURCE_DIR "${NEVERC_RESOURCE_DIR}"

/* Directories neverc will search for headers */
#define C_INCLUDE_DIRS "${C_INCLUDE_DIRS}"

/* Directories neverc will search for configuration files */
#cmakedefine NEVERC_CONFIG_FILE_SYSTEM_DIR "${NEVERC_CONFIG_FILE_SYSTEM_DIR}"
#cmakedefine NEVERC_CONFIG_FILE_USER_DIR "${NEVERC_CONFIG_FILE_USER_DIR}"

/* Default <path> to all compiler invocations for --sysroot=<path>. */
#define DEFAULT_SYSROOT "${DEFAULT_SYSROOT}"

/* Directory where gcc is installed. */
#define GCC_INSTALL_PREFIX "${GCC_INSTALL_PREFIX}"

/* Define if we have sys/resource.h (rlimits) */
#cmakedefine NEVERC_HAVE_RLIMITS ${NEVERC_HAVE_RLIMITS}

/* Linker version detected at compile time. */
#cmakedefine HOST_LINK_VERSION "${HOST_LINK_VERSION}"

/* enable x86 relax relocations by default */
#cmakedefine01 ENABLE_X86_RELAX_RELOCATIONS

#endif
