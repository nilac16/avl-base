#pragma once

#ifndef AVL_H
#define AVL_H


/** @brief A generic AVL tree node */
struct avl {
    struct avl *next[2];    /* The child pointers */
    signed char balance;    /* The current AVL balance */
};


/** @brief Compare two AVL nodes
 *  @param n1
 *      Left operand
 *  @param n2
 *      Right operand
 *  @return Negative if @p n1 < @p n2, positive if @p n1 > @p n2, and zero if
 *      they are equal
 */
typedef int avl_cmpfn_t(const struct avl *n1, const struct avl *n2);


/** @brief Execute arbitrary code to join @p join to @p node. This function is
 *      intended to facilitate creation of multisets
 *  @param node
 *      Double-pointer to the node in the tree that compares equal to @p join.
 *      You can safely overwrite the pointer to @p node with @p join if you want
 *      to, but be sure to free(3) *node afterwards if you need to
 *  @param join
 *      The node being inserted that was found to be equal to @p node. Note that
 *      it is safe (and generally expected) to free(3) this node from this
 *      function in all contexts (the union function will leak its memory
 *      otherwise)
 */
typedef void avl_joinfn_t(struct avl **node, struct avl *join);


/** @brief Insert @p node into the AVL tree, using @p cmpfn to establish a
 *      total ordering
 *  @param root
 *      Address of the pointer to the root node. This cannot be NULL, but if the
 *      tree is empty this should point to a NULL pointer
 *  @param node
 *      Preallocated node to insert into the tree. This memory is externally
 *      managed. If insertion is successful, the tree "borrows" the node while
 *      it contains it, but you are ultimately responsible for its memory.
 *      Ensure in some way that the avl part of this object is zeroed before
 *      this call or hilarity will ensue
 *  @param cmpfn
 *      Comparison function
 *  @param joinfn
 *      Function called if @p node is already found in the tree. This may be
 *      NULL, in which case it is ignored
 *  @returns Nonzero if @p node was already in the tree
 */
int avl_insert(struct avl **root,  struct avl   *node,
               avl_cmpfn_t *cmpfn, avl_joinfn_t *joinfn);


/** @brief Callback invoked on a node before it is deleted. This is intended to
 *      facilitate implementation of multisets
 *  @param node
 *      The node tentatively being deleted. It is *NOT safe* to free(3) the node
 *      from this function
 *  @return Nonzero to actually delete this node. Zero terminates the algorithm.
 *      Be wary that early termination still results in the node pointer being
 *      returned from the deletion method
 */
typedef int avl_delfn_t(struct avl *node);


/** @brief Search for a node that compares equal to @p node and remove it from
 *      the tree
 *  @param root
 *      Address of the tree root pointer
 *  @param node
 *      Test node used for querying the node to be deleted. You only need to set
 *      the information required for @p cmpfn
 *  @param cmpfn
 *      Comparison function
 *  @param delfn
 *      Deletion function. If this is NULL, it is ignored and deletion occurs
 *      normally
 *  @returns A pointer to the removed node, or NULL if it was not found. The
 *      correct pointer is still returned even if @p delfn stops this algorithm
 *      from removing @p node from the tree. Be careful not to free(3) it
 *      blindly if it is still in the tree
 */
struct avl *avl_delete(struct avl **root,  struct avl  *node,
                       avl_cmpfn_t *cmpfn, avl_delfn_t *delfn);


/** @brief Look up a node comparing equal to @p query
 *  @param root
 *      Tree root
 *  @param query
 *      Test node comparing equal to the query node. This only needs to contain
 *      the information required for @p cmpfn
 *  @param cmpfn
 *      Comparison function
 *  @returns A pointer to the node in the tree that matches @p query, or NULL if
 *      such a node could not be found
 */
struct avl *avl_lookup(struct avl *root, struct avl *query, avl_cmpfn_t *cmpfn);


/** @brief Iterator function signature
 *  @param node
 *      The node, i.e. current iterator value
 *  @param data
 *      Extra user data
 *  @return Nonzero to immediately terminate
 *  @note If your nodes are heap-allocated, a postorder traversal can be used to
 *      free(3) the entire tree in one go
 */
typedef int avl_iterfn_t(struct avl *node, void *data);


/** @brief Iteration direction within the tree */
typedef enum {
    AVL_PREORDER,   /* Callback is invoked before traveling to the children */
    AVL_INORDER,    /* Callback is invoked in between children */
    AVL_POSTORDER   /* Callback is invoked after traveling to the children */
} avl_iterdir_t;

 
/** @brief Iterate over the tree
 *  @param root
 *      Tree root
 *  @param dir
 *      Iteration direction
 *  @param fn
 *      Iteration callback
 *  @param data
 *      Callback data
 *  @returns Nonzero if it was told to do so by the callback. The return value
 *      is the callback's return value, otherwise zero if iteration completed
 *      over the entire tree
 */
int avl_foreach(struct avl   *root,
                avl_iterdir_t dir,
                avl_iterfn_t *fn,
                void         *data);


#endif /* AVL_H */
