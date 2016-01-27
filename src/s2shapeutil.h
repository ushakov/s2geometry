// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Author: ericv@google.com (Eric Veach)
//
// This file contains various subclasses of S2Shape:
//
//  - S2FaceLoopShape (like S2Loop::Shape but more compact and flexible).
//  - S2FaceShape (like S2Polygon::Shape but more compact and flexible).
//  - S2EdgeVectorShape (for indexing random collections of edges).
//  - S2LoopOwningShape (like S2SLoop::Shape but owns the loop).
//  - S2PolygonOwningShape (like S2SPolygon::Shape but owns the polygon).
//  - S2PolylineOwningShape (like S2SPolyline::Shape but owns the polyline).
//
// and various functions related to S2ShapeIndex:
//
//  - IsOriginOnLeft (helper for implementing S2Shape::contains_origin).
//  - FindSelfIntersection (helper function for S2Loop validation).
//  - FindAnyCrossing (helper function for S2Polygon validation).

#ifndef S2GEOMETRY_S2SHAPEUTIL_H_
#define S2GEOMETRY_S2SHAPEUTIL_H_

#include <utility>
#include <vector>
#include "fpcontractoff.h"
#include "s2.h"
#include "s2loop.h"
#include "s2polygon.h"
#include "s2polyline.h"
#include "s2shapeindex.h"

class S2Loop;
class S2Error;

namespace s2shapeutil {

/////////////////////////////////////////////////////////////////////////////
///////////////////////////  Utility Shape Types  ///////////////////////////

// FaceLoopShape represents a closed loop of edges surrounding an interior
// region.  It is similar to S2Loop::Shape except that this class allows
// duplicate vertices and edges.  Loops may have any number of vertices,
// including 0, 1, or 2.  (A one-vertex loop defines a degenerate edge
// consisting of a single point.)
//
// Note that FaceLoopShape is faster to initialize and more compact than
// S2Loop::Shape, but does not support the same operations as S2Loop.
class FaceLoopShape : public S2Shape {
 public:
  FaceLoopShape() {}  // Must call Init().
  explicit FaceLoopShape(std::vector<S2Point> const& vertices);
  explicit FaceLoopShape(S2Loop const& loop);
  ~FaceLoopShape();

  // Initialize the shape.
  void Init(std::vector<S2Point> const& vertices);

  // Initialize from an S2Loop.  Does not take ownership of "loop".
  // REQUIRES: !loop->is_full()
  void Init(S2Loop const& loop);

  int num_vertices() const { return num_vertices_; }
  S2Point const& vertex(int i) const { return vertices_[i]; }

  // S2Shape interface:
  int num_edges() const { return num_vertices(); }
  void GetEdge(int e, S2Point const** a, S2Point const** b) const;
  bool has_interior() const { return true; }
  bool contains_origin() const;

 private:
  // For clients that have many small loops, we save some memory by
  // representing the vertices as an array rather than using std::vector.
  int32 num_vertices_;
  std::unique_ptr<S2Point[]> vertices_;
};

// FaceShape represents a region defined by a collection of closed loops.  The
// interior is the region to the left of all loops.  This is similar to
// S2Polygon::Shape except that this class allows duplicate vertices and edges
// (within loops and/or shared between loops).  There can be zero or more
// loops, and each loop must have at least one vertex.  (A one-vertex loop
// defines a degenerate edge consisting of a single point.)
//
// Note that FaceShape is faster to initialize and more compact than
// S2Polygon::Shape, but does not support the same operations as S2Polygon.
class FaceShape : public S2Shape {
 public:
  FaceShape() {}  // Must call Init().
  typedef std::vector<S2Point> Loop;
  explicit FaceShape(std::vector<Loop> const& loops);
  explicit FaceShape(S2Polygon const& polygon);
  ~FaceShape();

  // Initialize the shape.
  void Init(std::vector<Loop> const& loops);

  // Initialize from an S2Polygon.  Does not take ownership of "polygon".
  // REQUIRES: !polygon->is_full()
  void Init(S2Polygon const& polygon);

  // Return the number of loops.
  int num_loops() const { return num_loops_; }

  // Return the total number of vertices in all loops.
  int num_vertices() const;

  // Return the number of vertices in the given loop.
  int num_loop_vertices(int i) const;

  // Return the vertex from loop "i" at index "j".
  // REQUIRES: 0 <= i < num_loops()
  // REQUIRES: 0 <= j < num_loop_vertices(i)
  S2Point const& loop_vertex(int i, int j) const;

  // S2Shape interface:
  int num_edges() const { return num_vertices(); }
  void GetEdge(int e, S2Point const** a, S2Point const** b) const;
  bool has_interior() const { return true; }
  bool contains_origin() const;

