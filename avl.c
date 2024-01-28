#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include "avl.h"


typedef unsigned avl_dir_t;


/** @brief Signed byte max-of-two */
static signed char avl_max(signed char x, signed char y)
{
    return x > y ? x : y;
}


/** @brief Rotate @p root in direction @p dir
 *  @param root
 *      Pointer to pointer to root node to rotate
 *  @param dir
 *      Direction to rotate: 0 for a left rotation, 1 for a right
 */
static void avl_rotate(struct avl **root, avl_dir_t dir)
{
    struct avl *A = *root, *B, *y;
    signed char chg;

    B = A->next[!dir];
    y = B->next[dir];
    *root = B;
    A->next[!dir] = y;
    B->next[dir] = A;
    chg = avl_max(dir ? -B->balance : B->balance, 0) + 1;
    A->balance += dir ? chg : -chg;
    chg = avl_max(dir ? A->balance : -A->balance, 0) + 1;
    B->balance += dir ? chg : -chg;
}


/** @brief Determine if a double rotation is required
 *  @param rbal
 *      Root node (to be rotated) balance. This should never be zero
 *  @param cbal
 *      Balance of the child node, in the direction of the imbalance
 *  @returns Nonzero if you gotta zig before you zag
 */
static int avl_double_rot(signed char rbal, signed char cbal)
{
    if (rbal > 0) {
        return cbal < 0;
    } else {
        return cbal > 0;
    }
}


/** @brief Restructure @p root if needed to restore the AVL property after an
 *      insertion
 *  @param root
 *      Address of the pointer to the root node
 */
static void avl_restructure(struct avl **root)
{
    signed char bal = (*root)->balance, bal2;
    avl_dir_t inext;

    if (bal < -1 || bal > 1) {
        inext = bal < 0 ? 0 : 1;
        bal2 = (*root)->next[inext]->balance;
        if (avl_double_rot(bal, bal2)) {
            avl_rotate(&(*root)->next[inext], inext);
        }
        avl_rotate(root, !inext);
    }
}


struct avl_insctx {
    jmp_buf      env;
    volatile int ret;
};


/** @brief Attempt to restructure @p node and longjmp if the subtree height
 *      remains unchanged
 *  @param node
 *      Node to try rebalancing
 *  @param ctx
 *      Deletion context
 */
static void avl_insert_commit(struct avl **node, struct avl_insctx *ctx)
{
    avl_restructure(node);
    if (!(*node)->balance) {
        longjmp(ctx->env, 1);
    }
}


/** @brief Recursive insertion
 *  @param root
 *      Root node pointer address
 *  @param node
 *      Node to insert
 *  @param cmpfn
 *      Comparison function
 *  @param joinfn
 *      Possibly NULL joining function
 *  @param ctx
 *      Context struct for insertion
 */
static void avl_insert_thunk(struct avl       **root,
                             struct avl        *node,
                             avl_cmpfn_t       *cmpfn,
                             avl_joinfn_t      *joinfn,
                             struct avl_insctx *ctx)
{
    struct avl **next;
    int cmp;

    if (!*root) {
        *root = node;
        return;
    }
    cmp = cmpfn(node, *root);
    if (!cmp) {
        if (joinfn) {
            joinfn(root, node);
        }
        ctx->ret = 1;
        longjmp(ctx->env, 1);
    }
    next = &(*root)->next[cmp < 0 ? 0 : 1];
    avl_insert_thunk(next, node, cmpfn, joinfn, ctx);
    (*root)->balance += cmp < 0 ? -1 : 1;
    avl_insert_commit(root, ctx);
}


int avl_insert(struct avl **root,  struct avl   *node,
               avl_cmpfn_t *cmpfn, avl_joinfn_t *joinfn)
{
    struct avl_insctx ctx;

    ctx.ret = 0;
    if (!setjmp(ctx.env)) {
        avl_insert_thunk(root, node, cmpfn, joinfn, &ctx);
    }
    return ctx.ret;
}


struct avl_delctx {
    jmp_buf              env;
    struct avl *volatile res;
};


/** @brief Attempt to restructure @p node and longjmp if the subtree height
 *      remains unchanged
 *  @param node
 *      Node to try rebalancing
 *  @param ctx
 *      Deletion context
 */
static void avl_delete_commit(struct avl **node, struct avl_delctx *ctx)
{
    avl_restructure(node);
    if ((*node)->balance) {
        longjmp(ctx->env, 1);
    }
}


/** @brief Find the minimum node in the tree at @p root and unlink it
 *  @param root
 *      Subtree root. This may NOT address a NULL pointer!
 *  @param ctx
 *      Deletion context. The unlinked node is placed here on completion, and
 *      the jmp_buf is used to exit when no more rotations are required
 */
static void avl_unlink_min(struct avl **root, struct avl_delctx *ctx)
{
    if ((*root)->next[0]) {
        avl_unlink_min(&(*root)->next[0], ctx);
        (*root)->balance++;
        avl_delete_commit(root, ctx);
    } else {
        ctx->res = *root;
        *root = (*root)->next[1];
    }
}


/** @brief Delete @p root by replacing it with a valid replacement node
 *  @param root
 *      Root node to delete
 *  @param ctx
 *      Deletion context
 */
