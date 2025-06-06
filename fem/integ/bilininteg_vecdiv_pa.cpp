// Copyright (c) 2010-2025, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "../../general/forall.hpp"
#include "../bilininteg.hpp"
#include "../gridfunc.hpp"

namespace mfem
{

// PA Divergence Assemble 2D kernel
static void PADivergenceSetup2D(const int Q1D,
                                const int NE,
                                const Array<real_t> &w,
                                const Vector &j,
                                const real_t COEFF,
                                Vector &op)
{
   const int NQ = Q1D*Q1D;
   auto W = w.Read();
   auto J = Reshape(j.Read(), NQ, 2, 2, NE);
   auto y = Reshape(op.Write(), NQ, 2, 2, NE);

   mfem::forall(NE, [=] MFEM_HOST_DEVICE (int e)
   {
      for (int q = 0; q < NQ; ++q)
      {
         const real_t J11 = J(q,0,0,e);
         const real_t J12 = J(q,0,1,e);
         const real_t J21 = J(q,1,0,e);
         const real_t J22 = J(q,1,1,e);
         // Store wq * Q * adj(J)
         y(q,0,0,e) = W[q] * COEFF *  J22; // 1,1
         y(q,0,1,e) = W[q] * COEFF * -J12; // 1,2
         y(q,1,0,e) = W[q] * COEFF * -J21; // 2,1
         y(q,1,1,e) = W[q] * COEFF *  J11; // 2,2
      }
   });
}

// PA Divergence Assemble 3D kernel
static void PADivergenceSetup3D(const int Q1D,
                                const int NE,
                                const Array<real_t> &w,
                                const Vector &j,
                                const real_t COEFF,
                                Vector &op)
{
   const int NQ = Q1D*Q1D*Q1D;
   auto W = w.Read();
   auto J = Reshape(j.Read(), NQ, 3, 3, NE);
   auto y = Reshape(op.Write(), NQ, 3, 3, NE);
   mfem::forall(NE, [=] MFEM_HOST_DEVICE (int e)
   {
      for (int q = 0; q < NQ; ++q)
      {
         const real_t J11 = J(q,0,0,e);
         const real_t J21 = J(q,1,0,e);
         const real_t J31 = J(q,2,0,e);
         const real_t J12 = J(q,0,1,e);
         const real_t J22 = J(q,1,1,e);
         const real_t J32 = J(q,2,1,e);
         const real_t J13 = J(q,0,2,e);
         const real_t J23 = J(q,1,2,e);
         const real_t J33 = J(q,2,2,e);
         const real_t cw  = W[q] * COEFF;
         // adj(J)
         const real_t A11 = (J22 * J33) - (J23 * J32);
         const real_t A12 = (J32 * J13) - (J12 * J33);
         const real_t A13 = (J12 * J23) - (J22 * J13);
         const real_t A21 = (J31 * J23) - (J21 * J33);
         const real_t A22 = (J11 * J33) - (J13 * J31);
         const real_t A23 = (J21 * J13) - (J11 * J23);
         const real_t A31 = (J21 * J32) - (J31 * J22);
         const real_t A32 = (J31 * J12) - (J11 * J32);
         const real_t A33 = (J11 * J22) - (J12 * J21);
         // Store wq * Q * adj(J)
         y(q,0,0,e) = cw * A11; // 1,1
         y(q,0,1,e) = cw * A12; // 1,2
         y(q,0,2,e) = cw * A13; // 1,3
         y(q,1,0,e) = cw * A21; // 2,1
         y(q,1,1,e) = cw * A22; // 2,2
         y(q,1,2,e) = cw * A23; // 2,3
         y(q,2,0,e) = cw * A31; // 3,1
         y(q,2,1,e) = cw * A32; // 3,2
         y(q,2,2,e) = cw * A33; // 3,3
      }
   });
}

static void PADivergenceSetup(const int dim,
                              const int TR_D1D,
                              const int TE_D1D,
                              const int Q1D,
                              const int NE,
                              const Array<real_t> &W,
                              const Vector &J,
                              const real_t COEFF,
                              Vector &op)
{
   if (dim == 1) { MFEM_ABORT("dim==1 not supported in PADivergenceSetup"); }
   if (dim == 2)
   {
      PADivergenceSetup2D(Q1D, NE, W, J, COEFF, op);
   }
   if (dim == 3)
   {
      PADivergenceSetup3D(Q1D, NE, W, J, COEFF, op);
   }
}

void VectorDivergenceIntegrator::AssemblePA(const FiniteElementSpace &trial_fes,
                                            const FiniteElementSpace &test_fes)
{
   // Assumes tensor-product elements ordered by nodes
   MFEM_ASSERT(trial_fes.GetOrdering() == Ordering::byNODES,
               "PA Only supports Ordering::byNODES!");
   Mesh *mesh = trial_fes.GetMesh();
   const FiniteElement &trial_fe = *trial_fes.GetTypicalFE();
   const FiniteElement &test_fe = *test_fes.GetTypicalFE();
   ElementTransformation *trans = mesh->GetTypicalElementTransformation();
   const IntegrationRule *ir = IntRule ? IntRule : &GetRule(trial_fe, test_fe,
                                                            *trans);
   const int dims = trial_fe.GetDim();
   const int dimsToStore = dims * dims;
   nq = ir->GetNPoints();
   dim = mesh->Dimension();
   ne = trial_fes.GetNE();
   geom = mesh->GetGeometricFactors(*ir, GeometricFactors::JACOBIANS);
   trial_maps = &trial_fe.GetDofToQuad(*ir, DofToQuad::TENSOR);
   trial_dofs1D = trial_maps->ndof;
   quad1D = trial_maps->nqpt;
   test_maps  = &test_fe.GetDofToQuad(*ir, DofToQuad::TENSOR);
   test_dofs1D = test_maps->ndof;
   MFEM_ASSERT(quad1D == test_maps->nqpt,
               "PA requires test and trial space to have same number of quadrature points!");
   pa_data.SetSize(nq * dimsToStore * ne, Device::GetMemoryType());
   real_t coeff = 1.0;
   if (Q)
   {
      ConstantCoefficient *cQ = dynamic_cast<ConstantCoefficient*>(Q);
      MFEM_VERIFY(cQ != NULL, "only ConstantCoefficient is supported!");
      coeff = cQ->constant;
   }
   PADivergenceSetup(dim, trial_dofs1D, test_dofs1D, quad1D,
                     ne, ir->GetWeights(), geom->J, coeff, pa_data);
}

// PA Divergence Apply 2D kernel
template<const int T_TR_D1D = 0, const int T_TE_D1D = 0, const int T_Q1D = 0>
static void PADivergenceApply2D(const int NE,
                                const Array<real_t> &b,
                                const Array<real_t> &g,
                                const Array<real_t> &bt,
                                const Vector &op_,
                                const Vector &x_,
                                Vector &y_,
                                const int tr_d1d = 0,
                                const int te_d1d = 0,
                                const int q1d = 0)
{
   const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
   const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   MFEM_VERIFY(TR_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(TE_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(Q1D <= DeviceDofQuadLimits::Get().MAX_Q1D, "");
   auto B = Reshape(b.Read(), Q1D, TR_D1D);
   auto G = Reshape(g.Read(), Q1D, TR_D1D);
   auto Bt = Reshape(bt.Read(), TE_D1D, Q1D);
   auto op = Reshape(op_.Read(), Q1D*Q1D, 2,2, NE);
   auto x = Reshape(x_.Read(), TR_D1D, TR_D1D, 2, NE);
   auto y = Reshape(y_.ReadWrite(), TE_D1D, TE_D1D, NE);
   mfem::forall(NE, [=] MFEM_HOST_DEVICE (int e)
   {
      const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
      const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      const int VDIM = 2;
      // the following variables are evaluated at compile time
      constexpr int max_TE_D1D = T_TE_D1D ? T_TE_D1D : DofQuadLimits::MAX_D1D;
      constexpr int max_Q1D = T_Q1D ? T_Q1D : DofQuadLimits::MAX_Q1D;

      real_t grad[max_Q1D][max_Q1D][VDIM];
      real_t div[max_Q1D][max_Q1D];
      for (int qy = 0; qy < Q1D; ++qy)
      {
         for (int qx = 0; qx < Q1D; ++qx)
         {
            div[qy][qx] = 0.0;
         }
      }

      for (int c = 0; c < VDIM; ++c)
      {
         for (int qy = 0; qy < Q1D; ++qy)
         {
            for (int qx = 0; qx < Q1D; ++qx)
            {
               grad[qy][qx][0] = 0.0;
               grad[qy][qx][1] = 0.0;
            }
         }
         for (int dy = 0; dy < TR_D1D; ++dy)
         {
            real_t gradX[max_Q1D][VDIM];
            for (int qx = 0; qx < Q1D; ++qx)
            {
               gradX[qx][0] = 0.0;
               gradX[qx][1] = 0.0;
            }
            for (int dx = 0; dx < TR_D1D; ++dx)
            {
               const real_t s = x(dx,dy,c,e);
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  gradX[qx][0] += s * G(qx,dx);
                  gradX[qx][1] += s * B(qx,dx);
               }
            }
            for (int qy = 0; qy < Q1D; ++qy)
            {
               const real_t wy  = B(qy,dy);
               const real_t wDy = G(qy,dy);
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  grad[qy][qx][0] += gradX[qx][0] * wy;
                  grad[qy][qx][1] += gradX[qx][1] * wDy;
               }
            }
         }
         // We've now calculated grad(u_c) = [Dxy_1, xDy_2] in plane
         for (int qy = 0; qy < Q1D; ++qy)
         {
            for (int qx = 0; qx < Q1D; ++qx)
            {
               const int q = qx + qy * Q1D;
               const real_t gradX = grad[qy][qx][0];
               const real_t gradY = grad[qy][qx][1];

               div[qy][qx] += gradX*op(q,0,c,e) + gradY*op(q,1,c,e);
            }
         }
      }
      // We've now calculated div = reshape(div phi * op) * u
      for (int qy = 0; qy < Q1D; ++qy)
      {
         real_t opX[max_TE_D1D];
         for (int dx = 0; dx < TE_D1D; ++dx)
         {
            opX[dx] = 0.0;
            for (int qx = 0; qx < Q1D; ++qx)
            {
               opX[dx] += Bt(dx,qx)*div[qy][qx];
            }
         }
         for (int dy = 0; dy < TE_D1D; ++dy)
         {
            for (int dx = 0; dx < TE_D1D; ++dx)
            {
               y(dx,dy,e) += Bt(dy,qy)*opX[dx];
            }
         }
      }
      // We've now calculated y = p * div
   });
}

// Shared memory PA Divergence Apply 2D kernel
template<const int T_TR_D1D = 0, const int T_TE_D1D = 0, const int T_Q1D = 0,
         const int T_NBZ = 0>
static void SmemPADivergenceApply2D(const int NE,
                                    const Array<real_t> &b_,
                                    const Array<real_t> &g_,
                                    const Array<real_t> &bt_,
                                    const Vector &op_,
                                    const Vector &x_,
                                    Vector &y_,
                                    const int tr_d1d = 0,
                                    const int te_d1d = 0,
                                    const int q1d = 0)
{
   // TODO
   MFEM_ASSERT(false, "SHARED MEM NOT PROGRAMMED YET");
}

// PA Divergence Apply 2D kernel transpose
template<const int T_TR_D1D = 0, const int T_TE_D1D = 0, const int T_Q1D = 0>
static void PADivergenceApplyTranspose2D(const int NE,
                                         const Array<real_t> &bt,
                                         const Array<real_t> &gt,
                                         const Array<real_t> &b,
                                         const Vector &op_,
                                         const Vector &x_,
                                         Vector &y_,
                                         const int tr_d1d = 0,
                                         const int te_d1d = 0,
                                         const int q1d = 0)
{
   const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
   const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   MFEM_VERIFY(TR_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(TE_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(Q1D <= DeviceDofQuadLimits::Get().MAX_Q1D, "");
   auto Bt = Reshape(bt.Read(), TR_D1D, Q1D);
   auto Gt = Reshape(gt.Read(), TR_D1D, Q1D);
   auto B  = Reshape(b.Read(), Q1D, TE_D1D);
   auto op = Reshape(op_.Read(), Q1D*Q1D, 2,2, NE);
   auto x  = Reshape(x_.Read(), TE_D1D, TE_D1D, NE);
   auto y  = Reshape(y_.ReadWrite(), TR_D1D, TR_D1D, 2, NE);
   mfem::forall(NE, [=] MFEM_HOST_DEVICE (int e)
   {
      const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
      const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      const int VDIM = 2;
      // the following variables are evaluated at compile time
      constexpr int max_TR_D1D = T_TR_D1D ? T_TR_D1D : DofQuadLimits::MAX_D1D;
      constexpr int max_Q1D = T_Q1D ? T_Q1D : DofQuadLimits::MAX_Q1D;

      real_t quadTest[max_Q1D][max_Q1D];
      real_t grad[max_Q1D][max_Q1D][VDIM];
      for (int qy = 0; qy < Q1D; ++qy)
      {
         for (int qx = 0; qx < Q1D; ++qx)
         {
            quadTest[qy][qx] = 0.0;
         }
      }
      for (int dy = 0; dy < TE_D1D; ++dy)
      {
         real_t quadTestX[max_Q1D];
         for (int qx = 0; qx < Q1D; ++qx)
         {
            quadTestX[qx] = 0.0;
         }
         for (int dx = 0; dx < TE_D1D; ++dx)
         {
            const real_t s = x(dx,dy,e);
            for (int qx = 0; qx < Q1D; ++qx)
            {
               quadTestX[qx] += s * B(qx,dx);
            }
         }
         for (int qy = 0; qy < Q1D; ++qy)
         {
            const real_t wy = B(qy,dy);
            for (int qx = 0; qx < Q1D; ++qx)
            {
               quadTest[qy][qx] += quadTestX[qx] * wy;
            }
         }
      }
      // We've now calculated x on the quads
      for (int c = 0; c < VDIM; ++c)
      {
         for (int qy = 0; qy < Q1D; ++qy)
         {
            for (int qx = 0; qx < Q1D; ++qx)
            {
               const int q = qx + qy * Q1D;
               grad[qy][qx][0] = quadTest[qy][qx]*op(q,0,c,e);
               grad[qy][qx][1] = quadTest[qy][qx]*op(q,1,c,e);
            }
         }
         // We've now calculated op_c^T * x
         for (int qy = 0; qy < Q1D; ++qy)
         {
            real_t gradX[max_TR_D1D][VDIM];
            for (int dx = 0; dx < TR_D1D; ++dx)
            {
               gradX[dx][0] = 0.0;
               gradX[dx][1] = 0.0;
            }
            for (int qx = 0; qx < Q1D; ++qx)
            {
               const real_t gX = grad[qy][qx][0];
               const real_t gY = grad[qy][qx][1];
               for (int dx = 0; dx < TR_D1D; ++dx)
               {
                  const real_t wx  = Bt(dx,qx);
                  const real_t wDx = Gt(dx,qx);
                  gradX[dx][0] += gX * wDx;
                  gradX[dx][1] += gY * wx;
               }
            }
            for (int dy = 0; dy < TR_D1D; ++dy)
            {
               const real_t wy  = Bt(dy,qy);
               const real_t wDy = Gt(dy,qy);
               for (int dx = 0; dx < TR_D1D; ++dx)
               {
                  y(dx,dy,c,e) += ((gradX[dx][0] * wy) + (gradX[dx][1] * wDy));
               }
            }
         }
      }
      // We've now calculated y = reshape(div u * op^T) * x
   });
}

// PA Vector Divergence Apply 3D kernel
template<const int T_TR_D1D = 0, const int T_TE_D1D = 0, const int T_Q1D = 0>
static void PADivergenceApply3D(const int NE,
                                const Array<real_t> &b,
                                const Array<real_t> &g,
                                const Array<real_t> &bt,
                                const Vector &op_,
                                const Vector &x_,
                                Vector &y_,
                                int tr_d1d = 0,
                                int te_d1d = 0,
                                int q1d = 0)
{
   const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
   const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   MFEM_VERIFY(TR_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(TE_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(Q1D <= DeviceDofQuadLimits::Get().MAX_Q1D, "");
   auto B = Reshape(b.Read(), Q1D, TR_D1D);
   auto G = Reshape(g.Read(), Q1D, TR_D1D);
   auto Bt = Reshape(bt.Read(), TE_D1D, Q1D);
   auto op = Reshape(op_.Read(), Q1D*Q1D*Q1D, 3,3, NE);
   auto x = Reshape(x_.Read(), TR_D1D, TR_D1D, TR_D1D, 3, NE);
   auto y = Reshape(y_.ReadWrite(), TE_D1D, TE_D1D, TE_D1D, NE);
   mfem::forall(NE, [=] MFEM_HOST_DEVICE (int e)
   {
      const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
      const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      const int VDIM = 3;
      // the following variables are evaluated at compile time
      constexpr int max_TE_D1D = T_TE_D1D ? T_TE_D1D : DofQuadLimits::MAX_D1D;
      constexpr int max_Q1D = T_Q1D ? T_Q1D : DofQuadLimits::MAX_Q1D;

      real_t grad[max_Q1D][max_Q1D][max_Q1D][VDIM];
      real_t div[max_Q1D][max_Q1D][max_Q1D];
      for (int qz = 0; qz < Q1D; ++qz)
      {
         for (int qy = 0; qy < Q1D; ++qy)
         {
            for (int qx = 0; qx < Q1D; ++qx)
            {
               div[qz][qy][qx] = 0.0;
            }
         }
      }

      for (int c = 0; c < VDIM; ++c)
      {
         for (int qz = 0; qz < Q1D; ++qz)
         {
            for (int qy = 0; qy < Q1D; ++qy)
            {
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  grad[qz][qy][qx][0] = 0.0;
                  grad[qz][qy][qx][1] = 0.0;
                  grad[qz][qy][qx][2] = 0.0;
               }
            }
         }
         for (int dz = 0; dz < TR_D1D; ++dz)
         {
            real_t gradXY[max_Q1D][max_Q1D][VDIM];
            for (int qy = 0; qy < Q1D; ++qy)
            {
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  gradXY[qy][qx][0] = 0.0;
                  gradXY[qy][qx][1] = 0.0;
                  gradXY[qy][qx][2] = 0.0;
               }
            }
            for (int dy = 0; dy < TR_D1D; ++dy)
            {
               real_t gradX[max_Q1D][VDIM];
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  gradX[qx][0] = 0.0;
                  gradX[qx][1] = 0.0;
                  gradX[qx][2] = 0.0;
               }
               for (int dx = 0; dx < TR_D1D; ++dx)
               {
                  const real_t s = x(dx,dy,dz,c,e);
                  for (int qx = 0; qx < Q1D; ++qx)
                  {
                     gradX[qx][0] += s * G(qx,dx);
                     gradX[qx][1] += s * B(qx,dx);
                     gradX[qx][2] += s * B(qx,dx);
                  }
               }
               for (int qy = 0; qy < Q1D; ++qy)
               {
                  const real_t wy  = B(qy,dy);
                  const real_t wDy = G(qy,dy);
                  for (int qx = 0; qx < Q1D; ++qx)
                  {
                     gradXY[qy][qx][0] += gradX[qx][0] * wy;
                     gradXY[qy][qx][1] += gradX[qx][1] * wDy;
                     gradXY[qy][qx][2] += gradX[qx][2] * wy;
                  }
               }
            }
            for (int qz = 0; qz < Q1D; ++qz)
            {
               const real_t wz  = B(qz,dz);
               const real_t wDz = G(qz,dz);
               for (int qy = 0; qy < Q1D; ++qy)
               {
                  for (int qx = 0; qx < Q1D; ++qx)
                  {
                     grad[qz][qy][qx][0] += gradXY[qy][qx][0] * wz;
                     grad[qz][qy][qx][1] += gradXY[qy][qx][1] * wz;
                     grad[qz][qy][qx][2] += gradXY[qy][qx][2] * wDz;
                  }
               }
            }
         }
         // We've now calculated grad(u_c) = [Dxyz_1, xDyz_2, xyDz_3] in plane
         for (int qz = 0; qz < Q1D; ++qz)
         {
            for (int qy = 0; qy < Q1D; ++qy)
            {
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  const int q = qx + (qy + qz * Q1D) * Q1D;
                  const real_t gradX = grad[qz][qy][qx][0];
                  const real_t gradY = grad[qz][qy][qx][1];
                  const real_t gradZ = grad[qz][qy][qx][2];

                  div[qz][qy][qx] += gradX*op(q,0,c,e) + gradY*op(q,1,c,e) + gradZ*op(q,2,c,e);

               }
            }
         }
      }
      // We've now calculated div = reshape(div phi * op) * u
      for (int qz = 0; qz < Q1D; ++qz)
      {
         real_t opXY[max_TE_D1D][max_TE_D1D];
         for (int dy = 0; dy < TE_D1D; ++dy)
         {
            for (int dx = 0; dx < TE_D1D; ++dx)
            {
               opXY[dy][dx] = 0.0;
            }
         }
         for (int qy = 0; qy < Q1D; ++qy)
         {
            real_t opX[max_TE_D1D];
            for (int dx = 0; dx < TE_D1D; ++dx)
            {
               opX[dx] = 0.0;
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  opX[dx] += Bt(dx,qx)*div[qz][qy][qx];
               }
            }
            for (int dy = 0; dy < TE_D1D; ++dy)
            {
               for (int dx = 0; dx < TE_D1D; ++dx)
               {
                  opXY[dy][dx] += Bt(dy,qy)*opX[dx];
               }
            }
         }
         for (int dz = 0; dz < TE_D1D; ++dz)
         {
            for (int dy = 0; dy < TE_D1D; ++dy)
            {
               for (int dx = 0; dx < TE_D1D; ++dx)
               {
                  y(dx,dy,dz,e) += Bt(dz,qz)*opXY[dy][dx];
               }
            }
         }
      }
      // We've now calculated y = p * div
   });
}

