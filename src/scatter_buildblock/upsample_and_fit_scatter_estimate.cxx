/*
  Copyright (C) 2005 - 2011-12-31, Hammersmith Imanet Ltd
  Copyright (C) 2014, 2020 University College London
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
\ingroup scatter
\brief implementation of stir::ScatterEstimationByBin::upsample_and_fit_scatter_estimate

\author Charalampos Tsoumpas
\author Kris Thielemans
*/

#include "stir/ProjDataInfo.h"
#include "stir/ExamInfo.h"
#include "stir/ProjDataInMemory.h"
#include "stir/inverse_SSRB.h"
#include "stir/scale_sinograms.h"
#include "stir/scatter/ScatterEstimation.h"
#include "stir/recon_buildblock/BinNormalisation.h"
#include "stir/interpolate_projdata.h"
#include "stir/utilities.h"
#include "stir/IndexRange2D.h" 
#include "stir/stream.h"
#include "stir/Succeeded.h"
#include "stir/thresholding.h"
#include "stir/is_null_ptr.h"
#include "stir/ArrayFilter1DUsingConvolution.h"
#include <iostream>
#include <fstream>
#include <string>
/***********************************************************/

START_NAMESPACE_STIR

void 
ScatterEstimation::
upsample_and_fit_scatter_estimate(ProjData& scaled_scatter_proj_data,
                                  const  ProjData& emission_proj_data,
                                  const ProjData& scatter_proj_data,
                                  BinNormalisation& scatter_normalisation,
                                  const ProjData& weights_proj_data,
                                  const float min_scale_factor,
                                  const float max_scale_factor,
                                  const unsigned half_filter_width,
                                  BSpline::BSplineType spline_type,
                                  const bool remove_interleaving)
{
  shared_ptr<ProjDataInfo> 
    interpolated_direct_scatter_proj_data_info_sptr(emission_proj_data.get_proj_data_info_sptr()->clone());
  interpolated_direct_scatter_proj_data_info_sptr->reduce_segment_range(0,0);

  info("upsample_and_fit_scatter_estimate: Interpolating scatter estimate to size of emission data");
  ProjDataInMemory interpolated_direct_scatter(emission_proj_data.get_exam_info_sptr(),
					       interpolated_direct_scatter_proj_data_info_sptr);        
  interpolate_projdata(interpolated_direct_scatter, scatter_proj_data, spline_type, remove_interleaving);

  const TimeFrameDefinitions& time_frame_defs =
    emission_proj_data.get_exam_info_sptr()->time_frame_definitions;

  if (min_scale_factor != 1 || max_scale_factor != 1 || !scatter_normalisation.is_trivial())
    {
      ProjDataInMemory interpolated_scatter(emission_proj_data.get_exam_info_sptr(),
					    emission_proj_data.get_proj_data_info_sptr()->create_shared_clone());
      inverse_SSRB(interpolated_scatter, interpolated_direct_scatter);

      scatter_normalisation.set_up(emission_proj_data.get_exam_info_sptr(), emission_proj_data.get_proj_data_info_sptr()->create_shared_clone());
      scatter_normalisation.undo(interpolated_scatter);
      Array<2,float> scale_factors;

      if (min_scale_factor == max_scale_factor)
	{
	  if (min_scale_factor == 1.F)
            {
              scaled_scatter_proj_data.fill(interpolated_scatter);
              return; // all done
            }

	  const ProjDataInfo& proj_data_info = *emission_proj_data.get_proj_data_info_sptr();
	  IndexRange2D sinogram_range(proj_data_info.get_min_segment_num(),proj_data_info.get_max_segment_num(),0,0);
	  for (int segment_num=proj_data_info.get_min_segment_num();
	       segment_num<=proj_data_info.get_max_segment_num();
	       ++segment_num)
	    {
	      sinogram_range[segment_num].resize(
						 proj_data_info.get_min_axial_pos_num(segment_num),
						 proj_data_info.get_max_axial_pos_num(segment_num) );
	    }
	  scale_factors.grow(sinogram_range);
	  scale_factors.fill(min_scale_factor);
	}
      else
	{
	  info("upsample_and_fit_scatter_estimate: Finding scale factors by sinogram", 3);
	  scale_factors = get_scale_factors_per_sinogram(
							 emission_proj_data, 
							 interpolated_scatter,
							 weights_proj_data);
    
	  info(boost::format("upsample_and_fit_scatter_estimate: scale factors before thresholding:\n%1%") %
	       scale_factors,
	       2);
	
	  threshold_lower(scale_factors.begin_all(), 
			  scale_factors.end_all(),
			  min_scale_factor);
	  threshold_upper(scale_factors.begin_all(), 
			  scale_factors.end_all(),
			  max_scale_factor);
	  info(boost::format("upsample_and_fit_scatter_estimate: scale factors after thresholding:\n%1%") %
	       scale_factors,
	       2);
	  VectorWithOffset<float> kernel(-static_cast<int>(half_filter_width),half_filter_width);
	  kernel.fill(1.F/(2*half_filter_width+1));
	  ArrayFilter1DUsingConvolution<float> lowpass_filter(kernel, BoundaryConditions::constant);
	  std::for_each(scale_factors.begin(), 
			scale_factors.end(),
			lowpass_filter);
	  info(boost::format("upsample_and_fit_scatter_estimate: scale factors after filtering:\n%1%") %
	       scale_factors,
	       2);
	}
      info("upsample_and_fit_scatter_estimate: applying scale factors", 3);
      if (scale_sinograms(scaled_scatter_proj_data, 
                          interpolated_scatter,
                          scale_factors) != Succeeded::yes)
        {
          error("upsample_and_fit_scatter_estimate: writing of scaled sinograms failed");
        }
    }
  else // min/max_scale_factor equal to 1 and no norm
    {
      inverse_SSRB(scaled_scatter_proj_data, interpolated_direct_scatter);
    }
}

END_NAMESPACE_STIR