static void avl_delete_replace(struct avl **root, struct avl_delctx *ctx)
{
    struct avl_delctx ctx2;

    if (!(*root)->next[0] || !(*root)->next[1]) {
        *root = (*root)->next[0] ? (*root)->next[0] : (*root)->next[1];
    } else {
        if (!setjmp(ctx2.env)) {
            avl_unlink_min(&(*root)->next[1], &ctx2);
            /* Continue unwinding and rebalancing */
            *ctx2.res = **root;
            *root = ctx2.res;
            (*root)->balance--;
            avl_delete_commit(root, ctx);
        } else {
            /* No further rotations are required, but we still must update the
            pointers */
            *ctx2.res = **root;
            *root = ctx2.res;
            longjmp(ctx->env, 1);
        }
    }
}


/** @brief Search for the node to be deleted
 *  @param root
 *      Root node pointer
 *  @param test
 *      Test node used for comparing to the query node
 *  @param cmpfn
 *      Comparison function
 *  @param ctx
 *      Deletion context
 */
static void avl_delete_thunk(struct avl       **root,
                             struct avl        *test,
                             avl_cmpfn_t       *cmpfn,
                             avl_delfn_t       *delfn,
                             struct avl_delctx *ctx)
{
    struct avl **next;
    int cmp;

    if (!*root) {
        longjmp(ctx->env, 1);
    }
    cmp = cmpfn(test, *root);
    if (!cmp) {
        ctx->res = *root;
        if (delfn && !delfn(*root)) {
            longjmp(ctx->env, 1);
        }
        avl_delete_replace(root, ctx);
    } else {
        next = &(*root)->next[cmp < 0 ? 0 : 1];
        avl_delete_thunk(next, test, cmpfn, delfn, ctx);
        (*root)->balance += cmp < 0 ? 1 : -1;
        avl_delete_commit(root, ctx);
    }
}


struct avl *avl_delete(struct avl **root,  struct avl  *node,
                       avl_cmpfn_t *cmpfn, avl_delfn_t *delfn)
{
    struct avl_delctx ctx;

    ctx.res = NULL;
    if (!setjmp(ctx.env)) {
        avl_delete_thunk(root, node, cmpfn, delfn, &ctx);
    }
    return ctx.res;
}


struct avl *avl_lookup(struct avl *root, struct avl *query, avl_cmpfn_t *cmpfn)
{
    int cmp;

    while (root) {
        cmp = cmpfn(query, root);
        if (!cmp) {
            break;
        } else {
            root = root->next[cmp < 0 ? 0 : 1];
        }
    }
    return root;
}


struct avl_iterctx {
    jmp_buf      env;
    volatile int ret;
};


/** @brief Invoke the callback, and longjmp if requested
 *  @param node
 *      Tree node to supply to the callback
 *  @param fn
 *      Callback function
 *  @param data
 *      Callback data
 *  @param ctx
 *      Iteration context
 */
static void avl_foreach_invoke(struct avl         *node,
                               avl_iterfn_t       *fn,
                               void               *data,
                               struct avl_iterctx *ctx)
{
    int res;

    res = fn(node, data);
    if (res) {
        ctx->ret = res;
        longjmp(ctx->env, 1);
    }
}


/** @brief Iterate in preorder */
static void avl_foreach_preorder(struct avl         *node,
                                 avl_iterfn_t       *fn,
                                 void               *data,
                                 struct avl_iterctx *ctx)
{
    if (node) {
        avl_foreach_invoke(node, fn, data, ctx);
        avl_foreach_preorder(node->next[0], fn, data, ctx);
        avl_foreach_preorder(node->next[1], fn, data, ctx);
    }
}


/** @brief Iterate in-order */
static void avl_foreach_inorder(struct avl         *node,
                                avl_iterfn_t       *fn,
                                void               *data,
                                struct avl_iterctx *ctx)
{
    if (node) {
        avl_foreach_inorder(node->next[0], fn, data, ctx);
        avl_foreach_invoke(node, fn, data, ctx);
        avl_foreach_inorder(node->next[1], fn, data, ctx);
    }
}


/** @brief Iterate in postorder */
static void avl_foreach_postorder(struct avl         *node,
                                  avl_iterfn_t       *fn,
                                  void               *data,
                                  struct avl_iterctx *ctx)
{
    if (node) {
        avl_foreach_postorder(node->next[0], fn, data, ctx);
        avl_foreach_postorder(node->next[1], fn, data, ctx);
        avl_foreach_invoke(node, fn, data, ctx);
    }
}


int avl_foreach(struct avl   *root,
                avl_iterdir_t dir,
                avl_iterfn_t *fn,
                void         *data)
{
    struct avl_iterctx ctx;

    ctx.ret = 0;
    if (!setjmp(ctx.env)) {
        switch (dir) {
        case AVL_PREORDER:
            avl_foreach_preorder(root, fn, data, &ctx);
            break;
        case AVL_INORDER:
            avl_foreach_inorder(root, fn, data, &ctx);
            break;
        case AVL_POSTORDER:
            avl_foreach_postorder(root, fn, data, &ctx);
            break;
        }
    }
    return ctx.ret;
}