// PA Vector Divergence Apply 3D kernel
template<const int T_TR_D1D = 0, const int T_TE_D1D = 0, const int T_Q1D = 0>
static void PADivergenceApplyTranspose3D(const int NE,
                                         const Array<real_t> &bt,
                                         const Array<real_t> &gt,
                                         const Array<real_t> &b,
                                         const Vector &op_,
                                         const Vector &x_,
                                         Vector &y_,
                                         int tr_d1d = 0,
                                         int te_d1d = 0,
                                         int q1d = 0)
{
   const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
   const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   MFEM_VERIFY(TR_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(TE_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(Q1D <= DeviceDofQuadLimits::Get().MAX_Q1D, "");
   auto Bt = Reshape(bt.Read(), TR_D1D, Q1D);
   auto Gt = Reshape(gt.Read(), TR_D1D, Q1D);
   auto B  = Reshape(b.Read(), Q1D, TE_D1D);
   auto op = Reshape(op_.Read(), Q1D*Q1D*Q1D, 3,3, NE);
   auto x  = Reshape(x_.Read(), TE_D1D, TE_D1D, TE_D1D, NE);
   auto y  = Reshape(y_.ReadWrite(), TR_D1D, TR_D1D, TR_D1D, 3, NE);
   mfem::forall(NE, [=] MFEM_HOST_DEVICE (int e)
   {
      const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
      const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      const int VDIM = 3;
      // the following variables are evaluated at compile time
      constexpr int max_TR_D1D = T_TR_D1D ? T_TR_D1D : DofQuadLimits::MAX_D1D;
      constexpr int max_Q1D = T_Q1D ? T_Q1D : DofQuadLimits::MAX_Q1D;

      real_t quadTest[max_Q1D][max_Q1D][max_Q1D];
      real_t grad[max_Q1D][max_Q1D][max_Q1D][VDIM];
      for (int qz = 0; qz < Q1D; ++qz)
      {
         for (int qy = 0; qy < Q1D; ++qy)
         {
            for (int qx = 0; qx < Q1D; ++qx)
            {
               quadTest[qz][qy][qx] = 0.0;
            }
         }
      }
      for (int dz = 0; dz < TE_D1D; ++dz)
      {
         real_t quadTestXY[max_Q1D][max_Q1D];
         for (int qy = 0; qy < Q1D; ++qy)
         {
            for (int qx = 0; qx < Q1D; ++qx)
            {
               quadTestXY[qy][qx] = 0.0;
            }
         }
         for (int dy = 0; dy < TE_D1D; ++dy)
         {
            real_t quadTestX[max_Q1D];
            for (int qx = 0; qx < Q1D; ++qx)
            {
               quadTestX[qx] = 0.0;
            }
            for (int dx = 0; dx < TE_D1D; ++dx)
            {
               const real_t s = x(dx,dy,dz,e);
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  quadTestX[qx] += s * B(qx,dx);
               }
            }
            for (int qy = 0; qy < Q1D; ++qy)
            {
               const real_t wy  = B(qy,dy);
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  quadTestXY[qy][qx] += quadTestX[qx] * wy;
               }
            }
         }
         for (int qz = 0; qz < Q1D; ++qz)
         {
            const real_t wz  = B(qz,dz);
            for (int qy = 0; qy < Q1D; ++qy)
            {
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  quadTest[qz][qy][qx] += quadTestXY[qy][qx] * wz;
               }
            }
         }
      }
      // We've now calculated x on the quads
      for (int c = 0; c < VDIM; ++c)
      {
         for (int qz = 0; qz < Q1D; ++qz)
         {
            for (int qy = 0; qy < Q1D; ++qy)
            {
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  const int q = qx + (qy + qz * Q1D) * Q1D;
                  grad[qz][qy][qx][0] = quadTest[qz][qy][qx]*op(q,0,c,e);
                  grad[qz][qy][qx][1] = quadTest[qz][qy][qx]*op(q,1,c,e);
                  grad[qz][qy][qx][2] = quadTest[qz][qy][qx]*op(q,2,c,e);
               }
            }
         }
         // We've now calculated op_c^T * x
         for (int qz = 0; qz < Q1D; ++qz)
         {
            real_t gradXY[max_TR_D1D][max_TR_D1D][VDIM];
            for (int dy = 0; dy < TR_D1D; ++dy)
            {
               for (int dx = 0; dx < TR_D1D; ++dx)
               {
                  gradXY[dy][dx][0] = 0.0;
                  gradXY[dy][dx][1] = 0.0;
                  gradXY[dy][dx][2] = 0.0;
               }
            }
            for (int qy = 0; qy < Q1D; ++qy)
            {
               real_t gradX[max_TR_D1D][VDIM];
               for (int dx = 0; dx < TR_D1D; ++dx)
               {
                  gradX[dx][0] = 0.0;
                  gradX[dx][1] = 0.0;
                  gradX[dx][2] = 0.0;
               }
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  const real_t gX = grad[qz][qy][qx][0];
                  const real_t gY = grad[qz][qy][qx][1];
                  const real_t gZ = grad[qz][qy][qx][2];
                  for (int dx = 0; dx < TR_D1D; ++dx)
                  {
                     const real_t wx  = Bt(dx,qx);
                     const real_t wDx = Gt(dx,qx);
                     gradX[dx][0] += gX * wDx;
                     gradX[dx][1] += gY * wx;
                     gradX[dx][2] += gZ * wx;
                  }
               }
               for (int dy = 0; dy < TR_D1D; ++dy)
               {
                  const real_t wy  = Bt(dy,qy);
                  const real_t wDy = Gt(dy,qy);
                  for (int dx = 0; dx < TR_D1D; ++dx)
                  {
                     gradXY[dy][dx][0] += gradX[dx][0] * wy;
                     gradXY[dy][dx][1] += gradX[dx][1] * wDy;
                     gradXY[dy][dx][2] += gradX[dx][2] * wy;
                  }
               }
            }
            for (int dz = 0; dz < TR_D1D; ++dz)
            {
               const real_t wz  = Bt(dz,qz);
               const real_t wDz = Gt(dz,qz);
               for (int dy = 0; dy < TR_D1D; ++dy)
               {
                  for (int dx = 0; dx < TR_D1D; ++dx)
                  {
                     y(dx,dy,dz,c,e) +=
                        ((gradXY[dy][dx][0] * wz) +
                         (gradXY[dy][dx][1] * wz) +
                         (gradXY[dy][dx][2] * wDz));
                  }
               }
            }
         }
      }
      // We've now calculated y = reshape(div u * op^T) * x
   });
}