 private:
  class VertexArray;
  void Init(std::vector<VertexArray> const& loops);

  int32 num_loops_;
  std::unique_ptr<S2Point[]> vertices_;
  // If num_loops_ <= 1, this union stores the number of vertices.
  // Otherwise it points to an array of size (num_loops + 1) where element "i"
  // is the total number of vertices in loops 0..i-1.
  union {
    int32 num_vertices_;
    int32* cumulative_vertices_;  // Don't use unique_ptr in unions.
  };
};

// S2EdgeVectorShape is an S2Shape representing a set of unrelated edges.
// It is mainly used for testing, but it can also be useful if you have, say,
// a collection of polylines and don't care about memory efficiency (since
// this class would store most of the vertices twice).
//
// Note that if you already have data stored in an S2Loop, S2Polyline, or
// S2Polygon, then you would be better off using the "Shape" class defined
// within those classes (e.g., S2Loop::Shape).  Similarly, if the vertex data
// is stored in your own data structures, you can easily write your own
// subclass of S2Shape that points to the existing vertex data rather than
// copying it.
//
// When an object of this class is inserted into an S2ShapeIndex, the index
// takes ownership.  The object and edge data will be deleted automatically
// when the index no longer needs it.  You can subtype this class to change
// this behavior.
class S2EdgeVectorShape : public S2Shape {
 public:
  S2EdgeVectorShape() {}
  // Convenience constructor for creating a vector of length 1.
  S2EdgeVectorShape(S2Point const& a, S2Point const& b) {
    edges_.push_back(std::make_pair(a, b));
  }
  // Add an edge to the vector.
  void Add(S2Point const& a, S2Point const& b) {
    edges_.push_back(std::make_pair(a, b));
  }

  // S2Shape interface:
  int num_edges() const { return edges_.size(); }
  void GetEdge(int e, S2Point const** a, S2Point const** b) const {
    *a = &edges_[e].first;
    *b = &edges_[e].second;
  }
  bool has_interior() const { return false; }
  bool contains_origin() const { return false; }

 private:
  std::vector<std::pair<S2Point, S2Point>> edges_;
};

// Like S2Loop::Shape, except that the referenced S2Loop is automatically
// deleted when this object is released by the S2ShapeIndex.  This is useful
// when an S2Loop is constructed solely for the purpose of indexing it.
class S2LoopOwningShape : public S2Loop::Shape {
 public:
  S2LoopOwningShape() {}  // Must call Init().
  explicit S2LoopOwningShape(S2Loop const* loop)
      : S2Loop::Shape(loop) {
  }
  ~S2LoopOwningShape() {
    delete loop();
  }
};

// Like S2Polygon::Shape, except that the referenced S2Polygon is
// automatically deleted when this object is released by the S2ShapeIndex.
// This is useful when an S2Polygon is constructed solely for the purpose of
// indexing it.
class S2PolygonOwningShape : public S2Polygon::Shape {
 public:
  S2PolygonOwningShape() {}  // Must call Init().
  explicit S2PolygonOwningShape(S2Polygon const* polygon)
      : S2Polygon::Shape(polygon) {
  }
  ~S2PolygonOwningShape() {
    delete polygon();
  }
};

// Like S2Polyline::Shape, except that the referenced S2Polyline is
// automatically deleted when this object is released by the S2ShapeIndex.
// This is useful when an S2Polyline is constructed solely for the purpose of
// indexing it.
class S2PolylineOwningShape : public S2Polyline::Shape {
 public:
  S2PolylineOwningShape() {}  // Must call Init().
  explicit S2PolylineOwningShape(S2Polyline const* polyline)
      : S2Polyline::Shape(polyline) {
  }
  ~S2PolylineOwningShape() {
    delete polyline();
  }
};


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////// Utility Functions ///////////////////////////


// ShapeEdgeId is a unique identifier for an edge within an S2ShapeIndex,
// consisting of a (shape_id, edge_id) pair.  It is similar to
// std::pair<int32, int32> except that it has named fields.
class ShapeEdgeId {
 public:
  ShapeEdgeId(int32 shape_id, int32 edge_id);
  int32 shape_id() const { return shape_id_; }
  int32 edge_id() const { return edge_id_; }
  bool operator==(ShapeEdgeId other) const;
  bool operator<(ShapeEdgeId other) const;

