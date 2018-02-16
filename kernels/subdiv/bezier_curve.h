// ======================================================================== //
// Copyright 2009-2018 Intel Corporation                                    //
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

#include "../common/default.h"

namespace embree
{
  class BezierBasis
  {
  public:

    template<typename T>
      static __forceinline Vec4<T> eval(const T& u) 
    {
      const T t1 = u;
      const T t0 = 1.0f-t1;
      const T B0 = t0 * t0 * t0;
      const T B1 = 3.0f * t1 * (t0 * t0);
      const T B2 = 3.0f * (t1 * t1) * t0;
      const T B3 = t1 * t1 * t1;
      return Vec4<T>(B0,B1,B2,B3);
    }
    
    template<typename T>
      static __forceinline Vec4<T>  derivative(const T& u)
    {
      const T t1 = u;
      const T t0 = 1.0f-t1;
      const T B0 = -(t0*t0);
      const T B1 = madd(-2.0f,t0*t1,t0*t0);
      const T B2 = msub(+2.0f,t0*t1,t1*t1);
      const T B3 = +(t1*t1);
      return T(3.0f)*Vec4<T>(B0,B1,B2,B3);
    }

    template<typename T>
      static __forceinline Vec4<T>  derivative2(const T& u)
    {
      const T t1 = u;
      const T t0 = 1.0f-t1;
      const T B0 = t0;
      const T B1 = madd(-2.0f,t0,t1);
      const T B2 = madd(-2.0f,t1,t0);
      const T B3 = t1;
      return T(6.0f)*Vec4<T>(B0,B1,B2,B3);
    }
  };
  
  struct PrecomputedBezierBasis
  {
    enum { N = 16 };
  public:
    PrecomputedBezierBasis() {}
    PrecomputedBezierBasis(int shift);

    /* basis for bezier evaluation */
  public:
    float c0[N+1][N+1];
    float c1[N+1][N+1];
    float c2[N+1][N+1];
    float c3[N+1][N+1];
    
    /* basis for bezier derivative evaluation */
  public:
    float d0[N+1][N+1];
    float d1[N+1][N+1];
    float d2[N+1][N+1];
    float d3[N+1][N+1];
  };
  extern PrecomputedBezierBasis bezier_basis0;
  extern PrecomputedBezierBasis bezier_basis1;

  template<typename Vertex>
    struct BezierCurveT
    {
      Vertex v0,v1,v2,v3;
      
      __forceinline BezierCurveT() {}
      
      __forceinline BezierCurveT(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3)
        : v0(v0), v1(v1), v2(v2), v3(v3) {}

      __forceinline Vertex begin() const {
        return v0;
      }

      __forceinline Vertex end() const {
        return v3;
      }

      __forceinline Vertex eval(const float t) const 
      {
        const Vec4<float> b = BezierBasis::eval(t);
        return madd(b.x,v0,madd(b.y,v1,madd(b.z,v2,b.w*v3)));
      }
      
      __forceinline Vertex eval_du(const float t) const
      {
        const Vec4<float> b = BezierBasis::derivative(t);
        return madd(b.x,v0,madd(b.y,v1,madd(b.z,v2,b.w*v3)));
      }
      
      __forceinline Vertex eval_dudu(const float t) const 
      {
        const Vec4<float> b = BezierBasis::derivative2(t);
        return madd(b.x,v0,madd(b.y,v1,madd(b.z,v2,b.w*v3)));
      }
      
      __forceinline void eval(const float t, Vertex& p, Vertex& dp, Vertex& ddp) const
      {
        const Vertex p00 = v0;
        const Vertex p01 = v1;
        const Vertex p02 = v2;
        const Vertex p03 = v3;
        const Vertex p10 = lerp(p00,p01,t);
        const Vertex p11 = lerp(p01,p02,t);
        const Vertex p12 = lerp(p02,p03,t);
        const Vertex p20 = lerp(p10,p11,t);
        const Vertex p21 = lerp(p11,p12,t);
        const Vertex p30 = lerp(p20,p21,t);
        p = p30;
        dp = 3.0f*(p21-p20);
        ddp = eval_dudu(t);
      }
      
