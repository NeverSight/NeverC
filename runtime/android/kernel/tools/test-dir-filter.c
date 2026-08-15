// SPDX-License-Identifier: GPL-2.0
/* Host behavior fixture for the production nvk_dir.c implementation. */

#include "test-dir-filter-shim.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

struct fixture_dir_context;
typedef int (*fixture_filldir_int_fn)(struct fixture_dir_context *,
				      const char *, int, loff_t, u64,
				      unsigned int);
typedef bool (*fixture_filldir_bool_fn)(struct fixture_dir_context *,
					const char *, int, loff_t, u64,
					unsigned int);

struct fixture_dir_context {
	void *actor;
	loff_t pos;
};

struct fixture_reader {
	struct fixture_dir_context context;
	int calls;
	int wrong_context;
	loff_t observed_pos;
	loff_t actor_pos;
	int update_pos;
	bool bool_result;
};

_Static_assert(sizeof(struct fixture_dir_context) == 16,
	       "host fixture must match certified dir_context size");
_Static_assert(offsetof(struct fixture_dir_context, actor) == 0,
	       "host fixture actor offset");
_Static_assert(sizeof(((struct fixture_dir_context *)0)->actor) == 8,
	       "host fixture actor width");
_Static_assert(offsetof(struct fixture_dir_context, pos) == 8,
	       "host fixture pos offset");
_Static_assert(sizeof(((struct fixture_dir_context *)0)->pos) == 8,
	       "host fixture pos width");

static struct neverc_krt_gki_layout fixture_layout = {
	.dir_context_size = 16,
	.dir_context_actor = 0,
	.dir_context_actor_size = 8,
	.dir_context_pos = 8,
	.dir_context_pos_size = 8,
};
static struct neverc_krt_runtime_caps fixture_caps = {
	.filldir_abi = NEVERC_KRT_FILLDIR_ABI_RETURNS_INT,
};
static int fixture_version_match = NEVERC_KRT_VER_EXACT;
static unsigned long fixture_layout_certificates =
	NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT;
static int fixture_fail_next_read;
static int fixture_fail_next_write;

int neverc_krt_check_kernel_match(void)
{
	return fixture_version_match;
}

unsigned long _neverc_krt_current_layout_certificates(void)
{
	return fixture_layout_certificates;
}

const struct neverc_krt_runtime_caps *_neverc_krt_current_caps(void)
{
	return &fixture_caps;
}

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void)
{
	return &fixture_layout;
}

const struct neverc_krt_gki_layout *_neverc_krt_get_proven_gki_layout(
	unsigned long required)
{
	return (fixture_layout_certificates & required) == required ?
		&fixture_layout : NULL;
}

long neverc_krt_mem_read(void *dst, const void *src, size_t len)
{
	if (fixture_fail_next_read) {
		fixture_fail_next_read = 0;
		return -1;
	}
	memcpy(dst, src, len);
	return 0;
}

long neverc_krt_mem_write(void *dst, const void *src, size_t len)
{
	if (fixture_fail_next_write) {
		fixture_fail_next_write = 0;
		return -1;
	}
	memcpy(dst, src, len);
	return 0;
}

static bool hide_named_entry(const char *name, int namelen, loff_t offset,
			     u64 ino, unsigned int type, void *opaque)
{
	const char hidden[] = "hide";

	(void)offset;
	(void)ino;
	(void)type;
	(void)opaque;
	return namelen == (int)sizeof(hidden) - 1 &&
	       memcmp(name, hidden, sizeof(hidden) - 1) == 0;
}

static struct fixture_reader *reader_from_context(
	struct fixture_dir_context *context)
{
	return (struct fixture_reader *)((char *)context -
		offsetof(struct fixture_reader, context));
}

static int fixture_actor_int(struct fixture_dir_context *context,
			     const char *name, int namelen, loff_t offset,
			     u64 ino, unsigned int type)
{
	struct fixture_reader *reader = reader_from_context(context);

	(void)name;
	(void)namelen;
	(void)offset;
	(void)ino;
	(void)type;
	if (context != &reader->context)
		reader->wrong_context++;
	reader->observed_pos = context->pos;
	if (reader->update_pos)
		context->pos = reader->actor_pos;
	reader->calls++;
	return 0;
}

static bool fixture_actor_bool(struct fixture_dir_context *context,
			       const char *name, int namelen, loff_t offset,
			       u64 ino, unsigned int type)
{
	struct fixture_reader *reader = reader_from_context(context);

	(void)name;
	(void)namelen;
	(void)offset;
	(void)ino;
	(void)type;
	if (context != &reader->context)
		reader->wrong_context++;
	reader->observed_pos = context->pos;
	if (reader->update_pos)
		context->pos = reader->actor_pos;
	reader->calls++;
	return reader->bool_result;
}

