//
//
/*
    Copyright (C) 2019, 2021 University College London
    This file is part of STIR.

    This file is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This file is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    See STIR/LICENSE.txt for details
*/
/*!
  \file
  \ingroup Parallelproj

  \brief Declares class stir::ProjectorByBinPairUsingParallelproj

  \author Richard Brown
  \author Kris Thielemans

*/
#ifndef __stir_recon_buildblock_ProjectorByBinPairUsingParallelproj_h_
#define __stir_recon_buildblock_ProjectorByBinPairUsingParallelproj_h_

#include "stir/RegisteredParsingObject.h"
#include "stir/recon_buildblock/ProjectorByBinPair.h"

START_NAMESPACE_STIR

class Succeeded;
namespace detail { class ParallelprojHelper; }
/*!
  \ingroup Parallelproj
  \brief A projector pair based on Parallelproj projectors
*/
class ProjectorByBinPairUsingParallelproj :
  public RegisteredParsingObject<ProjectorByBinPairUsingParallelproj,
                                 ProjectorByBinPair,
                                 ProjectorByBinPair> 
{ 
 private:
  typedef
    RegisteredParsingObject<ProjectorByBinPairUsingParallelproj,
                            ProjectorByBinPair,
                            ProjectorByBinPair> 
    base_type;
public:
  //! Name which will be used when parsing a ProjectorByBinPair object
  static const char * const registered_name; 

  //! Default constructor 
  ProjectorByBinPairUsingParallelproj();

  Succeeded
    set_up(const shared_ptr<ProjDataInfo>&,
           const shared_ptr<DiscretisedDensity<3,float> >&);

  /// Set verbosity
  void set_verbosity(const bool verbosity);

private:
  shared_ptr<detail::ParallelprojHelper> _helper;

  void set_defaults();
  void initialise_keymap();
  bool post_processing();
  bool _verbosity;
};

END_NAMESPACE_STIR


#endif // __stir_recon_buildblock_ProjectorByBinPairUsingParallelproj_h_
