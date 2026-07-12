/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RBTREE_H
#define _NEVERC_KRT_LINUX_RBTREE_H

#include <linux/types.h>
#include <linux/kernel.h>

struct rb_node {
	unsigned long __rb_parent_color;
	struct rb_node *rb_right;
	struct rb_node *rb_left;
};

struct rb_root {
	struct rb_node *rb_node;
};

#define RB_ROOT (struct rb_root) { (void *)0 }
#define rb_entry(ptr, type, member) container_of(ptr, type, member)

void rb_insert_color(struct rb_node *node, struct rb_root *root);
void rb_erase(struct rb_node *node, struct rb_root *root);
struct rb_node *rb_first(const struct rb_root *root);
struct rb_node *rb_last(const struct rb_root *root);
struct rb_node *rb_next(const struct rb_node *node);
struct rb_node *rb_prev(const struct rb_node *node);
struct rb_node *rb_first_postorder(const struct rb_root *root);
struct rb_node *rb_next_postorder(const struct rb_node *node);
void rb_replace_node(struct rb_node *victim, struct rb_node *new_node,
		     struct rb_root *root);

static __always_inline void rb_link_node(struct rb_node *node,
					 struct rb_node *parent,
					 struct rb_node **rb_link)
{
	node->__rb_parent_color = (unsigned long)parent;
	node->rb_left = node->rb_right = (void *)0;
	*rb_link = node;
}

#define rb_entry_safe(ptr, type, member)                                      \
	({                                                                     \
		__typeof__(ptr) __rb_ptr = (ptr);                               \
		__rb_ptr ? rb_entry(__rb_ptr, type, member) : (type *)0;         \
	})

#define rbtree_postorder_for_each_entry_safe(pos, n, root, field)             \
	for (pos = rb_entry_safe(rb_first_postorder(root),                    \
				 __typeof__(*pos), field);                       \
	     pos && ({                                                        \
		     n = rb_entry_safe(rb_next_postorder(&pos->field),        \
				       __typeof__(*pos), field);                \
		     1;                                                       \
	     });                                                               \
	     pos = n)

#endif /* _NEVERC_KRT_LINUX_RBTREE_H */
