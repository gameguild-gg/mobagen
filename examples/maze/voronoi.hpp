// Source: https://github.com/samkusin/gamelabs/tree/master/voronoi
// with a bug fix (ennetws)
/* Usage:
using namespace cinekine;
...
voronoi::Sites sites;
for(int i = 0; i < 5; i++) sites.push_back(voronoi::Vertex(rand()*xBound/RAND_MAX,rand()*yBound/RAND_MAX));
...
voronoi::Graph graph = voronoi::build(std::move(sites), xBound, yBound);
...
for (auto& cell: cells) for (auto& halfedge : cell.halfEdges) auto& edge = edges[halfedge.edge];
*/

/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2013 Samir Sinha
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 *  This is a C++ implementation of Raymond Hill's Javascript implementation of
 *  Steven Fortune's algorithm to generate Voronoi diagrams.  Note it is roughly
 *  a line-by-line translation of the original Javascript to C++11 (with
 *  obvious optimizations and modifications tailored to C++)
 *
 *  The original license including links to the original source is supplied
 *  below.  Where appropriate, I've kept the original source comments explaining
 *  the algorithm, and highlighted sections I've made changes to (ssinha)
 *
 *  Copyright (C) 2010-2013 Raymond Hill: https://github.com/gorhill/Javascript-Voronoi
 *  MIT License: See https://github.com/gorhill/Javascript-Voronoi/LICENSE.md
 */

#ifndef CK_VORONOI_HPP
#define CK_VORONOI_HPP

#include <vector>
#include <cmath>
#include <cstdint>

namespace cinekine
{
/*
 Copyright (C) 2010-2013 Raymond Hill: https://github.com/gorhill/Javascript-Voronoi
 MIT License: See https://github.com/gorhill/Javascript-Voronoi/LICENSE.md
 */
/*
 Author: Raymond Hill (rhill@raymondhill.net)
 Contributor: Jesse Morgan (morgajel@gmail.com)
 File: rhill-voronoi-core.js
 Version: 0.98
 Date: January 21, 2013
 Description: This is my personal Javascript implementation of
 Steven Fortune's algorithm to compute Voronoi diagrams.

 License: See https://github.com/gorhill/Javascript-Voronoi/LICENSE.md
 Credits: See https://github.com/gorhill/Javascript-Voronoi/CREDITS.md
 History: See https://github.com/gorhill/Javascript-Voronoi/CHANGELOG.md
 */

// ssinha - Taken mostly from the Javascript impl - converted to a template
// for future reuse.

// ---------------------------------------------------------------------------
// Red-Black tree code (based on C version of "rbtree" by Franck Bui-Huu
// https://github.com/fbuihuu/libtree/blob/master/rb.c

template<class T>
class RBNodeBase
{
 public:
  RBNodeBase() :
      _parent(nullptr),
      _prev(nullptr),
      _next(nullptr),
      _left(nullptr),
      _right(nullptr),
      _red(false) {}

  void setParent(T* parent)   { _parent = parent; }
  const T* parent() const     { return _parent; }
  T* parent()                 { return _parent; }
  void setPrevious(T* prev)   { _prev = prev; }
  const T* previous() const   { return _prev; }
  T* previous()               { return _prev; }
  void setNext(T* next)       { _next = next; }
  const T* next() const       { return _next; }
  T* next()                   { return _next; }
  void setLeft(T* left)       { _left = left; }
  const T* left() const       { return _left; }
  T* left()                   { return _left; }
  void setRight(T* right)     { _right = right; }
  const T* right() const      { return _right; }
  T* right()                  { return _right; }
  void setRed()               { _red = true; }
  bool red()                  { return _red; }
  void setBlack()             { _red = false; }
  bool black()                { return !_red; }

 private:
  T* _parent;
  T* _prev;
  T* _next;
  T* _left;
  T* _right;
  bool _red;
};

/**
 * RBNode must implement the following:
 *
 *  RBNode* previous();
 *  void setPrevious(RBNode* node);
 *
 *  RBNode* next();
 *  void setNext(RBNode* node);
 *
 *  RBNode* left();
 *  void setLeft(RBNode* node);
 *
 *  RBNode* right();
 *  void setRight(RBNode* node);
 *
 *  RBNode* parent();
 *  void setParent(RBNode* node);
 *
 *  void setRed();
 *  void setBlack();
 *  bool red();
 *  bool black();
 */

template<class RBNode>
class RBTree
{
 public:
  RBTree() : _root(nullptr) {}

  RBNode* root() { return _root; }

  void insert(RBNode* node, RBNode* successor);
  void remove(RBNode* node);