      template<int M>
      __forceinline void evalN(const vfloat<M>& t, Vec4vf<M>& p, Vec4vf<M>& dp) const
      {
        const Vec4vf<M> p00 = v0;
        const Vec4vf<M> p01 = v1;
        const Vec4vf<M> p02 = v2;
        const Vec4vf<M> p03 = v3;
        
        const Vec4vf<M> p10 = lerp(p00,p01,t);
        const Vec4vf<M> p11 = lerp(p01,p02,t);
        const Vec4vf<M> p12 = lerp(p02,p03,t);
        const Vec4vf<M> p20 = lerp(p10,p11,t);
        const Vec4vf<M> p21 = lerp(p11,p12,t);
        const Vec4vf<M> p30 = lerp(p20,p21,t);
        
        p = p30;
        dp = vfloat<M>(3.0f)*(p21-p20);
      }
      
      template<int M>
      __forceinline Vec4vf<M> eval0(const int ofs, const int size) const
      {
        assert(size <= PrecomputedBezierBasis::N);
        assert(ofs <= size);
        return madd(vfloat<M>::loadu(&bezier_basis0.c0[size][ofs]), Vec4vf<M>(v0),
                    madd(vfloat<M>::loadu(&bezier_basis0.c1[size][ofs]), Vec4vf<M>(v1),
                         madd(vfloat<M>::loadu(&bezier_basis0.c2[size][ofs]), Vec4vf<M>(v2),
                              vfloat<M>::loadu(&bezier_basis0.c3[size][ofs]) * Vec4vf<M>(v3))));
      }
      
      template<int M>
      __forceinline Vec4vf<M> eval1(const int ofs, const int size) const
      {
        assert(size <= PrecomputedBezierBasis::N);
        assert(ofs <= size);
        return madd(vfloat<M>::loadu(&bezier_basis1.c0[size][ofs]), Vec4vf<M>(v0), 
                    madd(vfloat<M>::loadu(&bezier_basis1.c1[size][ofs]), Vec4vf<M>(v1),
                         madd(vfloat<M>::loadu(&bezier_basis1.c2[size][ofs]), Vec4vf<M>(v2),
                              vfloat<M>::loadu(&bezier_basis1.c3[size][ofs]) * Vec4vf<M>(v3))));
      }
      
      template<int M>
      __forceinline Vec4vf<M> derivative0(const int ofs, const int size) const
      {
        assert(size <= PrecomputedBezierBasis::N);
        assert(ofs <= size);
        return madd(vfloat<M>::loadu(&bezier_basis0.d0[size][ofs]), Vec4vf<M>(v0),
                    madd(vfloat<M>::loadu(&bezier_basis0.d1[size][ofs]), Vec4vf<M>(v1),
                         madd(vfloat<M>::loadu(&bezier_basis0.d2[size][ofs]), Vec4vf<M>(v2),
                              vfloat<M>::loadu(&bezier_basis0.d3[size][ofs]) * Vec4vf<M>(v3))));
      }
      
      template<int M>
      __forceinline Vec4vf<M> derivative1(const int ofs, const int size) const
      {
        assert(size <= PrecomputedBezierBasis::N);
        assert(ofs <= size);
        return madd(vfloat<M>::loadu(&bezier_basis1.d0[size][ofs]), Vec4vf<M>(v0),
                    madd(vfloat<M>::loadu(&bezier_basis1.d1[size][ofs]), Vec4vf<M>(v1),
                         madd(vfloat<M>::loadu(&bezier_basis1.d2[size][ofs]), Vec4vf<M>(v2),
                              vfloat<M>::loadu(&bezier_basis1.d3[size][ofs]) * Vec4vf<M>(v3))));
      }
      