// Shared memory PA Vector Divergence Apply 3D kernel
template<const int T_TR_D1D = 0, const int T_TE_D1D = 0, const int T_Q1D = 0>
static void SmemPADivergenceApply3D(const int NE,
                                    const Array<real_t> &b_,
                                    const Array<real_t> &g_,
                                    const Array<real_t> &bt_,
                                    const Vector &q_,
                                    const Vector &x_,
                                    Vector &y_,
                                    const int tr_d1d = 0,
                                    const int te_d1d = 0,
                                    const int q1d = 0)
{
   const int TR_D1D = T_TR_D1D ? T_TR_D1D : tr_d1d;
   const int TE_D1D = T_TE_D1D ? T_TE_D1D : te_d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;

   MFEM_VERIFY(TR_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(TE_D1D <= DeviceDofQuadLimits::Get().MAX_D1D, "");
   MFEM_VERIFY(Q1D <= DeviceDofQuadLimits::Get().MAX_Q1D, "");

   auto b = Reshape(b_.Read(), Q1D, TR_D1D);
   auto g = Reshape(g_.Read(), Q1D, TR_D1D);
   auto bt = Reshape(bt_.Read(), TE_D1D, Q1D);
   auto Q = Reshape(q_.Read(), Q1D*Q1D*Q1D, 3,3, NE);
   auto x = Reshape(x_.Read(), TR_D1D, TR_D1D, TR_D1D, 3, NE);
   auto y = Reshape(y_.ReadWrite(), TE_D1D, TE_D1D, TE_D1D, NE);

   mfem::forall_3D(NE, Q1D, Q1D, Q1D, [=] MFEM_HOST_DEVICE (int e)
   {
      constexpr int VDIM = 3;
      const int tidz = MFEM_THREAD_ID(z);
      const int D1DR = T_TR_D1D ? T_TR_D1D : tr_d1d;
      const int D1DE = T_TE_D1D ? T_TE_D1D : te_d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      constexpr int MQ1 = T_Q1D ? T_Q1D : DofQuadLimits::MAX_Q1D;
      constexpr int MD1R = T_TR_D1D ? T_TR_D1D : DofQuadLimits::MAX_D1D;
      constexpr int MD1E = T_TE_D1D ? T_TE_D1D : DofQuadLimits::MAX_D1D;
      constexpr int MD1 = MD1E > MD1R ? MD1E : MD1R;
      constexpr int MDQ = MQ1 > MD1 ? MQ1 : MD1;
      MFEM_SHARED real_t sBG[2][MQ1*MD1];
      real_t (*B)[MD1] = (real_t (*)[MD1]) (sBG+0);
      real_t (*G)[MD1] = (real_t (*)[MD1]) (sBG+1);
      real_t (*Bt)[MQ1] = (real_t (*)[MQ1]) (sBG+0);
      MFEM_SHARED real_t sm0[3][MDQ*MDQ*MDQ];
      MFEM_SHARED real_t sm1[3][MDQ*MDQ*MDQ];
      real_t (*X)[MD1][MD1]    = (real_t (*)[MD1][MD1]) (sm0+2);
      real_t (*DDQ0)[MD1][MQ1] = (real_t (*)[MD1][MQ1]) (sm0+0);
      real_t (*DDQ1)[MD1][MQ1] = (real_t (*)[MD1][MQ1]) (sm0+1);
      real_t (*DQQ0)[MQ1][MQ1] = (real_t (*)[MQ1][MQ1]) (sm1+0);
      real_t (*DQQ1)[MQ1][MQ1] = (real_t (*)[MQ1][MQ1]) (sm1+1);
      real_t (*DQQ2)[MQ1][MQ1] = (real_t (*)[MQ1][MQ1]) (sm1+2);
      real_t (*QQQ0)[MQ1][MQ1] = (real_t (*)[MQ1][MQ1]) (sm0+0);
      real_t (*QQQ1)[MQ1][MQ1] = (real_t (*)[MQ1][MQ1]) (sm0+1);
      real_t (*QQQ2)[MQ1][MQ1] = (real_t (*)[MQ1][MQ1]) (sm0+2);
      real_t (*QQD0)[MQ1][MD1] = (real_t (*)[MQ1][MD1]) (sm1+0);
      real_t (*QDD0)[MD1][MD1] = (real_t (*)[MD1][MD1]) (sm0+0);
      MFEM_SHARED real_t div[MQ1][MQ1][MQ1];

      if (tidz == 0)
      {
         MFEM_FOREACH_THREAD(d,y,D1DR)
         {
            MFEM_FOREACH_THREAD(q,x,Q1D)
            {
               B[q][d] = b(q,d);
               G[q][d] = g(q,d);
            }
         }
      }
      MFEM_SYNC_THREAD;
      MFEM_FOREACH_THREAD(qz,z,Q1D)
      {
         MFEM_FOREACH_THREAD(qy,y,Q1D)
         {
            MFEM_FOREACH_THREAD(qx,x,Q1D)
            {
               div[qz][qy][qx] = 0.0;
            }
         }
      }
      MFEM_SYNC_THREAD;

      for (int c = 0; c < VDIM; ++c)
      {
         MFEM_FOREACH_THREAD(qz,z,Q1D)
         {
            MFEM_FOREACH_THREAD(qy,y,Q1D)
            {
               MFEM_FOREACH_THREAD(qx,x,Q1D)
               {
                  QQQ0[qz][qy][qx] = 0.0;
                  QQQ1[qz][qy][qx] = 0.0;
                  QQQ2[qz][qy][qx] = 0.0;
               }
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(dz,z,D1DR)
         {
            MFEM_FOREACH_THREAD(dy,y,D1DR)
            {
               MFEM_FOREACH_THREAD(dx,x,D1DR)
               {
                  X[dz][dy][dx] = x(dx,dy,dz,c,e);
               }
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(dz,z,D1DR)
         {
            MFEM_FOREACH_THREAD(dy,y,D1DR)
            {
               MFEM_FOREACH_THREAD(qx,x,Q1D)
               {
                  real_t u = 0.0;
                  real_t v = 0.0;
                  for (int dx = 0; dx < D1DR; ++dx)
                  {
                     const real_t coord = X[dz][dy][dx];
                     u += coord * B[qx][dx];
                     v += coord * G[qx][dx];
                  }
                  DDQ0[dz][dy][qx] = u;
                  DDQ1[dz][dy][qx] = v;
               }
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(dz,z,D1DR)
         {
            MFEM_FOREACH_THREAD(qy,y,Q1D)
            {
               MFEM_FOREACH_THREAD(qx,x,Q1D)
               {
                  real_t u = 0.0;
                  real_t v = 0.0;
                  real_t w = 0.0;
                  for (int dy = 0; dy < D1DR; ++dy)
                  {
                     u += DDQ1[dz][dy][qx] * B[qy][dy];
                     v += DDQ0[dz][dy][qx] * G[qy][dy];
                     w += DDQ0[dz][dy][qx] * B[qy][dy];
                  }
                  DQQ0[dz][qy][qx] = u;
                  DQQ1[dz][qy][qx] = v;
                  DQQ2[dz][qy][qx] = w;
               }
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(qz,z,Q1D)
         {
            MFEM_FOREACH_THREAD(qy,y,Q1D)
            {
               MFEM_FOREACH_THREAD(qx,x,Q1D)
               {
                  real_t u = 0.0;
                  real_t v = 0.0;
                  real_t w = 0.0;
                  for (int dz = 0; dz < D1DR; ++dz)
                  {
                     u += DQQ0[dz][qy][qx] * B[qz][dz];
                     v += DQQ1[dz][qy][qx] * B[qz][dz];
                     w += DQQ2[dz][qy][qx] * G[qz][dz];
                  }
                  QQQ0[qz][qy][qx] = u;
                  QQQ1[qz][qy][qx] = v;
                  QQQ2[qz][qy][qx] = w;
               }
            }
         }
         MFEM_SYNC_THREAD;
         MFEM_FOREACH_THREAD(qz,z,Q1D)
         {
            MFEM_FOREACH_THREAD(qy,y,Q1D)
            {
               MFEM_FOREACH_THREAD(qx,x,Q1D)
               {
                  const int q = qx + (qy + qz * Q1D) * Q1D;
                  const real_t gX = QQQ0[qz][qy][qx];
                  const real_t gY = QQQ1[qz][qy][qx];
                  const real_t gZ = QQQ2[qz][qy][qx];
                  div[qz][qy][qx] += gX*Q(q,0,c,e) + gY*Q(q,1,c,e) + gZ*Q(q,2,c,e);
               }
            }
         }
         MFEM_SYNC_THREAD;
      }

      if (tidz == 0)
      {
         MFEM_FOREACH_THREAD(d,y,D1DE)
         {
            MFEM_FOREACH_THREAD(q,x,Q1D)
            {
               Bt[d][q] = bt(d,q);
            }
         }
      }
      MFEM_SYNC_THREAD;

      MFEM_FOREACH_THREAD(qz,z,Q1D)
      {
         MFEM_FOREACH_THREAD(qy,y,Q1D)
         {
            MFEM_FOREACH_THREAD(dx,x,D1DE)
            {
               real_t u = 0.0;
               for (int qx = 0; qx < Q1D; ++qx)
               {
                  u += div[qz][qy][qx] * Bt[dx][qx];
               }
               QQD0[qz][qy][dx] = u;
            }
         }
      }
      MFEM_SYNC_THREAD;
      MFEM_FOREACH_THREAD(qz,z,Q1D)
      {
         MFEM_FOREACH_THREAD(dy,y,D1DE)
         {
            MFEM_FOREACH_THREAD(dx,x,D1DE)
            {
               real_t u = 0.0;
               for (int qy = 0; qy < Q1D; ++qy)
               {
                  u += QQD0[qz][qy][dx] * Bt[dy][qy];
               }
               QDD0[qz][dy][dx] = u;
            }
         }
      }
      MFEM_SYNC_THREAD;
      MFEM_FOREACH_THREAD(dz,z,D1DE)
      {
         MFEM_FOREACH_THREAD(dy,y,D1DE)
         {
            MFEM_FOREACH_THREAD(dx,x,D1DE)
            {
               real_t u = 0.0;
               for (int qz = 0; qz < Q1D; ++qz)
               {
                  u += QDD0[qz][dy][dx] * Bt[dz][qz];
               }
               y(dx,dy,dz,e) += u;
            }
         }
      }
   });
}

static void PADivergenceApply(const int dim,
                              const int TR_D1D,
                              const int TE_D1D,
                              const int Q1D,
                              const int NE,
                              const Array<real_t> &B,
                              const Array<real_t> &G,
                              const Array<real_t> &Bt,
                              const Vector &op,
                              const Vector &x,
                              Vector &y,
                              bool transpose=false)
{
   if (dim == 2)
   {
      if (transpose)
      {
         return PADivergenceApplyTranspose2D(NE,B,G,Bt,op,x,y,TR_D1D,TE_D1D,Q1D);
      }
      else
      {
         return PADivergenceApply2D(NE,B,G,Bt,op,x,y,TR_D1D,TE_D1D,Q1D);
      }
   }
   if (dim == 3)
   {
      if (transpose)
      {
         return PADivergenceApplyTranspose3D(NE,B,G,Bt,op,x,y,TR_D1D,TE_D1D,Q1D);
      }
      else
      {
         return PADivergenceApply3D(NE,B,G,Bt,op,x,y,TR_D1D,TE_D1D,Q1D);
      }
   }
   MFEM_ABORT("Unknown kernel.");
}

// PA Divergence Apply kernel
void VectorDivergenceIntegrator::AddMultPA(const Vector &x, Vector &y) const
{
   PADivergenceApply(dim, trial_dofs1D, test_dofs1D, quad1D, ne,
                     trial_maps->B, trial_maps->G, test_maps->Bt, pa_data, x, y,
                     false);
}

// PA Divergence Apply kernel
void VectorDivergenceIntegrator::AddMultTransposePA(const Vector &x,
                                                    Vector &y) const
{
   PADivergenceApply(dim, trial_dofs1D, test_dofs1D, quad1D, ne,
                     trial_maps->Bt, trial_maps->Gt, test_maps->B, pa_data, x, y,
                     true);
}

} // namespace mfem
