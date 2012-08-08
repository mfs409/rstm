/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef LIST_HPP__
#define LIST_HPP__

// We construct other data structures from the List. In order to do their
// sanity checks correctly, we might need to pass in a validation function of
// this type
typedef bool (*verifier)(uint32_t, uint32_t);

// Set of LLNodes represented as a linked list in sorted order
class List
{

  // Node in a List
  struct Node
  {
      int m_val;
      Node* m_next;

      // ctors
      Node(int val = -1) : m_val(val), m_next() { }

      Node(int val, Node* next) : m_val(val), m_next(next) { }
  };

  public:

    Node* sentinel;

    List();

    // true iff val is in the data structure
    TM_CALLABLE
    bool lookup(TX_FIRST_PARAMETER int val) const;

    // standard IntSet methods
    TM_CALLABLE
    void insert(TX_FIRST_PARAMETER int val);

    // remove a node if its value = val
    TM_CALLABLE
    void remove(TX_FIRST_PARAMETER int val);

    // make sure the list is in sorted order
    bool isSane() const;

    // make sure the list is in sorted order and for each node x,
    // v(x, verifier_param) is true
    bool extendedSanityCheck(verifier v, uint32_t param) const;

    // find max and min
    TM_CALLABLE
    int findmax(TX_LONE_PARAMETER) const;

    TM_CALLABLE
    int findmin(TX_LONE_PARAMETER) const;

    // overwrite all elements up to val
    TM_CALLABLE
    void overwrite(TX_FIRST_PARAMETER int val);
};


// constructor just makes a sentinel for the data structure
List::List() : sentinel(new Node()) { }

// simple sanity check: make sure all elements of the list are in sorted order
bool List::isSane(void) const
{
    const Node* prev(sentinel);
    const Node* curr((prev->m_next));

    while (curr != NULL) {
        if ((prev->m_val) >= (curr->m_val))
            return false;
        prev = curr;
        curr = curr->m_next;
    }
    return true;
}

// extended sanity check, does the same as the above method, but also calls v()
// on every item in the list
bool List::extendedSanityCheck(verifier v, uint32_t v_param) const
{
    const Node* prev(sentinel);
    const Node* curr((prev->m_next));
    while (curr != NULL) {
        if (!v((curr->m_val), v_param) || ((prev->m_val) >= (curr->m_val)))
            return false;
        prev = curr;
        curr = prev->m_next;
    }
    return true;
}

// insert method; find the right place in the list, add val so that it is in
// sorted order; if val is already in the list, exit without inserting
TM_CALLABLE
void List::insert(TX_FIRST_PARAMETER int val)
{
    // traverse the list to find the insertion point
    const Node* prev(sentinel);
    const Node* curr(TM_READ(prev->m_next));

    while (curr != NULL) {
        if (TM_READ(curr->m_val) >= val)
            break;
        prev = curr;
        curr = TM_READ(prev->m_next);
    }

    // now insert new_node between prev and curr
    if (!curr || (TM_READ(curr->m_val) > val)) {
        Node* insert_point = const_cast<Node*>(prev);

        // create the new node
        Node* i = (Node*)TM_ALLOC(sizeof(Node));
        i->m_val = val;
        i->m_next = const_cast<Node*>(curr);
        TM_WRITE(insert_point->m_next, i);
    }
}

// search function
TM_CALLABLE
bool List::lookup(TX_FIRST_PARAMETER int val) const
{
    bool found = false;
    const Node* curr(sentinel);
    curr = TM_READ(curr->m_next);

    while (curr != NULL) {
        if (TM_READ(curr->m_val) >= val)
            break;
        curr = TM_READ(curr->m_next);
    }

    found = ((curr != NULL) && (TM_READ(curr->m_val) == val));
    return found;
}

// findmax function
TM_CALLABLE
int List::findmax(TX_LONE_PARAMETER) const
{
    int max = -1;
    const Node* curr(sentinel);
    while (curr != NULL) {
        max = TM_READ(curr->m_val);
        curr = TM_READ(curr->m_next);
    }
    return max;
}

// findmin function
TM_CALLABLE
int List::findmin(TX_LONE_PARAMETER) const
{
    int min = -1;
    const Node* curr(sentinel);
    curr = TM_READ(curr->m_next);
    if (curr != NULL)
        min = TM_READ(curr->m_val);
    return min;
}

// remove a node if its value == val
TM_CALLABLE
void List::remove(TX_FIRST_PARAMETER int val)
{
    // find the node whose val matches the request
    const Node* prev(sentinel);
    const Node* curr(TM_READ(prev->m_next));
    while (curr != NULL) {
        // if we find the node, disconnect it and end the search
        if (TM_READ(curr->m_val) == val) {
            Node* mod_point = const_cast<Node*>(prev);
            TM_WRITE(mod_point->m_next, TM_READ(curr->m_next));

            // delete curr...
            TM_FREE(const_cast<Node*>(curr));
            break;
        }
        else if (TM_READ(curr->m_val) > val) {
            // this means the search failed
            break;
        }
        prev = curr;
        curr = TM_READ(prev->m_next);
    }
}

// search function
TM_CALLABLE
void List::overwrite(TX_FIRST_PARAMETER int val)
{
    const Node* curr(sentinel);
    curr = TM_READ(curr->m_next);

    while (curr != NULL) {
        if (TM_READ(curr->m_val) >= val)
            break;
        Node* wcurr = const_cast<Node*>(curr);
        TM_WRITE(wcurr->m_val, TM_READ(wcurr->m_val));
        curr = TM_READ(wcurr->m_next);
    }
}

#endif // LIST_HPP__
