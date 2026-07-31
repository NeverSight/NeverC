#include "neverc/std/context.h"

#include <stdio.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    neverc_context_cancel_handle_t *cancel = NULL;
#ifdef __neverc__
    neverc_context_t *background = context.background();
    neverc_context_t *ctx =
        context.with_cancel_handle(background, &cancel);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);
    CHECK(context.done(ctx) == 0);
    context.cancel_handle_cancel(cancel);
    CHECK(context.done(ctx) == 1);
    context.cancel_handle_free(cancel);
    context.free(ctx);

    cancel = NULL;
    ctx = context.with_timeout_handle(background, 60000, &cancel);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);
    context.cancel_handle_cancel(cancel);
    CHECK(context.done(ctx) == 1);
    context.cancel_handle_free(cancel);
    context.free(ctx);

    cancel = NULL;
    ctx = context.with_deadline_handle(background, INT64_MAX, &cancel);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);
    context.cancel_handle_cancel(cancel);
    CHECK(context.done(ctx) == 1);
    context.cancel_handle_free(cancel);
    context.free(ctx);
    context.free(background);
#else
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *ctx =
        neverc_context_with_cancel_handle(background, &cancel);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);
    CHECK(neverc_context_done(ctx) == 0);
    neverc_context_cancel_handle_cancel(cancel);
    CHECK(neverc_context_done(ctx) == 1);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);

    cancel = NULL;
    ctx = neverc_context_with_timeout_handle(background, 60000, &cancel);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);
    neverc_context_cancel_handle_cancel(cancel);
    CHECK(neverc_context_done(ctx) == 1);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);

    cancel = NULL;
    ctx = neverc_context_with_deadline_handle(
        background, INT64_MAX, &cancel);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);
    neverc_context_cancel_handle_cancel(cancel);
    CHECK(neverc_context_done(ctx) == 1);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);
    neverc_context_free(background);
#endif

    puts("passed");
    return 0;
}
