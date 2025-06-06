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

#include "mesh_headers.hpp"

namespace mfem
{

Element::Type Element::TypeFromGeometry(const Geometry::Type geom)
{
   switch (geom)
   {
      case Geometry::POINT: return Element::POINT;
      case Geometry::SEGMENT: return Element::SEGMENT;
      case Geometry::TRIANGLE: return Element::TRIANGLE;
      case Geometry::SQUARE: return Element::QUADRILATERAL;
      case Geometry::TETRAHEDRON: return Element::TETRAHEDRON;
      case Geometry::CUBE: return Element::HEXAHEDRON;
      case Geometry::PRISM: return Element::WEDGE;
      case Geometry::PYRAMID: return Element::PYRAMID;
      default: MFEM_ABORT("Unknown geometry type.");
   }
}

void Element::SetVertices(const int *ind)
{
   int i, n, *v;

   n = GetNVertices();
   v = GetVertices();

   for (i = 0; i < n; i++)
   {
      v[i] = ind[i];
   }
}

}
