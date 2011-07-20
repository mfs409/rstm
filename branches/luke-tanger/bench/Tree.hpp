/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef TREE_HPP__
#define TREE_HPP__

#include <climits>
#include <api/api.hpp> // need this for malloc and free

class RBTree
{
    enum Color { RED, BLACK };

    // Node of an RBTree
    struct RBNode
    {
        Color   m_color;
        int     m_val;
        RBNode* m_parent;
        int     m_ID;
        RBNode* m_child[2];

        // basic constructor
        RBNode(Color color = BLACK,
               long val = -1,
               RBNode* parent = NULL,
               long ID = 0,
               RBNode* child0 = NULL,
               RBNode* child1 = NULL)
            : m_color(color), m_val(val), m_parent(parent), m_ID(ID)
        {
            m_child[0] = child0;
            m_child[1] = child1;
        }
    };

    // helper functions for sanity checks
    static int blackHeight(const RBNode* x);
    static bool redViolation(const RBNode* p_r, const RBNode* x);
    static bool validParents(const RBNode* p, int xID, const RBNode* x);
    static bool inOrder(const RBNode* x, int lowerBound, int upperBound);

  public:
    RBNode* sentinel;

    RBTree();

    // standard IntSet methods
    TM_CALLABLE
    bool lookup(int val TM_ARG) const NOINLINE;

    TM_CALLABLE
    void insert(int val TM_ARG) NOINLINE;

    TM_CALLABLE
    void remove(int val TM_ARG) NOINLINE;

    TM_CALLABLE
    void modify(int val TM_ARG) NOINLINE;

    bool isSane() const;
};

#endif // TREE_HPP__