static void set_proxy_pos(void *proxy, loff_t pos)
{
	memcpy((char *)proxy + fixture_layout.dir_context_pos, &pos, sizeof(pos));
}

static loff_t get_proxy_pos(void *proxy)
{
	loff_t pos;

	memcpy(&pos, (char *)proxy + fixture_layout.dir_context_pos, sizeof(pos));
	return pos;
}

static void check_int_actor_semantics(void)
{
	struct neverc_krt_dir_filter_scope scope;
	struct fixture_reader reader = {
		.actor_pos = 57,
		.update_pos = 1,
	};
	fixture_filldir_int_fn actor;
	void *proxy = NULL;

	fixture_caps.filldir_abi = NEVERC_KRT_FILLDIR_ABI_RETURNS_INT;
	reader.context.actor = (void *)fixture_actor_int;
	reader.context.pos = 7;
	assert(neverc_krt_dir_filter_available() == 1);
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == 0);
	assert(proxy != NULL && proxy != &reader.context);
	memcpy(&actor, proxy, sizeof(actor));

	/* Old int filldir: zero means the hidden entry was consumed. */
	assert(actor(proxy, "hide", 4, 8, 1, 4) == 0);
	assert(reader.calls == 0);
	set_proxy_pos(proxy, 41);
	assert(actor(proxy, "show", 4, 9, 2, 4) == 0);
	assert(reader.calls == 1);
	assert(reader.wrong_context == 0);
	assert(reader.observed_pos == 41);
	assert(get_proxy_pos(proxy) == 57);

	set_proxy_pos(proxy, 123);
	assert(neverc_krt_dir_filter_end(&scope) == 0);
	assert(reader.context.pos == 123);
}

static void check_bool_actor_semantics(void)
{
	struct neverc_krt_dir_filter_scope scope;
	struct fixture_reader reader = {
		.actor_pos = 71,
		.update_pos = 1,
		.bool_result = false,
	};
	fixture_filldir_bool_fn actor;
	void *proxy = NULL;

	fixture_caps.filldir_abi = NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL;
	reader.context.actor = (void *)fixture_actor_bool;
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == 0);
	memcpy(&actor, proxy, sizeof(actor));

	/* New bool filldir: true means consume the hidden entry and continue. */
	assert(actor(proxy, "hide", 4, 1, 1, 4) == true);
	assert(reader.calls == 0);
	set_proxy_pos(proxy, 63);
	assert(actor(proxy, "show", 4, 2, 2, 4) == false);
	assert(reader.calls == 1);
	assert(reader.wrong_context == 0);
	assert(reader.observed_pos == 63);
	assert(get_proxy_pos(proxy) == 71);
	assert(neverc_krt_dir_filter_end(&scope) == 0);
}

static void check_many_independent_scopes(void)
{
	enum { SCOPE_COUNT = 64 };
	struct neverc_krt_dir_filter_scope scopes[SCOPE_COUNT];
	struct fixture_reader readers[SCOPE_COUNT];
	void *proxies[SCOPE_COUNT];
	int i;

	fixture_caps.filldir_abi = NEVERC_KRT_FILLDIR_ABI_RETURNS_INT;
	memset(readers, 0, sizeof(readers));
	for (i = 0; i < SCOPE_COUNT; i++) {
		readers[i].context.actor = (void *)fixture_actor_int;
		assert(neverc_krt_dir_filter_begin(&scopes[i],
				&readers[i].context, hide_named_entry,
				NULL, &proxies[i]) == 0);
	}
	for (i = 0; i < SCOPE_COUNT; i++) {
		fixture_filldir_int_fn actor;

		memcpy(&actor, proxies[i], sizeof(actor));
		assert(actor(proxies[i], "show", 4, i, i, 4) == 0);
		assert(readers[i].calls == 1);
		assert(neverc_krt_dir_filter_end(&scopes[i]) == 0);
	}
}

static void check_invalid_contracts_fail_closed(void)
{
	struct neverc_krt_dir_filter_scope scope;
	struct fixture_reader reader = { 0 };
	struct neverc_krt_gki_layout saved_layout = fixture_layout;
	void *proxy = NULL;

	reader.context.actor = (void *)fixture_actor_int;
	fixture_version_match = NEVERC_KRT_VER_MISMATCH;
	assert(neverc_krt_dir_filter_available() == 0);
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) != 0);
	fixture_version_match = NEVERC_KRT_VER_EXACT;

	fixture_caps.filldir_abi = NEVERC_KRT_FILLDIR_ABI_UNSUPPORTED;
	assert(neverc_krt_dir_filter_available() == 0);
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) != 0);

	fixture_caps.filldir_abi = NEVERC_KRT_FILLDIR_ABI_RETURNS_INT;
	fixture_layout.dir_context_actor_size = 4;
	assert(neverc_krt_dir_filter_available() == 0);
	fixture_layout = saved_layout;
	fixture_layout.dir_context_pos = 12;
	assert(neverc_krt_dir_filter_available() == 0);
	fixture_layout = saved_layout;
	fixture_layout.dir_context_size = 0;
	assert(neverc_krt_dir_filter_available() == 0);
	fixture_layout = saved_layout;
}

