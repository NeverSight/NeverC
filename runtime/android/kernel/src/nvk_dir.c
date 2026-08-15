/* SPDX-License-Identifier: GPL-2.0 */
/* Profile-aware, stack-scoped opaque dir_context filtering. */
#if defined(NEVERC_KRT_DIR_FILTER_HOST_TEST)
#include "../tools/test-dir-filter-shim.h"
#else
#include <nvk.h>
#include "nvk_internal.h"
#endif

#define _NEVERC_KRT_DIR_CONTEXT_MAX 64UL
#define _NEVERC_KRT_DIR_SCOPE_MAGIC 0x4E564B4449525343ULL

struct dir_context;

typedef int (*_neverc_krt_filldir_int_fn)(struct dir_context *ctx,
					  const char *name, int namelen,
					  loff_t offset, u64 ino,
					  unsigned int type);
typedef bool (*_neverc_krt_filldir_bool_fn)(struct dir_context *ctx,
					    const char *name, int namelen,
					    loff_t offset, u64 ino,
					    unsigned int type);

struct _neverc_krt_dir_filter_scope {
	unsigned char proxy_context[_NEVERC_KRT_DIR_CONTEXT_MAX];
	void *original_context;
	void *original_actor;
	neverc_krt_dir_should_hide_fn should_hide;
	void *opaque;
	unsigned long pos_offset;
	u64 magic;
	unsigned int filldir_abi;
	unsigned int active;
	int sync_error;
};

_Static_assert(sizeof(struct _neverc_krt_dir_filter_scope) <=
	       sizeof(struct neverc_krt_dir_filter_scope),
	       "public directory-filter scope is too small");
_Static_assert(__builtin_offsetof(struct _neverc_krt_dir_filter_scope,
				  proxy_context) == 0,
	       "proxy dir_context must begin at the scope address");

static int _neverc_krt_dir_field_fits(unsigned long context_size,
				      unsigned long offset,
				      unsigned long width)
{
	return width != 0 && offset <= context_size &&
	       width <= context_size - offset;
}

static int _neverc_krt_dir_contract(
	const struct neverc_krt_gki_layout **layout_out,
	const struct neverc_krt_runtime_caps **caps_out)
{
	int match = neverc_krt_check_kernel_match();
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_proven_gki_layout(
			NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT);
	const struct neverc_krt_runtime_caps *caps =
		_neverc_krt_current_caps();
	unsigned long actor_end;
	unsigned long pos_end;

	if ((match != NEVERC_KRT_VER_EXACT &&
	     match != NEVERC_KRT_VER_COMPAT) ||
	    !layout || !caps || layout->dir_context_size == 0 ||
	    layout->dir_context_size > _NEVERC_KRT_DIR_CONTEXT_MAX ||
	    layout->dir_context_actor_size != sizeof(void *) ||
	    layout->dir_context_pos_size != sizeof(loff_t) ||
	    !_neverc_krt_dir_field_fits(layout->dir_context_size,
					layout->dir_context_actor,
					layout->dir_context_actor_size) ||
	    !_neverc_krt_dir_field_fits(layout->dir_context_size,
					layout->dir_context_pos,
					layout->dir_context_pos_size))
		return -1;

	actor_end = layout->dir_context_actor + layout->dir_context_actor_size;
	pos_end = layout->dir_context_pos + layout->dir_context_pos_size;
	if (!(actor_end <= layout->dir_context_pos ||
	      pos_end <= layout->dir_context_actor))
		return -1;
	if (caps->filldir_abi != NEVERC_KRT_FILLDIR_ABI_RETURNS_INT &&
	    caps->filldir_abi != NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL)
		return -1;

	if (layout_out)
		*layout_out = layout;
	if (caps_out)
		*caps_out = caps;
	return 0;
}

static struct _neverc_krt_dir_filter_scope *
_neverc_krt_dir_scope_from_context(struct dir_context *ctx)
{
	return (struct _neverc_krt_dir_filter_scope *)(void *)ctx;
}

static int _neverc_krt_dir_pos_to_original(
	struct _neverc_krt_dir_filter_scope *scope)
{
	loff_t pos;

	__builtin_memcpy(&pos, scope->proxy_context + scope->pos_offset,
			 sizeof(pos));
	if (neverc_krt_mem_write(
			(char *)scope->original_context + scope->pos_offset,
			&pos, sizeof(pos))) {
		scope->sync_error = -3;
		return -1;
	}
	return 0;
}

static int _neverc_krt_dir_pos_from_original(
	struct _neverc_krt_dir_filter_scope *scope)
{
	loff_t pos;

	if (neverc_krt_mem_read(
			&pos,
			(char *)scope->original_context + scope->pos_offset,
			sizeof(pos))) {
		scope->sync_error = -3;
		return -1;
	}
	__builtin_memcpy(scope->proxy_context + scope->pos_offset,
			 &pos, sizeof(pos));
	return 0;
}