 private:
  RBNode* _root;
  void rotateLeft(RBNode* node);
  void rotateRight(RBNode* node);
  RBNode* getFirst(RBNode* node);
  RBNode* getLast(RBNode* node);
};

template<class RBNode>
void RBTree<RBNode>::insert(RBNode* node, RBNode* successor)
{
  RBNode* parent = nullptr;
  if (node)
  {
    successor->setPrevious(node);
    successor->setNext(node->next());
    if (node->next())
    {
      node->next()->setPrevious(successor);
    }
    node->setNext(successor);
    if (node->right())
    {
      node = node->right();
      while (node->left())
        node = node->left();
      node->setLeft(successor);
    }
    else
    {
      node->setRight(successor);
    }
    parent = node;
  }
    // rhill 2011-06-07: if node is null, successor must be inserted
    // to the left-most part of the tree
  else if (_root)
  {
    node = getFirst(_root);
    successor->setPrevious(nullptr);
    successor->setNext(node);
    node->setPrevious(successor);
    node->setLeft(successor);
    parent = node;
  }
  else
  {
    successor->setPrevious(nullptr);
    successor->setNext(nullptr);
    _root = successor;
    parent = nullptr;
  }

  successor->setLeft(nullptr);
  successor->setRight(nullptr);
  successor->setParent(parent);
  successor->setRed();

  // Fixup the modified tree by recoloring nodes and performing
  // rotations (2 at most) hence the red-black tree properties are
  // preserved.
  RBNode* grandpa;
  RBNode* uncle;
  node = successor;
  while (parent && parent->red())
  {
    grandpa = parent->parent();
    if (parent == grandpa->left())
    {
      uncle = grandpa->right();
      if (uncle && uncle->red())
      {
        parent->setBlack();
        uncle->setBlack();
        grandpa->setRed();
        node = grandpa;
      }
      else
      {
        if (node == parent->right())
        {
          rotateLeft(parent);
          node = parent;
          parent = node->parent();
        }
        parent->setBlack();
        grandpa->setRed();
        rotateRight(grandpa);
      }
    }
    else
    {
      uncle = grandpa->left();
      if (uncle && uncle->red())
      {
        parent->setBlack();
        uncle->setBlack();
        grandpa->setRed();
        node = grandpa;
      }
      else
      {
        if (node == parent->left())
        {
          rotateRight(parent);
          node = parent;
          parent = node->parent();
        }
        parent->setBlack();
        grandpa->setRed();
        rotateLeft(grandpa);
      }
    }
    parent = node->parent();
  }
  _root->setBlack();
}

template<class RBNode>
void RBTree<RBNode>::remove(RBNode* node)
{
  if (node->next())
  {
    node->next()->setPrevious(node->previous());
  }
  if (node->previous())
  {
    node->previous()->setNext(node->next());
  }
  node->setNext(nullptr);
  node->setPrevious(nullptr);

  RBNode* parent = node->parent();
  RBNode* left = node->left();
  RBNode* right = node->right();
  RBNode* next = (!left) ? right : (!right) ? left : getFirst(right);

  if (parent)
  {
    if (parent->left() == node)
      parent->setLeft(next);
    else
      parent->setRight(next);
  }
  else
  {
    _root = next;
  }

  //  rhill - enforce red-black rules
  bool isRed;
  if (left && right)
  {
    isRed = next->red();
    if (node->red())
      next->setRed();
    else
      next->setBlack();
    next->setLeft(left);
    left->setParent(next);
    if (next != right)
    {
      parent = next->parent();
      next->setParent(node->parent());
      node = next->right();
      parent->setLeft(node);
      next->setRight(right);
      right->setParent(next);
    }
    else
    {
      next->setParent(parent);
      parent = next;
      node = next->right();
    }
  }
  else
  {
    isRed = node->red();
    node = next;
  }
  // 'node' is now the sole successor's child and 'parent' its
  // new parent (since the successor can have been moved)
  if (node)
  {
    node->setParent(parent);
  }
  if (isRed)
  {
    return;
  }
  if (node && node->red())
  {
    node->setBlack();
    return;
  }
  RBNode* sibling;
  do
  {
    if (node == _root)
      break;
    if (node == parent->left())
    {
      sibling = parent->right();
      if (sibling->red())
      {
        sibling->setBlack();
        parent->setRed();
        rotateLeft(parent);
        sibling = parent->right();
      }
      if ((sibling->left() && sibling->left()->red()) ||
          (sibling->right() && sibling->right()->red()))
      {
        if (!sibling->right() || sibling->right()->black())
        {
          sibling->left()->setBlack();
          sibling->setRed();
          rotateRight(sibling);
          sibling = parent->right();
        }
        if (parent->red())
          sibling->setRed();
        else
          sibling->setBlack();
        parent->setBlack();
        sibling->right()->setBlack();
        rotateLeft(parent);
        node = _root;
        break;
      }
    }
    else
    {
      sibling = parent->left();
      if (sibling->red())
      {
        sibling->setBlack();
        parent->setRed();
        rotateRight(parent);
        sibling = parent->left();
      }
      if ((sibling->left() && sibling->left()->red()) ||
          (sibling->right() && sibling->right()->red()))
      {
        if (!sibling->left() || sibling->left()->black())
        {
          sibling->right()->setBlack();
          sibling->setRed();
          rotateLeft(sibling);
          sibling = parent->left();
        }
        if (parent->red())
          sibling->setRed();
        else
          sibling->setBlack();
        parent->setBlack();
        sibling->left()->setBlack();
        rotateRight(parent);
        node = _root;
        break;
      }
    }
    sibling->setRed();
    node = parent;
    parent = parent->parent();
  }
  while (node->black());

  if (node)
    node->setBlack();
}

template<class RBNode>
void RBTree<RBNode>::rotateLeft(RBNode* node)
{
  RBNode* p = node;
  RBNode* q = node->right();
  RBNode* parent = p->parent();
  if (parent)
  {
    if (parent->left() == p)
      parent->setLeft(q);
    else
      parent->setRight(q);
  }
  else
  {
    _root = q;
  }
  q->setParent(parent);
  p->setParent(q);
  p->setRight(q->left());
  if (p->right())
  {
    p->right()->setParent(p);
  }
  q->setLeft(p);
}

template<class RBNode>
void RBTree<RBNode>::rotateRight(RBNode* node)
{
  RBNode* p = node;
  RBNode* q = node->left();
  RBNode* parent = p->parent();
  if (parent)
  {
    if (parent->left() == p)
      parent->setLeft(q);
    else
      parent->setRight(q);
  }
  else
  {
    _root = q;
  }
  q->setParent(parent);
  p->setParent(q);
  p->setLeft(q->right());
  if (p->left())
  {
    p->left()->setParent(p);
  }
  q->setRight(p);
}

template<class RBNode>
RBNode* RBTree<RBNode>::getFirst(RBNode* node)
{
  while (node->left())
    node = node->left();
  return node;
}

template<class RBNode>
RBNode* RBTree<RBNode>::getLast(RBNode* node)
{
  while (node->right())
    node = node->right();
  return node;
}

}   // namespace cinekine

namespace cinekine
{
namespace voronoi
{

/**
 * @class Vertex
 * @brief A 2D vertex used during voronoi computation
 */
class Vertex
{
 public:
  Vertex() = default;
  Vertex(float _x, float _y): x(_x), y(_y) {}
  static const Vertex undefined;
  operator bool() const {
    return !std::isnan(x) && !std::isnan(y);
  }
  float x, y;
};

inline bool operator==(const Vertex& v1, const Vertex& v2)
{
  return v1.x == v2.x && v1.y == v2.y;
}

inline bool operator!=(const Vertex& v1, const Vertex& v2)
{
  return v1.x != v2.x || v1.y != v2.y;
}

/**
 * @struct Site
 * @brief  Extends Site - Site metadata built on top of the site's position
 *         (a 2D vertex)
 */
struct Site: public Vertex
{
  int cell;

  Site(const Vertex& v) : Vertex(v.x, v.y), cell(-1) {}
  Site(): cell(-1) {}
};

/**
 * @struct Edge
 * @brief  Defines an edge of a Voronoi Cell and its placement
 *         relative to other Sites
 * Note that p0 and p1 are invalid unless explicitly set
 */
struct Edge
{
  int leftSite;
  int rightSite;
  Vertex p0;
  Vertex p1;

  Edge() :
      leftSite(-1), rightSite(-1),
      p0(Vertex::undefined),
      p1(Vertex::undefined) {}

  Edge(int lSite, int rSite) :
      leftSite(lSite), rightSite(rSite),
      p0(Vertex::undefined),
      p1(Vertex::undefined) {}