static void check_match_contracts(void)
{
	struct neverc_krt_dir_filter_scope scope;
	struct fixture_reader reader = { 0 };
	void *proxy = NULL;

	fixture_caps.filldir_abi = NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL;
	reader.context.actor = (void *)fixture_actor_bool;

	fixture_version_match = NEVERC_KRT_VER_EXACT;
	fixture_layout_certificates = 0;
	assert(neverc_krt_dir_filter_available() == 0);

	fixture_version_match = NEVERC_KRT_VER_COMPAT;
	fixture_layout_certificates = 0;
	assert(neverc_krt_dir_filter_available() == 0);
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) != 0);

	fixture_layout_certificates = NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT;
	assert(neverc_krt_dir_filter_available() == 1);
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == 0);
	assert(neverc_krt_dir_filter_end(&scope) == 0);

	fixture_layout_certificates = NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT;
	fixture_version_match = NEVERC_KRT_VER_MISMATCH;
	assert(neverc_krt_dir_filter_available() == 0);
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) != 0);
	fixture_version_match = NEVERC_KRT_VER_UNKNOWN;
	assert(neverc_krt_dir_filter_available() == 0);
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) != 0);

	fixture_version_match = NEVERC_KRT_VER_EXACT;
	fixture_layout_certificates = NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT;
}

static void check_failure_paths(void)
{
	struct neverc_krt_dir_filter_scope scope;
	struct neverc_krt_dir_filter_scope other_scope;
	struct fixture_reader reader = { 0 };
	struct fixture_reader other_reader = { 0 };
	fixture_filldir_int_fn actor;
	void *proxy = NULL;
	void *other_proxy = NULL;

	fixture_version_match = NEVERC_KRT_VER_EXACT;
	fixture_layout_certificates = NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT;
	fixture_caps.filldir_abi = NEVERC_KRT_FILLDIR_ABI_RETURNS_INT;
	reader.context.actor = (void *)fixture_actor_int;

	fixture_fail_next_read = 1;
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == -3);
	assert(proxy == NULL);

	reader.context.actor = NULL;
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == -4);
	reader.context.actor = (void *)fixture_actor_int;

	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == 0);
	memcpy(&actor, proxy, sizeof(actor));
	other_reader.context.actor = (void *)actor;
	assert(neverc_krt_dir_filter_begin(&other_scope, &other_reader.context,
			hide_named_entry, NULL, &other_proxy) == -5);
	assert(other_proxy == NULL);
	assert(neverc_krt_dir_filter_end(&scope) == 0);

	reader.calls = 0;
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == 0);
	memcpy(&actor, proxy, sizeof(actor));
	fixture_fail_next_write = 1;
	assert(actor(proxy, "show", 4, 1, 1, 4) == -1);
	assert(reader.calls == 0);
	assert(neverc_krt_dir_filter_end(&scope) == -3);

	reader.calls = 0;
	reader.update_pos = 1;
	reader.actor_pos = 901;
	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == 0);
	memcpy(&actor, proxy, sizeof(actor));
	fixture_fail_next_read = 1;
	assert(actor(proxy, "show", 4, 1, 1, 4) == -1);
	assert(reader.calls == 1);
	assert(neverc_krt_dir_filter_end(&scope) == -3);
	assert(reader.context.pos == 901);
	reader.update_pos = 0;

	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == 0);
	fixture_fail_next_write = 1;
	assert(neverc_krt_dir_filter_end(&scope) == -2);
	assert(neverc_krt_dir_filter_end(&scope) == -1);

	assert(neverc_krt_dir_filter_begin(&scope, &reader.context,
			hide_named_entry, NULL, &proxy) == 0);
	assert(neverc_krt_dir_filter_end(&scope) == 0);
	assert(neverc_krt_dir_filter_end(&scope) == -1);
}

int main(void)
{
	check_int_actor_semantics();
	check_bool_actor_semantics();
	check_many_independent_scopes();
	check_match_contracts();
	check_invalid_contracts_fail_closed();
	check_failure_paths();
	return 0;
}