static int _neverc_krt_dir_filldir_int(struct dir_context *ctx,
				       const char *name, int namelen,
				       loff_t offset, u64 ino,
				       unsigned int type)
{
	struct _neverc_krt_dir_filter_scope *scope =
		_neverc_krt_dir_scope_from_context(ctx);
	_neverc_krt_filldir_int_fn original;
	int ret;

	if (!scope || scope->magic != _NEVERC_KRT_DIR_SCOPE_MAGIC ||
	    !scope->active ||
	    scope->filldir_abi != NEVERC_KRT_FILLDIR_ABI_RETURNS_INT ||
	    !scope->should_hide || scope->sync_error)
		return -1;
	if (scope->should_hide(name, namelen, offset, ino, type, scope->opaque))
		return 0;

	original = (_neverc_krt_filldir_int_fn)scope->original_actor;
	if (!original || !scope->original_context)
		return -1;
	if (_neverc_krt_dir_pos_to_original(scope))
		return -1;
	ret = original((struct dir_context *)scope->original_context,
		       name, namelen, offset, ino, type);
	if (_neverc_krt_dir_pos_from_original(scope))
		return -1;
	return ret;
}

static bool _neverc_krt_dir_filldir_bool(struct dir_context *ctx,
					 const char *name, int namelen,
					 loff_t offset, u64 ino,
					 unsigned int type)
{
	struct _neverc_krt_dir_filter_scope *scope =
		_neverc_krt_dir_scope_from_context(ctx);
	_neverc_krt_filldir_bool_fn original;
	bool ret;

	if (!scope || scope->magic != _NEVERC_KRT_DIR_SCOPE_MAGIC ||
	    !scope->active ||
	    scope->filldir_abi != NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL ||
	    !scope->should_hide || scope->sync_error)
		return false;
	if (scope->should_hide(name, namelen, offset, ino, type, scope->opaque))
		return true;

	original = (_neverc_krt_filldir_bool_fn)scope->original_actor;
	if (!original || !scope->original_context)
		return false;
	if (_neverc_krt_dir_pos_to_original(scope))
		return false;
	ret = original((struct dir_context *)scope->original_context,
		        name, namelen, offset, ino, type);
	if (_neverc_krt_dir_pos_from_original(scope))
		return false;
	return ret;
}

int neverc_krt_dir_filter_available(void)
{
	return _neverc_krt_dir_contract((void *)0, (void *)0) == 0;
}

int neverc_krt_dir_filter_begin(struct neverc_krt_dir_filter_scope *public_scope,
				void *original_ctx,
				neverc_krt_dir_should_hide_fn should_hide,
				void *opaque, void **proxy_ctx)
{
	const struct neverc_krt_gki_layout *layout;
	const struct neverc_krt_runtime_caps *caps;
	struct _neverc_krt_dir_filter_scope *scope =
		(struct _neverc_krt_dir_filter_scope *)(void *)public_scope;
	void *wrapper;

	if (proxy_ctx)
		*proxy_ctx = (void *)0;
	if (!scope || !original_ctx || !should_hide || !proxy_ctx)
		return -1;
	__builtin_memset(scope, 0, sizeof(*scope));
	if (_neverc_krt_dir_contract(&layout, &caps))
		return -2;
	if (neverc_krt_mem_read(scope->proxy_context, original_ctx,
				layout->dir_context_size))
		return -3;

	__builtin_memcpy(&scope->original_actor,
			 scope->proxy_context + layout->dir_context_actor,
			 sizeof(scope->original_actor));
	if (!scope->original_actor)
		return -4;
	if (caps->filldir_abi == NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL)
		wrapper = (void *)_neverc_krt_dir_filldir_bool;
	else
		wrapper = (void *)_neverc_krt_dir_filldir_int;
	if (scope->original_actor == wrapper)
		return -5;

	scope->original_context = original_ctx;
	scope->should_hide = should_hide;
	scope->opaque = opaque;
	scope->pos_offset = layout->dir_context_pos;
	scope->filldir_abi = (unsigned int)caps->filldir_abi;
	__builtin_memcpy(scope->proxy_context + layout->dir_context_actor,
			 &wrapper, sizeof(wrapper));
	scope->active = 1;
	scope->magic = _NEVERC_KRT_DIR_SCOPE_MAGIC;
	*proxy_ctx = scope->proxy_context;
	return 0;
}

int neverc_krt_dir_filter_end(struct neverc_krt_dir_filter_scope *public_scope)
{
	struct _neverc_krt_dir_filter_scope *scope =
		(struct _neverc_krt_dir_filter_scope *)(void *)public_scope;
	void *original_context;
	loff_t pos;
	int ret;

	if (!scope || scope->magic != _NEVERC_KRT_DIR_SCOPE_MAGIC ||
	    !scope->active || !scope->original_context)
		return -1;
	original_context = scope->original_context;
	ret = scope->sync_error;
	if (!ret) {
		__builtin_memcpy(&pos, scope->proxy_context + scope->pos_offset,
				 sizeof(pos));
		if (neverc_krt_mem_write(
				(char *)original_context + scope->pos_offset,
				&pos, sizeof(pos)))
			ret = -2;
	}
	__builtin_memset(scope, 0, sizeof(*scope));
	return ret;
}