  void setStartpoint(int lSite, int rSite,
                     const Vertex& vertex);
  void setEndpoint(int lSite, int rSite,
                   const Vertex& vertex);
};

inline void Edge::setStartpoint(int lSite, int rSite,
                                const Vertex& vertex)
{
  if (!p0 && !p1)
  {
    p0 = vertex;
    leftSite = lSite;
    rightSite = rSite;
  }
  else if (leftSite == rSite)
  {
    p1 = vertex;
  }
  else
  {
    p0 = vertex;
  }
}

inline void Edge::setEndpoint(int lSite, int rSite,
                              const Vertex& vertex)
{
  setStartpoint(rSite, lSite, vertex);
}

/**
 * @struct HalfEdge
 * @brief  An edge segment as it relates to a single site (versus a
 *         full edge as it relates to two sites.)
 */
struct HalfEdge
{
  int site;
  int edge;
  float angle;
};

/** A half edges container */
typedef std::vector<HalfEdge> HalfEdges;

/**
 * @struct Cell
 * @brief  A cell containing a site surrounded by edges.
 *
 * It's possible to optimize this, specifying a start and end point to
 * a common HalfEdge vector (or pool?)
 *
 */
struct Cell
{
  int site;
  HalfEdges halfEdges;
  bool closeMe;

  Cell(int s) :
      site(s),
      halfEdges(),
      closeMe(false) {}
};

/** A Site container */
typedef std::vector<Site> Sites;
/** An edges container */
typedef std::vector<Edge> Edges;
/** A cells container */
typedef std::vector<Cell> Cells;

class Fortune;

/**
 * @class Graph
 * @brief A Voronoi cell graph from a collection of sites
 */
class Graph
{
 public:
  Graph();
  Graph(float xBound, float yBound, Sites&& sites);
  Graph(Graph&& other);

  Graph& operator=(Graph&& other);

  const Sites& sites() const {
    return _sites;
  }
  const Cells& cells() const {
    return _cells;
  }
  const Edges& edges() const {
    return _edges;
  }

 private:
  friend class Fortune;
  friend Graph build(Sites&& sites, float xBound, float yBound);

  int createEdge(int left, int right,
                 const Vertex& va=Vertex::undefined,
                 const Vertex& vb=Vertex::undefined);
  int createBorderEdge(int site,
                       const Vertex& va, const Vertex& vb);
  HalfEdge createHalfEdge(int edge, int lSite, int rSite);

  void clipEdges();
  bool clipEdge(int32_t edge);
  bool connectEdge(int edgeIdx);

  void closeCells();
  bool prepareHalfEdgesForCell(int32_t cell);

  Vertex getHalfEdgeStartpoint(const HalfEdge& halfEdge);
  Vertex getHalfEdgeEndpoint(const HalfEdge& halfEdge);

 private:
  Sites _sites;

  Edges _edges;
  Cells _cells;

  float _xBound;
  float _yBound;
};

///////////////////////////////////////////////////////////////////////

struct BeachArc;

struct CircleEvent : RBNodeBase<CircleEvent>
{
  BeachArc* arc;
  int site;
  float x;
  float y;
  float yCenter;

  CircleEvent() :
      arc(nullptr),
      site(-1),
      x(0.0f), y(0.0f), yCenter(0.0f) {}
};

struct BeachArc : RBNodeBase<BeachArc>
{
  int site;
  int edge;
  int refcnt;

  CircleEvent* circleEvent;

  BeachArc(int s) :
      site(s),
      edge(-1),
      refcnt(0),
      circleEvent(nullptr) {}
};

class Fortune
{
 public:
  Fortune(Graph& graph);
  ~Fortune();

  void addBeachSection(int site);
  void removeBeachSection(BeachArc* arc);

  CircleEvent* circleEvent() {
    return _topCircleEvent;
  }

 private:
  Graph& _graph;
  const Sites& _sites;
  Edges& _edges;

  RBTree<BeachArc> _beachline;
  RBTree<CircleEvent> _circleEvents;
  CircleEvent* _topCircleEvent;

  int _arcCnt, _circleCnt;

  BeachArc* allocArc(int site) {
    BeachArc* arc = new BeachArc(site);
    ++arc->refcnt;
    ++_arcCnt;
    return arc;
  }
  void releaseArc(BeachArc* arc) {
    if (arc->refcnt > 0)
    {
      --arc->refcnt;
      if (!arc->refcnt)
      {
        --_arcCnt;
        delete arc;
      }
    }
    else
    {
      printf("Releasing after 0 refcnt\n");
    }
  }

  CircleEvent* allocCircleEvent(BeachArc* arc) {
    auto event = new CircleEvent();
    event->arc = arc;
    ++event->arc->refcnt;
    ++_circleCnt;
    return event;
  }
  void freeCircleEvent(CircleEvent* event) {
    releaseArc(event->arc);
    --_circleCnt;
    delete event;
  }

  void attachCircleEvent(BeachArc* arc);
  void detachCircleEvent(BeachArc* arc);
  void detachBeachSection(BeachArc* arc);
  float leftBreakPoint(BeachArc* arc, float directrix);
  float rightBreakPoint(BeachArc* arc, float directrix);
};

//  Builds a graph given a collection of sites and a bounding box
//
Graph build(Sites&& sites, float xBound, float yBound);

}   // namespace voronoi
}   // namespace cinekine


