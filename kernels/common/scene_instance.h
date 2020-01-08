// ======================================================================== //
// Copyright 2009-2020 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "geometry.h"
#include "accel.h"

namespace embree
{
  enum TransformationInterpolation
  {
    LINEAR = 0,
    NONLINEAR = 1
  };

  __forceinline AffineSpace3fa quaternionDecompositionToAffineSpace(const AffineSpace3fa& qd)
  {
    // compute affine transform from quaternion decomposition
    Quaternion3f q(qd.l.vx.w, qd.l.vy.w, qd.l.vz.w, qd.p.w);
    AffineSpace3fa M = qd;
    AffineSpace3fa D(one);
    D.p.x = M.l.vx.y;
    D.p.y = M.l.vx.z;
    D.p.z = M.l.vy.z;
    M.l.vx.y = 0;
    M.l.vx.z = 0;
    M.l.vy.z = 0;
    AffineSpace3fa R = LinearSpace3fa(q);
    return D * R * M;
  }


  struct MotionDerivativeCoefficients;

  /*! Instanced acceleration structure */
  struct Instance : public Geometry
  {
    ALIGNED_STRUCT_(16);
    static const Geometry::GTypeMask geom_type = Geometry::MTY_INSTANCE;

  public:
    Instance (Device* device, Accel* object = nullptr, unsigned int numTimeSteps = 1);
    ~Instance();

  private:
    Instance (const Instance& other) DELETED; // do not implement
    Instance& operator= (const Instance& other) DELETED; // do not implement

  private:
    LBBox3fa nonlinearBounds(const BBox1f& time_range_in,
                             const BBox1f& geom_time_range,
                             float geom_time_segments) const;

    BBox3fa boundSegment(size_t itime,
      BBox3fa const& obbox0, BBox3fa const& obbox1,
      BBox3fa const& bbox0, BBox3fa const& bbox1,
      float t_min, float t_max) const;

    /* calculates the (correct) interpolated bounds */
    __forceinline BBox3fa bounds(size_t itime0, size_t itime1, float f) const
    {
      if (unlikely(interpolation == TransformationInterpolation::NONLINEAR))
        return xfmBounds(slerp(local2world[itime0], local2world[itime1], f),
                         lerp(getObjectBounds(itime0), getObjectBounds(itime1), f));
      else
        return xfmBounds(lerp(local2world[itime0], local2world[itime1], f),
                         lerp(getObjectBounds(itime0), getObjectBounds(itime1), f));
    }

  public:
    virtual Geometry* attach(Scene* scene, unsigned int geomID) override;
    virtual void detach() override;
    virtual void setNumTimeSteps (unsigned int numTimeSteps) override;
    virtual void setInstancedScene(const Ref<Scene>& scene) override;
    virtual void setTransform(const AffineSpace3fa& local2world, unsigned int timeStep) override;
    virtual void setQuaternionDecomposition(const AffineSpace3fa& qd, unsigned int timeStep) override;
    virtual AffineSpace3fa getTransform(float time) override;
    virtual void setMask (unsigned mask) override;
    virtual void build() {}
    virtual void preCommit() override;
    virtual void addElementsToCount (GeometryCounts & counts) const override;
    virtual void postCommit() override;
    virtual void commit() override;

  public:

    /* computes the interpolation mode to use by looking at the type of matrices set by the user */
    void updateInterpolationMode();
   
     /*! calculates the bounds of instance */
    __forceinline BBox3fa bounds(size_t i) const {
      assert(i == 0);
      return xfmBounds(local2world[0],object->bounds.bounds());
    }

    /*! gets the bounds of the instanced scene */
    __forceinline BBox3fa getObjectBounds(size_t itime) const {
      return object->getBounds(timeStep(itime));
    }

     /*! calculates the bounds of instance */
    __forceinline BBox3fa bounds(size_t i, size_t itime) const {
      assert(i == 0);
      return xfmBounds(local2world[itime],getObjectBounds(itime));
    }

     /*! calculates the linear bounds at the itimeGlobal'th time segment */
    __forceinline LBBox3fa linearBounds(size_t i, size_t itime) const {
      assert(i == 0);
      return LBBox3fa(bounds(i,itime+0),bounds(i,itime+1));
    }

    /*! calculates the linear bounds of the i'th primitive for the specified time range */
    __forceinline LBBox3fa linearBounds(size_t i, const BBox1f& dt) const {
      assert(i == 0);
      LBBox3fa lbbox = nonlinearBounds(dt, time_range, fnumTimeSegments);
      return lbbox;
    }

    /*! check if the i'th primitive is valid between the specified time range */
    __forceinline bool valid(size_t i, const range<size_t>& itime_range) const
    {
      assert(i == 0);
      for (size_t itime = itime_range.begin(); itime <= itime_range.end(); itime++)
        if (!isvalid(bounds(i,itime))) return false;

      return true;
    }

    __forceinline AffineSpace3fa getLocal2World() const
    {
      if (unlikely(interpolation == TransformationInterpolation::NONLINEAR))
        return quaternionDecompositionToAffineSpace(local2world[0]);
      else
        return local2world[0];
    }

    __forceinline AffineSpace3fa getLocal2World(float t) const
    {
      float ftime; const unsigned int itime = timeSegment(t, ftime);
      if (unlikely(interpolation == TransformationInterpolation::NONLINEAR))
        return slerp(local2world[itime+0],local2world[itime+1],ftime);
      else
        return lerp(local2world[itime+0],local2world[itime+1],ftime);
    }