      /* calculates bounds of bezier curve geometry */
      __forceinline BBox3fa accurateBounds() const
      {
        const int N = 7;
        const float scale = 1.0f/(3.0f*(N-1));
        Vec4vfx pl(pos_inf), pu(neg_inf);
        for (int i=0; i<=N; i+=VSIZEX)
        {
          vintx vi = vintx(i)+vintx(step);
          vboolx valid = vi <= vintx(N);
          const Vec4vfx p  = eval0<VSIZEX>(i,N);
          const Vec4vfx dp = derivative0<VSIZEX>(i,N);
          const Vec4vfx pm = p-Vec4vfx(scale)*select(vi!=vintx(0),dp,Vec4vfx(zero));
          const Vec4vfx pp = p+Vec4vfx(scale)*select(vi!=vintx(N),dp,Vec4vfx(zero));
          pl = select(valid,min(pl,p,pm,pp),pl); // FIXME: use masked min
          pu = select(valid,max(pu,p,pm,pp),pu); // FIXME: use masked min
        }
        const Vec3fa lower(reduce_min(pl.x),reduce_min(pl.y),reduce_min(pl.z));
        const Vec3fa upper(reduce_max(pu.x),reduce_max(pu.y),reduce_max(pu.z));
        const float r_min = reduce_min(pl.w);
        const float r_max = reduce_max(pu.w);
        const Vec3fa upper_r = Vec3fa(max(abs(r_min),abs(r_max)));
        return enlarge(BBox3fa(lower,upper),upper_r);
      }
      
      /* calculates bounds when tessellated into N line segments */
      __forceinline BBox3fa tessellatedBounds(int N) const
      {
        if (likely(N == 4))
        {
          const Vec4vf4 pi = eval0<4>(0,4);
          const Vec3fa lower(reduce_min(pi.x),reduce_min(pi.y),reduce_min(pi.z));
          const Vec3fa upper(reduce_max(pi.x),reduce_max(pi.y),reduce_max(pi.z));
          const Vec3fa upper_r = Vec3fa(reduce_max(abs(pi.w)));
          return enlarge(BBox3fa(min(lower,v3),max(upper,v3)),max(upper_r,Vec3fa(abs(v3.w))));
        } 
        else
        {
          Vec3vfx pl(pos_inf), pu(neg_inf); vfloatx ru(0.0f);
          for (int i=0; i<N; i+=VSIZEX)
          {
            vboolx valid = vintx(i)+vintx(step) < vintx(N);
            const Vec4vfx pi = eval0<VSIZEX>(i,N);
            
            pl.x = select(valid,min(pl.x,pi.x),pl.x); // FIXME: use masked min
            pl.y = select(valid,min(pl.y,pi.y),pl.y); 
            pl.z = select(valid,min(pl.z,pi.z),pl.z); 
            
            pu.x = select(valid,max(pu.x,pi.x),pu.x); // FIXME: use masked min
            pu.y = select(valid,max(pu.y,pi.y),pu.y); 
            pu.z = select(valid,max(pu.z,pi.z),pu.z); 
            
            ru   = select(valid,max(ru,abs(pi.w)),ru);
          }
          const Vec3fa lower(reduce_min(pl.x),reduce_min(pl.y),reduce_min(pl.z));
          const Vec3fa upper(reduce_max(pu.x),reduce_max(pu.y),reduce_max(pu.z));
          const Vec3fa upper_r(reduce_max(ru));
          return enlarge(BBox3fa(min(lower,v3),max(upper,v3)),max(upper_r,Vec3fa(abs(v3.w))));
        }
      }
      
      friend inline std::ostream& operator<<(std::ostream& cout, const BezierCurveT& curve) {
        return cout << "BezierCurve { v0 = " << curve.v0 << ", v1 = " << curve.v1 << ", v2 = " << curve.v2 << ", v3 = " << curve.v3 << " }";
      }
    };
  
  typedef BezierCurveT<Vec3fa> BezierCurve3fa;
}