namespace cinekine
{
namespace voronoi
{

const float kEpsilon = 1e-4;

const Vertex Vertex::undefined =
    Vertex(std::numeric_limits<float>::quiet_NaN(),
           std::numeric_limits<float>::quiet_NaN());

Fortune::Fortune(Graph& graph) :
    _graph(graph),
    _sites(graph._sites),
    _edges(graph._edges),
    _beachline(),
    _circleEvents(),
    _topCircleEvent(nullptr),
    _arcCnt(0),
    _circleCnt(0)
{
}

Fortune::~Fortune()
{
  printf("Arcs Remaining: %d\n", _arcCnt);
  printf("Circles Remaining: %d\n", _circleCnt);
  printf("Edges alloced: %lu\n", _edges.size());
}

float Fortune::leftBreakPoint(BeachArc* arc, float directrix)
{
  const Site& site = _sites[arc->site];
  float rfocx = site.x, rfocy = site.y;
  float pby2 = rfocy - directrix;

  // parabola in degenerate case where focus is on directrix
  if (pby2 == 0.0f)
    return rfocx;
  BeachArc *leftArc = arc->previous();
  if (!leftArc)
    return -std::numeric_limits<float>::infinity();

  const Site& leftSite = _sites[leftArc->site];
  float lfocx = leftSite.x, lfocy = leftSite.y;
  float plby2 = lfocy - directrix;
  if (plby2 == 0.0f)
    return lfocx;
  float hl = lfocx-rfocx;
  float aby2 = 1/pby2 - 1/plby2;
  float b = hl/plby2;
  if (aby2 != 0.0f)
  {
    float dist = std::sqrt(b*b -
        2*aby2 *
            (hl*hl/(-2*plby2) -
                lfocy + plby2/2 + rfocy-pby2/2));
    return (-b + dist)/aby2 + rfocx;
  }
  // both parabolas have same distance to directrix, thus break point is
  // midway
  return (rfocx+lfocx)/2;
}

float Fortune::rightBreakPoint(BeachArc* arc, float directrix)
{
  BeachArc* rightArc = arc->next();
  if (rightArc)
  {
    return leftBreakPoint(rightArc, directrix);
  }
  const Site& site = _sites[arc->site];
  return site.y == directrix ? site.x :
         std::numeric_limits<float>::infinity();
}

void Fortune::attachCircleEvent(BeachArc* arc)
{
  BeachArc* leftArc = arc->previous();
  BeachArc* rightArc = arc->next();
  if (!leftArc || !rightArc)
    return;
  // If site of left beachsection is same as site of
  // right beachsection, there can't be convergence
  if (leftArc->site == rightArc->site)
    return;

  const Site& leftSite = _sites[leftArc->site];
  const Site& centerSite = _sites[arc->site];
  const Site& rightSite = _sites[rightArc->site];

  // Find the circumscribed circle for the three sites associated
  // with the beachsection triplet.
  // rhill 2011-05-26: It is more efficient to calculate in-place
  // rather than getting the resulting circumscribed circle from an
  // object returned by calling Voronoi.circumcircle()
  // http://mathforum.org/library/drmath/view/55002.html
  // Except that I bring the origin at cSite to simplify calculations.
  // The bottom-most part of the circumcircle is our Fortune 'circle
  // event', and its center is a vertex potentially part of the final
  // Voronoi diagram.
  float bx = centerSite.x, by = centerSite.y;
  float ax = leftSite.x - bx, ay = leftSite.y - by;
  float cx = rightSite.x - bx, cy = rightSite.y - by;

  // If points l->c->r are clockwise, then center beach section does not
  // collapse, hence it can't end up as a vertex (we reuse 'd' here, which
  // sign is reverse of the orientation, hence we reverse the test.
  // http://en.wikipedia.org/wiki/Curve_orientation#Orientation_of_a_simple_polygon
  // rhill 2011-05-21: Nasty finite precision error which caused
  // circumcircle() to return infinites: 1e-12 seems to fix the problem.
  float d = 2*(ax*cy - ay*cx);
  if (d >= -2e-9)
    return;

  float ha = ax*ax + ay*ay;
  float hc = cx*cx + cy*cy;
  float x = (cy*ha - ay*hc)/d;
  float y = (ax*hc - cx*ha)/d;
  float ycenter = y + by;

  CircleEvent* circleEvent = allocCircleEvent(arc);
  circleEvent->site = arc->site;
  circleEvent->x = x+bx;
  circleEvent->y = ycenter + std::sqrt(x*x+y*y);
  circleEvent->yCenter = ycenter;
  arc->circleEvent = circleEvent;

  // find insertion point in RB-tree: circle events are ordered from
  // smallest to largest
  CircleEvent* predecessor = nullptr;
  CircleEvent* node = _circleEvents.root();
  while (node)
  {
    if (circleEvent->y < node->y ||
        (circleEvent->y == node->y && circleEvent->x <= node->x))
    {
      if (node->left())
      {
        node = node->left();
      }
      else
      {
        predecessor = node->previous();
        break;
      }
    }
    else
    {
      if (node->right())
      {
        node = node->right();
      }
      else
      {
        predecessor = node;
        break;
      }
    }
  }
  _circleEvents.insert(predecessor, circleEvent);
  if (!predecessor)
  {
    _topCircleEvent = circleEvent;
  }
}

void Fortune::detachCircleEvent(BeachArc* arc)
{
  CircleEvent* circleEvent = arc->circleEvent;
  if (circleEvent)
  {
    if (!circleEvent->previous())
    {
      _topCircleEvent = circleEvent->next();
    }
    _circleEvents.remove(circleEvent);
    if (_topCircleEvent != circleEvent)
    {
      freeCircleEvent(circleEvent);
    }
    arc->circleEvent = nullptr;
  }
}

void Fortune::addBeachSection(int siteIndex)
{
  const Site& site = _sites[siteIndex];
  float x = site.x, directrix = site.y;

  //  find the left and right beach sections which will surround
  //  the newly created beach section.
  BeachArc* leftArc = nullptr;
  BeachArc* rightArc = nullptr;
  BeachArc* node = _beachline.root();

  while (node)
  {
    float dxl = leftBreakPoint(node, directrix) - x;
    // x lessThanWithEpsilon xl => falls somewhere before the left edge
    // of the beachsection
    if (dxl > kEpsilon)     // float episilon
    {
      node = node->left();
    }
    else
    {
      float dxr = x - rightBreakPoint(node, directrix);
      // x greaterThanWithEpsilon xr => falls somewhere after the
      // right edge of the beachsection
      if (dxr > kEpsilon)
      {
        if (!node->right())
        {
          leftArc = node;
          break;
        }
        node = node->right();
      }
      else
      {
        // x equalWithEpsilon xl => falls exactly on the left edge
        // of the beachsection
        if (dxl > -kEpsilon)
        {
          leftArc = node->previous();
          rightArc = node;
        }
          // x equalWithEpsilon xr => falls exactly on the right edge
          // of the beachsection
        else if (dxr > -kEpsilon)
        {
          leftArc = node;
          rightArc = node->next();
        }
          // falls exactly somewhere in the middle of the
          // beachsection
        else
        {
          leftArc = rightArc = node;
        }
        break;
      }
    }
  }

  // create a new beach section object for the site and add it to RB-tree
  BeachArc* newArc = allocArc(siteIndex);

  _beachline.insert(leftArc, newArc);

  // [null,null]
  // least likely case: new beach section is the first beach section on the
  // beachline.
  // This case means:
  //   no new transition appears
  //   no collapsing beach section
  //   new beachsection become root of the RB-tree
  if (!leftArc && !rightArc)
    return;

  // [lArc,rArc] where lArc == rArc
  // most likely case: new beach section split an existing beach
  // section.
  // This case means:
  //   one new transition appears
  //   the left and right beach section might be collapsing as a result
  //   two new nodes added to the RB-tree
  if (leftArc == rightArc)
  {
    // invalidate circle event of split beach section
    detachCircleEvent(leftArc);
    // split the beach section into two separate beach sections
    rightArc = allocArc(leftArc->site);
    _beachline.insert(newArc, rightArc);

    // since we have a new transition between two beach sections,
    // a new edge is born
    newArc->edge = rightArc->edge = _graph.createEdge(leftArc->site,
                                                      newArc->site);

    attachCircleEvent(leftArc);
    attachCircleEvent(rightArc);
    return;
  }
  // [lArc,null]
  // even less likely case: new beach section is the *last* beach section
  // on the beachline -- this can happen *only* if *all* the previous
  // beach sections currently on the beachline share the same y value as
  // the new beach section.
  // This case means:
  //   one new transition appears
  //   no collapsing beach section as a result
  //   new beach section become right-most node of the RB-tree
  if (leftArc && !rightArc) {
    newArc->edge = _graph.createEdge(leftArc->site, newArc->site);
    return;
  }

  // [lArc,rArc] where lArc != rArc
  // somewhat less likely case: new beach section falls *exactly* in
  // between two existing beach sections
  // This case means:
  //   one transition disappears
  //   two new transitions appear
  //   the left and right beach section might be collapsing as a result
  //   only one new node added to the RB-tree
  if (leftArc != rightArc)
  {
    detachCircleEvent(leftArc);
    detachCircleEvent(rightArc);

    // an existing transition disappears, meaning a vertex is defined
    // at the disappearance point.
    // since the disappearance is caused by the new beachsection, the
    // vertex is at the center of the circumscribed circle of the left,
    // new and right beachsections.
    // http://mathforum.org/library/drmath/view/55002.html
    // Except that I bring the origin at A to simplify
    // calculation
    const Site& leftSite = _sites[leftArc->site];
    float ax = leftSite.x, ay = leftSite.y;
    float bx = site.x - ax, by = site.y - ay;
    const Site& rightSite = _sites[rightArc->site];
    float cx = rightSite.x - ax, cy = rightSite.y - ay;
    float d = 2*(bx*cy-by*cx);
    float hb = bx*bx + by*by;
    float hc = cx*cx + cy*cy;

    Vertex vertex(ax+(cy*hb-by*hc)/d, ay+(bx*hc-cx*hb)/d);
    // one transition disappear
    _edges[rightArc->edge].setStartpoint(leftArc->site,
                                         rightArc->site, vertex);

    newArc->edge = _graph.createEdge(leftArc->site, siteIndex,
                                     Vertex::undefined, vertex);
    rightArc->edge = _graph.createEdge(siteIndex, rightArc->site,
                                       Vertex::undefined, vertex);

    // check whether the left and right beach sections are collapsing
    // and if so create circle events, to handle the point of collapse.
    attachCircleEvent(leftArc);
    attachCircleEvent(rightArc);
    return;
  }
}

void Fortune::removeBeachSection(BeachArc* arc)
{
  CircleEvent* circle = arc->circleEvent;
  float x = circle->x, y = circle->yCenter;
  Vertex vertex(x, y);

  BeachArc* previous = arc->previous();
  BeachArc* next = arc->next();

  //  ssinha - keep track of what arcs we've staged for deletion
  //  the algorithm needs to reference these arcs after detaching
  std::vector<BeachArc*> detachedSections;

  // remove collapsed arc from beachline
  detachedSections.push_back(arc);
  ++arc->refcnt;
  detachBeachSection(arc);

  // there could be more than one empty arc at the deletion point, this
  // happens when more than two edges are linked by the same vertex,
  // so we will collect all those edges by looking up both sides of
  // the deletion point.
  // by the way, there is *always* a predecessor/successor to any
  // collapsed beach section, it's just impossible to have a collapsing
  // first/last beach sections on the beachline, since they obviously are
  // unconstrained on their left/right side.
  //
  BeachArc* leftArc = previous;
  while (leftArc->circleEvent &&
      std::abs(x-leftArc->circleEvent->x) < kEpsilon &&
      std::abs(y-leftArc->circleEvent->yCenter) < kEpsilon)
  {
    previous = leftArc->previous();
    detachedSections.insert(detachedSections.begin(), leftArc);
    ++leftArc->refcnt;
    detachBeachSection(leftArc);
    leftArc = previous;
  }

  // even though it is not disappearing, I will also add the beach section
  // immediately to the left of the left-most collapsed beach section, for
  // convenience, since we need to refer to it later as this beach section
  // is the 'left' site of an edge for which a start point is set.
  detachedSections.insert(detachedSections.begin(), leftArc);
  detachCircleEvent(leftArc);

  BeachArc* rightArc = next;
  while (rightArc->circleEvent &&
      std::abs(x-rightArc->circleEvent->x) < kEpsilon &&
      std::abs(y-rightArc->circleEvent->yCenter) < kEpsilon)
  {
    next = rightArc->next();
    detachedSections.push_back(rightArc);
    ++rightArc->refcnt;
    detachBeachSection(rightArc);
    rightArc = next;
  }
  // we also have to add the beach section immediately to the right of
  // the right-most collapsed beach section, since there is also a
  // disappearing transition representing an edge's start point on its
  // left.
  detachedSections.push_back(rightArc);
  detachCircleEvent(rightArc);

  // walk through all the disappearing transitions between beach
  // sections and set the start point of their (implied) edge.
  size_t numArcs = detachedSections.size();
  for (size_t iArc = 1; iArc < numArcs; ++iArc)
  {
    rightArc = detachedSections[iArc];
    leftArc = detachedSections[iArc-1];
    _edges[rightArc->edge].setStartpoint(leftArc->site,
                                         rightArc->site,
                                         vertex);
  }

  // create a new edge as we have now a new transition between
  // two beach sections which were previously not adjacent.
  // since this edge appears as a new vertex is defined, the vertex
  // actually define an end point of the edge (relative to the site
  // on the left)
  leftArc = detachedSections[0];
  rightArc = detachedSections[numArcs-1];
  detachedSections.erase(detachedSections.begin());
  detachedSections.pop_back();

  //  clear detached sections
  for (auto section: detachedSections)
  {
    releaseArc(section);
  }
  detachedSections.clear();

  //  do we need to dererence the "old" edge?
  rightArc->edge = _graph.createEdge(leftArc->site, rightArc->site,
                                     Vertex::undefined, vertex);
  // create circle events if any for beach sections left in the beachline
  // adjacent to collapsed sections
  attachCircleEvent(leftArc);
  attachCircleEvent(rightArc);
}

void Fortune::detachBeachSection(BeachArc* arc)
{
  detachCircleEvent(arc);
  _beachline.remove(arc);
  releaseArc(arc);
}

///////////////////////////////////////////////////////////////////////////
Graph::Graph() :
    _sites(),
    _edges(),
    _cells(),
    _xBound(0.0f), _yBound(0)
{
}

Graph::Graph(float xBound, float yBound, Sites&& sites) :
    _sites(std::move(sites)),
    _edges(),
    _cells(),
    _xBound(xBound), _yBound(yBound)
{

}

Graph::Graph(Graph&& other) :
    _sites(std::move(other._sites)),
    _edges(std::move(other._edges)),
    _cells(std::move(other._cells)),
    _xBound(other._xBound), _yBound(other._yBound)
{
  other._xBound = 0.0f;
  other._yBound = 0.0f;
}

Graph& Graph::operator=(Graph&& other)
{
  _sites = std::move(other._sites);
  _edges = std::move(other._edges);
  _cells = std::move(other._cells);
  _xBound = other._xBound;
  _yBound = other._yBound;
  other._xBound = 0.0f;
  other._yBound = 0.0f;
  return *this;
}

int Graph::createEdge(int left, int right,
                      const Vertex& va,
                      const Vertex& vb)
{
  _edges.emplace_back(left, right);
  int edge = (int)_edges.size()-1;

  if (va)
  {
    _edges[edge].setStartpoint(left, right, va);
  }
  if (vb)
  {
    _edges[edge].setEndpoint(left, right, vb);
  }

  const Site& l = _sites[left];
  const Site& r = _sites[right];

  _cells[l.cell].halfEdges.push_back(createHalfEdge(edge,left,right));
  _cells[r.cell].halfEdges.push_back(createHalfEdge(edge,right,left));

  return edge;
}

//  creates an edge that lies on the border of the owning graph
//
int Graph::createBorderEdge(int site, const Vertex& va, const Vertex& vb)
{
  _edges.emplace_back(site, -1);
  int edgeIdx = (int)(_edges.size()-1);
  Edge& edge = _edges[edgeIdx];
  edge.p0 = va;
  edge.p1 = vb;

  return edgeIdx;
}

HalfEdge Graph::createHalfEdge(int edge, int lSite, int rSite)
{
  HalfEdge halfedge;
  halfedge.edge = edge;
  halfedge.site = lSite;

  const Site& lSiteRef = _sites[lSite];
  if (rSite >= 0)
  {
    const Site& rSiteRef = _sites[rSite];
    halfedge.angle = std::atan2(rSiteRef.y-lSiteRef.y,
                                rSiteRef.x-lSiteRef.x);
  }
  else
  {
    const Edge& edgeRef = _edges[edge];
    if (edgeRef.leftSite == lSite)
    {
      halfedge.angle = std::atan2(edgeRef.p1.x-edgeRef.p0.x,
                                  edgeRef.p0.y-edgeRef.p1.y);
    }
    else
    {
      halfedge.angle = std::atan2(edgeRef.p0.x-edgeRef.p1.x,
                                  edgeRef.p1.y-edgeRef.p0.y);
    }
  }

  return halfedge;
}

bool Graph::connectEdge(int edgeIdx)
{
  const float xBound = _xBound;
  const float yBound = _yBound;

  Edge& edge = _edges[edgeIdx];

  // skip if end point already connected
  if (edge.p1)
    return true;

  const float xl = 0.0f,
      xr = xBound,
      yt = 0.0f,
      yb = yBound;
  const Site& lSite = _sites[edge.leftSite];
  const Site& rSite = _sites[edge.rightSite];
  const float lx = lSite.x,
      ly = lSite.y,
      rx = rSite.x,
      ry = rSite.y,
      fx = (lx+rx)/2,
      fy = (ly+ry)/2;

  // if we reach here, this means cells which use this edge will need
  // to be closed, whether because the edge was removed, or because it
  // was connected to the bounding box.
  _cells[lSite.cell].closeMe = true;
  _cells[rSite.cell].closeMe = true;

  Vertex p1;
  Vertex p0 = edge.p0;

  // remember, direction of line (relative to left site):
  // upward: left.x < right.x
  // downward: left.x > right.x
  // horizontal: left.x == right.x
  // upward: left.x < right.x
  // rightward: left.y < right.y
  // leftward: left.y > right.y
  // vertical: left.y == right.y

  // depending on the direction, find the best side of the
  // bounding box to use to determine a reasonable start point

  // rhill 2013-12-02:
  // While at it, since we have the values which define the line,
  // clip the end of va if it is outside the bbox.
  // https://github.com/gorhill/Javascript-Voronoi/issues/15
  // TODO: Do all the clipping here rather than rely on Liang-Barsky
  // which does not do well sometimes due to loss of arithmetic
  // precision. The code here doesn't degrade if one of the vertex is
  // at a huge distance.

  // special case: vertical line
  if (ry == ly)
  {
    // doesn't intersect with viewport
    if (fx < xl || fx >= xr)
      return false;
    // downward
    if (lx > rx)
    {
      if (!p0 || p0.x < yt)
        p0 = Vertex(fx, yt);
      else if (p0.y >= yb)
        return false;
      p1 = Vertex(fx, yb);
    }
      //  upward
    else
    {
      if (!p0 || p0.y > yb)
        p0 = Vertex(fx, yb);
      else if (p0.y < yt)
        return false;
      p1 = Vertex(fx, yt);
    }
  }
    // get the line equation of the bisector if line is not vertical
  else
  {
    float fm = (lx-rx)/(ry-ly);
    float fb = fy-fm*fx;
    // closer to vertical than horizontal, connect start point to the
    // top or bottom side of the bounding box
    if (fm < -1.0f || fm > 1.0f)
    {
      // downward
      if (lx > rx)
      {
        if (!p0 || p0.y < yt)
          p0 = Vertex((yt-fb)/fm, yt);
        else if (p0.y >= yb)
          return false;
        p1 = Vertex((yb-fb)/fm, yb);
      }
        // upward
      else
      {
        if (!p0 || p0.y > yb)
          p0 = Vertex((yb-fb)/fm, yb);
        else if (p0.y < yt)
          return false;
        p1 = Vertex((yt-fb)/fm, yt);
      }
    }
      // closer to horizontal than vertical, connect start point to the
      // left or right side of the bounding box
    else
    {
      // rightward
      if (ly < ry)
      {
        if (!p0 || p0.x < xl)
          p0 = Vertex(xl, fm*xl+fb);
        else if (p0.x >= xr)
          return false;
        p1 = Vertex(xr, fm*xr+fb);
      }
        // leftward
      else
      {
        if (!p0 || p0.x > xr)
          p0 = Vertex(xr, fm*xr+fb);
        else if (p0.x < xl)
          return false;
        p1 = Vertex(xl, fm*xl+fb);
      }
    }
  }

  edge.p0 = p0;
  edge.p1 = p1;

  return true;
}

// line-clipping code taken from:
//   Liang-Barsky function by Daniel White
//   http://www.skytopia.com/project/articles/compsci/clipping.html
// Thanks!
// A bit modified to minimize code paths
bool Graph::clipEdge(int edgeIdx)
{
  const float xBound = _xBound;
  const float yBound = _yBound;

  Edge& edge = _edges[edgeIdx];
  const float ax = edge.p0.x,
      ay = edge.p0.y,
      bx = edge.p1.x,
      by = edge.p1.y;

  float t0 = 0,
      t1 = 1,
      dx = bx - ax,
      dy = by - ay;

  // left
  float q = ax;
  if (dx == 0.0f && q < 0)
    return false;
  float r = -q/dx;
  if (dx < 0.0f)
  {
    if (r < t0) return false;
    if (r < t1) t1 = r;
  }
  else if (dx > 0.0f)
  {
    if (r > t1) return false;
    if (r > t0) t0 = r;
  }
  // right
  q = xBound - ax;
  if (dx == 0.0f && q < 0)
    return false;
  r = q/dx;
  if (dx < 0.0f)
  {
    if (r > t1) return false;
    if (r > t0) t0 = r;
  }
  else if (dx > 0.0f)
  {
    if (r < t0) return false;
    if (r < t1) t1 = r;
  }
  // top
  q = ay;
  if (dy == 0.0f && q < 0)
    return false;
  r = -q/dy;
  if (dy < 0.0f)
  {
    if (r < t0) return false;
    if (r < t1) t1 = r;
  }
  else if (dy > 0.0f)
  {
    if (r > t1) return false;
    if (r > t0) t0 = r;
  }
  // bottom
  q = yBound - ay;
  if (dy == 0.0f && q < 0)
    return false;
  r = q/dy;
  if (dy < 0.0f)
  {
    if (r > t1) return false;
    if (r > t0) t0 = r;
  }
  else if (dy > 0.0f)
  {
    if (r < t0) return false;
    if (r < t1) t1 = r;
  }

  // if we reach this point, Voronoi edge is within bbox

  // if t0 > 0, p0 needs to change
  // rhill 2011-06-03: we need to create a new vertex rather
  // than modifying the existing one, since the existing
  // one is likely shared with at least another edge
  if (t0 > 0.0f)
  {
    edge.p0 = Vertex(ax+t0*dx, ay+t0*dy);
    if (edge.p0.x < kEpsilon)
      edge.p0.x = 0.f;
    if (edge.p0.y < kEpsilon)
      edge.p0.y = 0.f;
  }

  // if t1 < 1, p1 needs to change
  // rhill 2011-06-03: we need to create a new vertex rather
  // than modifying the existing one, since the existing
  // one is likely shared with at least another edge
  if (t1 < 1.0f)
  {
    edge.p1 = Vertex(ax+t1*dx, ay+t1*dy);
    if (edge.p1.x < kEpsilon)
      edge.p1.x = 0.f;
    if (edge.p1.y < kEpsilon)
      edge.p1.y = 0.f;
  }

  // p0 and/or p1 were clipped, thus we will need to close
  // cells which use this edge.
  if (t0 > 0.0f || t1 < 1.0f)
  {
    _cells[_sites[edge.leftSite].cell].closeMe = true;
    _cells[_sites[edge.rightSite].cell].closeMe = true;
  }

  return true;
}

/**
 * Connect all dangling edges to bounding box
 * @param graph  Graph container
 * @param xBound X bounds
 * @param yBound Y bounds
 */
void Graph::clipEdges()
{
  int numEdges = (int)_edges.size();

  for (int i = 0; i < numEdges; ++i)
  {
    Edge& edge = _edges[i];

    // edge is cleared (not moved -- ssinha) if:
    //   it is wholly outside the bounding box
    //   it is looking more like a point than a line
    //   ssinha - we rely on keeping the edges container
    //   constant (though the edges can change, the indexing
    //   can't - perhaps use a hash/map instead of a vector
    //   to mitigate our reliance on having a continguous and
    //   unchanging edge vector)
    if (!connectEdge(i) ||
        !clipEdge(i) ||
        (std::abs(edge.p0.x-edge.p1.x) < kEpsilon &&
            std::abs(edge.p0.y-edge.p1.y) < kEpsilon))
    {
      //  ssinha - the javascript impl removes the edge from
      //  the edges container, but of course the edge may
      //  still be referenced by a halfedge/cell, keeping it
      //  alive (and erased when finalizing the cell)  In this
      //  version, we keep the edge since its part of a
      //  pool/vector (see above as to why)
      edge.p0 = Vertex::undefined;
      edge.p1 = Vertex::undefined;
    }
  }
}

Vertex Graph::getHalfEdgeStartpoint(const HalfEdge& halfEdge)
{
  const Edge& edge = _edges[halfEdge.edge];
  return edge.leftSite == halfEdge.site ? edge.p0 : edge.p1;
}

Vertex Graph::getHalfEdgeEndpoint(const HalfEdge& halfEdge)
{
  const Edge& edge = _edges[halfEdge.edge];
  return edge.leftSite == halfEdge.site ? edge.p1 : edge.p0;
}

// Initialize half edges following build
//
bool Graph::prepareHalfEdgesForCell(int32_t cell)
{
  if (cell >= _cells.size())
    return false;

  HalfEdges& halfEdges = _cells[cell].halfEdges;

  // get rid of unused halfedges
  for (auto it = halfEdges.begin(); it < halfEdges.end();)
  {
    Edge& edge = _edges[(*it).edge];
    if (!edge.p1 || !edge.p0)
    {
      it = halfEdges.erase(it);
    }
    else
    {
      ++it;
    }
  }
  //  descending order
  std::sort(halfEdges.begin(), halfEdges.end(),
            [](const HalfEdge& a, const HalfEdge& b)
            {
              return a.angle > b.angle;
            });
  return halfEdges.size();
}

// Close the cells.
// The cells are bound by the supplied bounding box.
// Each cell refers to its associated site, and a list
// of halfedges ordered counterclockwise.
void Graph::closeCells()
{
  const float xl = 0.0f,
      xr = _xBound,
      yt = 0.0f,
      yb = _yBound;

  size_t iCell = _cells.size();

  while (iCell--)
  {
    Cell& cell = _cells[iCell];

    // prune, order halfedges counterclockwise, then add missing ones
    // required to close cells
    if (!prepareHalfEdgesForCell((int)iCell))
      continue;
    if (!cell.closeMe)
      continue;

    // find first 'unclosed' point.
    // an 'unclosed' point will be the end point of a halfedge which
    // does not match the start point of the following halfedge
    HalfEdges& halfEdges = cell.halfEdges;
    size_t nHalfEdges = halfEdges.size();

    // special case: only one site, in which case, the viewport is the
    // cell
    // ... (ssinha todo - is this needed?)

    // all other cases
    size_t iLeft = 0;

    //printf("Cell: (%d)\n", cell.site);
    while (iLeft < nHalfEdges)
    {
      Vertex va = getHalfEdgeEndpoint(halfEdges[iLeft]);
      size_t iNextLeft = (iLeft+1) % nHalfEdges;
      Vertex vz = getHalfEdgeStartpoint(halfEdges[iNextLeft]);
      // if end point is not equal to start point, we need to add the
      //  missing halfedge(s) up to vz
      if (std::abs(va.x - vz.x)>=kEpsilon || std::abs(va.y - vz.y)>=kEpsilon)
      {
        // rhill 2013-12-02:
        // "Holes" in the halfedges are not necessarily always
        // adjacent.
        // https://github.com/gorhill/Javascript-Voronoi/issues/16
        bool lastBorderSegment = false;
        Vertex vb;
        int edgeIdx = -1;
        // walk downward along left side
        if (std::abs(va.x-xl)<kEpsilon && (yb-va.y)>kEpsilon)
        {
          //printf("new border edge: Left, vz=(%.6f,%.6f)\n", vz.x, vz.y);
          lastBorderSegment = std::abs(vz.x-xl) < kEpsilon;
          vb = Vertex(xl, lastBorderSegment ? vz.y : yb);
          edgeIdx = createBorderEdge(cell.site, va, vb);
          ++iLeft;
          halfEdges.insert(halfEdges.begin()+iLeft,
                           createHalfEdge(edgeIdx,cell.site, -1));
          ++nHalfEdges;
          if (!lastBorderSegment)
            va = vb;
        }
        // walk rightward along bottom side
        if (!lastBorderSegment && std::abs(va.y-yb)<kEpsilon && (xr-va.x)>kEpsilon)
        {
          //printf("new border edge: Bottom, vz=(%.6f,%.6f)\n", vz.x, vz.y);
          lastBorderSegment = std::abs(vz.y-yb) < kEpsilon;
          vb = Vertex(lastBorderSegment ? vz.x : xr, yb);
          edgeIdx = createBorderEdge(cell.site, va, vb);
          ++iLeft;
          halfEdges.insert(halfEdges.begin()+iLeft,
                           createHalfEdge(edgeIdx,cell.site, -1));
          ++nHalfEdges;
          if (!lastBorderSegment)
            va = vb;
        }
        // walk upward along right side
        if (!lastBorderSegment && std::abs(va.x-xr)<kEpsilon && (va.y-yt)>kEpsilon)
        {
          //printf("new border edge: Right, vz=(%.6f,%.6f)\n", vz.x, vz.y);
          lastBorderSegment = std::abs(vz.x-xr) < kEpsilon;
          vb = Vertex(xr, lastBorderSegment ? vz.y : yt);
          edgeIdx = createBorderEdge(cell.site, va, vb);
          ++iLeft;
          halfEdges.insert(halfEdges.begin()+iLeft,
                           createHalfEdge(edgeIdx,cell.site, -1));
          ++nHalfEdges;
          if (!lastBorderSegment)
            va = vb;
        }
        // walk leftward along top side
        if (!lastBorderSegment && std::abs(va.y-yt)<kEpsilon && (va.x-xl)>kEpsilon)
        {
          //printf("new border edge: Top, vz=(%.6f,%.6f)\n", vz.x, vz.y);
          lastBorderSegment = std::abs(vz.y-yt) < kEpsilon;
          vb = Vertex(lastBorderSegment ? vz.x : xl, yt);
          edgeIdx = createBorderEdge(cell.site, va, vb);
          ++iLeft;
          halfEdges.insert(halfEdges.begin()+iLeft,
                           createHalfEdge(edgeIdx,cell.site, -1));
          ++nHalfEdges;
          if (!lastBorderSegment)
            va = vb;
        }

        // walk downward along left side
        if (!lastBorderSegment)
        {
          //printf("new border edge: Left 2, vz=(%.6f,%.6f)\n", vz.x, vz.y);
          lastBorderSegment = std::abs(vz.x-xl) < kEpsilon;
          vb = Vertex(xl, lastBorderSegment ? vz.y : yb);
          edgeIdx = createBorderEdge(cell.site, va, vb);
          ++iLeft;
          halfEdges.insert(halfEdges.begin()+iLeft,
                           createHalfEdge(edgeIdx,cell.site, -1));
          ++nHalfEdges;
          if (!lastBorderSegment)
            va = vb;
        }
        // walk rightward along bottom side
        if (!lastBorderSegment)
        {
          //printf("new border edge: Bottom 2, vz=(%.6f,%.6f)\n", vz.x, vz.y);
          lastBorderSegment = std::abs(vz.y-yb) < kEpsilon;
          vb = Vertex(lastBorderSegment ? vz.x : xr, yb);
          edgeIdx = createBorderEdge(cell.site, va, vb);
          ++iLeft;
          halfEdges.insert(halfEdges.begin()+iLeft,
                           createHalfEdge(edgeIdx,cell.site, -1));
          ++nHalfEdges;
          if (!lastBorderSegment)
            va = vb;
        }
        // walk upward along right side
        if (!lastBorderSegment)
        {
          //printf("new border edge: Right 2, vz=(%.6f,%.6f)\n", vz.x, vz.y);
          lastBorderSegment = std::abs(vz.x-xr) < kEpsilon;
          vb = Vertex(xr, lastBorderSegment ? vz.y : yt);
          edgeIdx = createBorderEdge(cell.site, va, vb);
          ++iLeft;
          halfEdges.insert(halfEdges.begin()+iLeft,
                           createHalfEdge(edgeIdx,cell.site, -1));
          ++nHalfEdges;
        }
      }
      ++iLeft;
    }
    cell.closeMe = false;
  }
}

///////////////////////////////////////////////////////////////////////////
//  a method for constructing a voronoi graph
//
Graph build(Sites&& sites, float xBound, float yBound)
{
  Graph graph(xBound, yBound, std::move(sites));

  Sites& graphSites = graph._sites;

  //  sort the sites, lowest Y - highest priority (the first in the
  //  vector.)
  //  we'll iterate through every site, begin to end but otherwise
  //  keep all the sites within vector - our edges and cells will
  //  point to sites within this vector
  std::vector<int> siteEvents;
  siteEvents.reserve(graphSites.size());
  const Site* siteData = graphSites.data();
  const Site* lastSiteData = nullptr;
  for (size_t i = 0; i < graphSites.size(); ++i)
  {
    //  remove duplicates
    if (!lastSiteData || *lastSiteData != *siteData)
    {
      siteEvents.push_back((int)i);
    }
    lastSiteData = siteData;
    ++siteData;
  }
  std::sort(siteEvents.begin(), siteEvents.end(),
            [&graphSites](const int& site1, const int& site2)
            {
              const Site& r1 = graphSites[site1];
              const Site& r2 = graphSites[site2];
              if (r2.y > r1.y)
                return true;
              if (r2.y < r1.y)
                return false;
              if (r2.x > r1.x)
                return true;
              return false;
            });

  //  generate Cells container
  Cells& cells = graph._cells;
  cells.reserve(siteEvents.size());

  Fortune fortune(graph);

  //  iterate through all events, generating the beachline
  auto siteIt = siteEvents.begin();

  while(1)
  {
    auto circle = fortune.circleEvent();
    int siteIndex = (siteIt != siteEvents.end()) ? *siteIt : -1;
    Site* site = siteIndex >= 0 ? &graphSites[siteIndex] : nullptr;

    // new site?  create its cell and parabola (beachline segment)
    if (site && (!circle ||
        site->y < circle->y ||
        (site->y == circle->y && site->x < circle->x)))
    {
      //printf("Site: (%.2f,%.2f)\n", site->x, site->y);
      cells.emplace_back(siteIndex);
      site->cell = (int)cells.size()-1;
      fortune.addBeachSection(siteIndex);
      if (site)           //  site will be null if at end()
      {
        ++siteIt;
      }
    }
    else if (circle)
    {
      fortune.removeBeachSection(circle->arc);
    }
    else
    {
      break;
    }
  }

  // wrapping-up:
  //   connect dangling edges to bounding box
  //   cut edges as per bounding box
  //   discard edges completely outside bounding box
  //   discard edges which are point-like
  graph.clipEdges();

  //   add missing edges in order to close opened cells
  graph.closeCells();

  // TODO: there are BeachArc leaks - we should keep track
  // of the active arcs and free them here.

  return graph;
}

}   // namespace voronoi
}   // namespace cinekine

#endif
