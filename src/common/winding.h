#pragma once

#include <cstdint>

#include "mathtypes.h"
#include "win32fix.h"
#include "mathlib.h"
#include "bspfile.h"
#include "boundingbox.h"

constexpr int MAX_POINTS_ON_WINDING = 128;
// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)

constexpr int SIDE_FRONT = 0;
constexpr int SIDE_ON = 2;
constexpr int SIDE_BACK = 1;
constexpr int SIDE_CROSS = -2;

#ifdef SBSP // seedee
#ifndef DOUBLEVEC_T
#error you must add -dDOUBLEVEC_T to the project!
#endif
#define dplane_t windingplane_t

struct dplane_t
{
  vec3_t normal;
  vec3_t unused_origin;
  vec_t dist;
  planetypes type;
};
extern dplane_t g_mapplanes[MAX_INTERNAL_MAP_PLANES];
#endif
class Winding
{
public:
  // General Functions
  void Print() const;
  void getPlane(dplane_t &plane) const;
  void getPlane(vec3_t &normal, vec_t &dist) const;
  auto getArea() const -> vec_t;
  void getBounds(BoundingBox &bounds) const;
  void getBounds(vec3_t &mins, vec3_t &maxs) const;
  void getCenter(vec3_t &center) const;
  auto Copy() const -> Winding *;

  // Specialized Functions
  void RemoveColinearPoints(vec_t epsilon = ON_EPSILON);
  auto Clip(const dplane_t &split, bool keepon, vec_t epsilon = ON_EPSILON) -> bool; // For hlbsp
  void Clip(const vec3_t normal, const vec_t dist, Winding **front, Winding **back, vec_t epsilon = ON_EPSILON);
  auto Chop(const vec3_t normal, const vec_t dist, vec_t epsilon = ON_EPSILON) -> bool;
  void Divide(const dplane_t &split, Winding **front, Winding **back, vec_t epsilon = ON_EPSILON);
  auto WindingOnPlaneSide(const vec3_t normal, const vec_t dist, vec_t epsilon = ON_EPSILON) -> int;

public:
  // Construction
  Winding();                                   // Do nothing :)
  Winding(vec3_t *points, uint32_t numpoints); // Create from raw points
  Winding(const BSPLumpFace &face, vec_t epsilon = ON_EPSILON);
  Winding(const dplane_t &face);
  Winding(const vec3_t normal, const vec_t dist);
  Winding(uint32_t points);
  Winding(const Winding &other);
  virtual ~Winding();
  auto operator=(const Winding &other) -> Winding &;

  // Misc
private:
  void initFromPlane(const vec3_t normal, const vec_t dist);

public:
  // Data
  uint32_t m_NumPoints;
  vec3_t *m_Points;

protected:
  uint32_t m_MaxPoints;
};