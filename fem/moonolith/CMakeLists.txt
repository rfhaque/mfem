# Copyright (c) 2010-2025, Lawrence Livermore National Security, LLC. Produced
# at the Lawrence Livermore National Laboratory. All Rights reserved. See files
# LICENSE and NOTICE for details. LLNL-CODE-806117.
#
# This file is part of the MFEM library. For more information and source code
# availability visit https://mfem.org.
#
# MFEM is free software; you can redistribute it and/or modify it under the
# terms of the BSD-3 license. We welcome feedback and contributions, see file
# CONTRIBUTING.md for details.

list(APPEND SRCS
  cut.cpp
  transferutils.cpp
  mortarassembler.cpp
  mortarintegrator.cpp
  transfer.cpp
  )

list(APPEND HDRS
  cut.hpp
  transferutils.hpp
  mortarassembler.hpp
  mortarintegrator.hpp
  transfer.hpp
  )

if(MFEM_USE_MPI)
  list(APPEND SRCS
    pmortarassembler.cpp
    )

  list(APPEND HDRS
    pmortarassembler.hpp
    )
endif()

convert_filenames_to_full_paths(SRCS)
convert_filenames_to_full_paths(HDRS)

set(SOURCES ${SOURCES} ${SRCS} PARENT_SCOPE)
set(HEADERS ${HEADERS} ${HDRS} PARENT_SCOPE)
