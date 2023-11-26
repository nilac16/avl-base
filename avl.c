#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
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
 *      Direction to rotate
 */
static void avl_rotate(struct avl **root, avl_dir_t dir)
{
    struct avl *base = *root, *side, *swing;
    signed char chg;

    side = base->next[!dir];
    swing = side->next[dir];
    base->next[!dir] = swing;
    side->next[dir] = base;
    *root = side;
    chg = avl_max(dir ? -side->balance : side->balance, 0) + 1;
    base->balance += dir ? chg : -chg;
    chg = avl_max(dir ? base->balance : -base->balance, 0) + 1;
    side->balance += dir ? chg : -chg;
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


/** @brief Check the balance of @p root and rotate it if it needs correcting
 *  @param root
 *      Address of the pointer to the root node
 *  @note This function only applies standard rotations used during insertion
 *      and deletion. It will not magically rebalance a non-AVL tree
 */
static void avl_balance_check(struct avl **root)
{
    signed char bal = (*root)->balance, bal2;
    avl_dir_t inext;

    if (bal > -2 && bal < 2) {
        return;
    }
    inext = bal < 0 ? 0 : 1;
    bal2 = (*root)->next[inext]->balance;
    if (avl_double_rot(bal, bal2)) {
        avl_rotate(&(*root)->next[inext], inext);
    }
    avl_rotate(root, !inext);
}


struct avl_insctx {
    jmp_buf      env;
    volatile int ret;
};


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
    avl_balance_check(root);
    if (!(*root)->balance) {
        longjmp(ctx->env, 1);
    }
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


/** @brief Overwrite @p dst with @p src and move the right child of @p src to
 *      its place
 *  @param root
 *      Overwritten node. The pointer this points to will be LOST
 *  @param succ
 *      Node to overwrite. Its left child pointer will be LOST
 */
static void avl_overwrite(struct avl **root, struct avl **succ)
{
    struct avl *repl = *succ;

    /* Move succ's right child into its place */
    *succ = (*succ)->next[1];
    /* Overwrite the AVL part of succ with that of root. This leaves user data
    intact, but copies root's topological information to succ */
    *repl = **root;
    /* Update/overwrite the pointer to root with that to succ, saved earlier */
    *root = repl;
}


/** @brief Find the successor to @p root and use it to replace @p root
 *  @param root
 *      Root node being replaced
 *  @param succ
 *      Successor search node. Pass this the address of the (nonnull) pointer to
 *      the right child of @p root to start
 *  @param ctx
 *      Deletion context
 */
static void avl_delete_replace(struct avl       **root,
                               struct avl       **succ,
                               struct avl_delctx *ctx)
{
    if (!(*succ)->next[0]) {
        avl_overwrite(root, succ);
        return;
    }
    avl_delete_replace(root, &(*succ)->next[0], ctx);
    (*succ)->balance++;
    avl_balance_check(succ);
    if ((*succ)->balance) {
        longjmp(ctx->env, 1);
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
        if (!(*root)->next[1]) {
            *root = (*root)->next[0];
            return;
        }
        avl_delete_replace(root, &(*root)->next[1], ctx);
        cmp = 1;
    } else {
        next = &(*root)->next[cmp < 0 ? 0 : 1];
        avl_delete_thunk(next, test, cmpfn, delfn, ctx);
    }
    (*root)->balance -= cmp < 0 ? -1 : 1;
    avl_balance_check(root);
    if ((*root)->balance) {
        longjmp(ctx->env, 1);
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
