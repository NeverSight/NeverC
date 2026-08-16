/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal static PID 1 for the GKI module load/unload smoke test. */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_finit_module
#error "SYS_finit_module is required"
#endif
#ifndef SYS_delete_module
#error "SYS_delete_module is required"
#endif

#ifndef NEVERC_GKI_MODULE_PATH
#define NEVERC_GKI_MODULE_PATH "/neverc-smoke.ko"
#endif
#ifndef NEVERC_GKI_MODULE_NAME
#define NEVERC_GKI_MODULE_NAME "neverc_gki_smoke"
#endif

static void emit(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vdprintf(STDOUT_FILENO, format, arguments);
	va_end(arguments);
}

static void power_off(int status)
{
	sync();
	(void)reboot(RB_POWER_OFF);
	_exit(status);
}

int main(void)
{
	int console;
	int module;
	int saved_errno;

	console = open("/dev/console", O_WRONLY | O_CLOEXEC);
	if (console >= 0) {
		(void)dup2(console, STDOUT_FILENO);
		(void)dup2(console, STDERR_FILENO);
		if (console > STDERR_FILENO)
			(void)close(console);
	}

	module = open(NEVERC_GKI_MODULE_PATH, O_RDONLY | O_CLOEXEC);
	if (module < 0) {
		saved_errno = errno;
		emit("NEVERC_GKI_LOAD_FAIL syscall=open errno=%d %s\n",
		     saved_errno, strerror(saved_errno));
		power_off(1);
	}
	if (syscall(SYS_finit_module, module, "", 0) != 0) {
		saved_errno = errno;
		emit("NEVERC_GKI_LOAD_FAIL syscall=finit_module errno=%d %s\n",
		     saved_errno, strerror(saved_errno));
		(void)close(module);
		power_off(1);
	}
	(void)close(module);
	emit("NEVERC_GKI_LOAD_PASS\n");

	if (syscall(SYS_delete_module, NEVERC_GKI_MODULE_NAME, 0) != 0) {
		saved_errno = errno;
		emit("NEVERC_GKI_UNLOAD_FAIL syscall=delete_module errno=%d %s\n",
		     saved_errno, strerror(saved_errno));
		power_off(1);
	}
	emit("NEVERC_GKI_UNLOAD_PASS\n");
	power_off(0);
}