 private:
  int32 shape_id_;
  int32 edge_id_;
};
std::ostream& operator<<(std::ostream& os, ShapeEdgeId id);

// A set of edge pairs within an S2ShapeIndex.
using EdgePairList = std::vector<std::pair<ShapeEdgeId, ShapeEdgeId>>;

// A parameter that controls the reporting of edge intersections.
//
//  - CrossingType::INTERIOR says to report only intersections that occur at
//    a point interior to both edges (i.e., not at a vertex).
//
//  - CrossingType::ALL says to report all intersections, even those where
//    two edges intersect only because they share a common vertex.
enum class CrossingType { INTERIOR, ALL };

// Return all pairs of intersecting edges in the given S2ShapeIndex.  If
// "type" is CrossingType::INTERIOR, then only intersections at a point
// interior to both edges are reported, while if "type" is CrossingType::ALL
// then edges that share a vertex are also reported.
//
// The result is sorted in lexicographic order.  Returns true if any
// crossing edge pairs were found.
bool GetCrossingEdgePairs(S2ShapeIndex const& index, CrossingType type,
                          EdgePairList* edge_pairs);

// This is a helper function for implementing S2Shape::contains_origin().
//
// Given a shape consisting of closed polygonal loops, define the interior of
// the shape to be the region to the left of all edges (which must be oriented
// consistently).  This function then returns true if S2::Origin() is
// contained by the shape.
//
// Unlike S2Loop and S2Polygon, this method allows duplicate vertices and
// edges, which requires some extra care with definitions.  The rule that we
// apply is that an edge and its reverse edge "cancel" each other: the result
// is the same as if that edge pair were not present.  Therefore shapes that
// consist only of degenerate loop(s) are considered to be empty.
//
// Determining whether a loop on the sphere contains a point is harder than
// the corresponding problem in 2D plane geometry.  It cannot be implemented
// just by counting edge crossings because there is no such thing as a "point
// at infinity" that is guaranteed to be outside the loop.
bool IsOriginOnLeft(S2Shape const& shape);


/////////////////////////////////////////////////////////////////////////////
///////////////// Methods used internally by the S2 library /////////////////


// The purpose of this function is to construct polygons consisting of
// multiple loops.  It takes as input a collection of loops whose boundaries
// do not cross, and groups them into polygons whose interiors do not
// intersect (where the boundary of each polygon may consists of multiple
// loops).
//
// some of those islands have lakes, then the input to this function would
// islands, and their lakes.  Each loop would actually be present twice, once
// in each direction (see below).  The output would consist of one polygon
// representing each lake, one polygon representing each island not including
// islands or their lakes, and one polygon representing the rest of the world
//
// This method is intended for internal use; external clients should use
// S2Builder, which has more convenient interface.
//
// The input consists of a set of connected components, where each component
// consists of one or more loops that satisfy the following properties:
//
//  - The loops in each component must form a subdivision of the sphere (i.e.,
//    they must cover the entire sphere without overlap), except that a
//    component may consist of a single loop if and only if that loop is
//    degenerate (i.e., its interior is empty).
//
//  - The boundaries of different connected components must be disjoint
//    (i.e. no crossing edges or shared vertices).
//
//  - No component should be empty, and no loop should have zero edges.
//
// The output consists of a set of polygons, where each polygon is defined by a
// collection of loops that form its boundary.  This function does not
// actually construct any S2Shapes; it simply identifies the loops that belong
// to each polygon.
void ResolveLoopContainment(
    std::vector<std::vector<S2Shape*>> const& components,
    std::vector<std::vector<S2Shape*>>* polygons);

// Given an S2ShapeIndex containing a single loop, return true if the loop has
// a self-intersection (including duplicate vertices) and set "error" to a
// human-readable error message.  Otherwise return false and leave "error"
// unchanged.
bool FindSelfIntersection(S2ShapeIndex const& index, S2Loop const& loop,
                          S2Error* error);

// Given an S2ShapeIndex containing a set of loops, return true if any loop
// has a self-intersection (including duplicate vertices) or crosses any other
// loop (including vertex crossings and duplicate edges) and set "error" to a
// human-readable error message.  Otherwise return false and leave "error"
// unchanged.
bool FindAnyCrossing(S2ShapeIndex const& index,
                     std::vector<S2Loop*> const& loops,
                     S2Error* error);


//////////////////   Implementation details follow   ////////////////////


inline ShapeEdgeId::ShapeEdgeId(int32 shape_id, int32 edge_id)
    : shape_id_(shape_id), edge_id_(edge_id) {
}

inline bool ShapeEdgeId::operator==(ShapeEdgeId other) const {
  return shape_id_ == other.shape_id_ && edge_id_ == other.edge_id_;
}

inline bool ShapeEdgeId::operator<(ShapeEdgeId other) const {
  if (shape_id_ < other.shape_id_) return true;
  if (other.shape_id_ < shape_id_) return false;
  return edge_id_ < other.edge_id_;
}

}  // namespace s2shapeutil

#endif  // S2GEOMETRY_S2SHAPEUTIL_H_
