#include "config.h"
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#define EXPORT __attribute__((visibility("default")))

EXPORT int nc_get_pid(void) { return getpid(); }

EXPORT task_t nc_self_task(void) { return mach_task_self(); }

EXPORT kern_return_t nc_task_basic_info(task_t task,
                                        struct task_basic_info *info) {
  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  return task_info(task, TASK_BASIC_INFO, (task_info_t)info, &count);
}

EXPORT kern_return_t nc_vm_read(task_t task, mach_vm_address_t addr,
                                mach_vm_size_t size, void *buf) {
  vm_offset_t data;
  mach_msg_type_number_t data_cnt;
  kern_return_t kr = mach_vm_read(task, addr, size, &data, &data_cnt);
  if (kr == KERN_SUCCESS) {
    memcpy(buf, (void *)data, data_cnt);
    mach_vm_deallocate(mach_task_self(), data, data_cnt);
  }
  return kr;
}

EXPORT kern_return_t nc_vm_write(task_t task, mach_vm_address_t addr,
                                 const void *buf,
                                 mach_msg_type_number_t size) {
  return mach_vm_write(task, addr, (vm_offset_t)buf, size);
}

EXPORT kern_return_t nc_vm_alloc(task_t task, mach_vm_address_t *addr,
                                 mach_vm_size_t size) {
  return mach_vm_allocate(task, addr, size, VM_FLAGS_ANYWHERE);
}

EXPORT kern_return_t nc_vm_dealloc(task_t task, mach_vm_address_t addr,
                                   mach_vm_size_t size) {
  return mach_vm_deallocate(task, addr, size);
}

EXPORT void nc_xor_buffer(unsigned char *buf, int len, unsigned char key) {
  for (int i = 0; i < len; ++i)
    buf[i] ^= key;
}

EXPORT const char *nc_version(void) { return "NeverC-dylib 1.0"; }