    __forceinline AffineSpace3fa getWorld2Local() const {
      return world2local0;
    }

    __forceinline AffineSpace3fa getWorld2Local(float t) const {
      return rcp(getLocal2World(t));
    }

    template<int K>
    __forceinline AffineSpace3vf<K> getWorld2Local(const vbool<K>& valid, const vfloat<K>& t) const
    {
      if (unlikely(interpolation == TransformationInterpolation::NONLINEAR))
        return getWorld2LocalSlerp(valid, t);
      else
        return getWorld2LocalLerp(valid, t);
    }

    private:

    template<int K>
    __forceinline AffineSpace3vf<K> getWorld2LocalSlerp(const vbool<K>& valid, const vfloat<K>& t) const
    {
      vfloat<K> ftime;
      const vint<K> itime_k = timeSegment(t, ftime);
      assert(any(valid));
      const size_t index = bsf(movemask(valid));
      const int itime = itime_k[index];
      if (likely(all(valid, itime_k == vint<K>(itime)))) {
        return rcp(slerp(AffineSpace3vfa<K>(local2world[itime+0]),
                         AffineSpace3vfa<K>(local2world[itime+1]),
                         ftime));
      }
      else {
        AffineSpace3vfa<K> space0,space1;
        vbool<K> valid1 = valid;
        while (any(valid1)) {
          vbool<K> valid2;
          const int itime = next_unique(valid1, itime_k, valid2);
          space0 = select(valid2, AffineSpace3vfa<K>(local2world[itime+0]), space0);
          space1 = select(valid2, AffineSpace3vfa<K>(local2world[itime+1]), space1);
        }
        return rcp(slerp(space0, space1, ftime));
      }
    }

    template<int K>
    __forceinline AffineSpace3vf<K> getWorld2LocalLerp(const vbool<K>& valid, const vfloat<K>& t) const
    {
      vfloat<K> ftime;
      const vint<K> itime_k = timeSegment(t, ftime);
      assert(any(valid));
      const size_t index = bsf(movemask(valid));
      const int itime = itime_k[index];
      if (likely(all(valid, itime_k == vint<K>(itime)))) {
        return rcp(lerp(AffineSpace3vf<K>(local2world[itime+0]),
                        AffineSpace3vf<K>(local2world[itime+1]),
                        ftime));
      } else {
        AffineSpace3vf<K> space0,space1;
        vbool<K> valid1 = valid;
        while (any(valid1)) {
          vbool<K> valid2;
          const int itime = next_unique(valid1, itime_k, valid2);
          space0 = select(valid2, AffineSpace3vf<K>(local2world[itime+0]), space0);
          space1 = select(valid2, AffineSpace3vf<K>(local2world[itime+1]), space1);
        }
        return rcp(lerp(space0, space1, ftime));
      }
    }

  public:
    Accel* object;                 //!< pointer to instanced acceleration structure
    AffineSpace3fa* local2world;   //!< transformation from local space to world space for each timestep (either normal matrix or quaternion decomposition)
    AffineSpace3fa world2local0;   //!< transformation from world space to local space for timestep 0
    TransformationInterpolation interpolation;
    MotionDerivativeCoefficients* motionDerivCoeffs; //!< coefficients of motion derivative for each timestep (for non-linear interpolation)
  };

  namespace isa
  {
    struct InstanceISA : public Instance
    {
      InstanceISA (Device* device)
        : Instance(device) {}

      PrimInfo createPrimRefArray(mvector<PrimRef>& prims, const range<size_t>& r, size_t k, unsigned int geomID) const
      {
        assert(r.begin() == 0);
        assert(r.end()   == 1);

        PrimInfo pinfo(empty);
        const BBox3fa b = bounds(0);
        if (!isvalid(b)) return pinfo;

        const PrimRef prim(b,geomID,unsigned(0));
        pinfo.add_center2(prim);
        prims[k++] = prim;
        return pinfo;
      }

      PrimInfo createPrimRefArrayMB(mvector<PrimRef>& prims, size_t itime, const range<size_t>& r, size_t k, unsigned int geomID) const
      {
        assert(r.begin() == 0);
        assert(r.end()   == 1);

        PrimInfo pinfo(empty);
        if (!valid(0,range<size_t>(itime))) return pinfo;
        const PrimRef prim(linearBounds(0,itime).bounds(),geomID,unsigned(0));
        pinfo.add_center2(prim);
        prims[k++] = prim;
        return pinfo;
      }
      
      PrimInfoMB createPrimRefMBArray(mvector<PrimRefMB>& prims, const BBox1f& t0t1, const range<size_t>& r, size_t k, unsigned int geomID) const
      {
        assert(r.begin() == 0);
        assert(r.end()   == 1);

        PrimInfoMB pinfo(empty);
        if (!valid(0, timeSegmentRange(t0t1))) return pinfo;
        const PrimRefMB prim(linearBounds(0,t0t1),this->numTimeSegments(),this->time_range,this->numTimeSegments(),geomID,unsigned(0));
        pinfo.add_primref(prim);
        prims[k++] = prim;
        return pinfo;
      }
    };
  }

  DECLARE_ISA_FUNCTION(Instance*, createInstance, Device*);
}
