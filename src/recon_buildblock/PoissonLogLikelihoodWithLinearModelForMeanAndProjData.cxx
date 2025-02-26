/*
    Copyright (C) 2000 PARAPET partners
    Copyright (C) 2000-2011, Hammersmith Imanet Ltd
    Copyright (C) 2018, 2019 University College London
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
  \ingroup GeneralisedObjectiveFunction
  \brief Declaration of class stir::PoissonLogLikelihoodWithLinearModelForMeanAndProjData

  \author Kris Thielemans
  \author Matthew Jacobson
  \author Sanida Mustafovic
  \author PARAPET project
*/

#include "stir/recon_buildblock/PoissonLogLikelihoodWithLinearModelForMeanAndProjData.h"
#include "stir/VoxelsOnCartesianGrid.h"
#include "stir/recon_buildblock/TrivialBinNormalisation.h"
#include "stir/Succeeded.h"
#include "stir/RelatedViewgrams.h"
#include "stir/stream.h"
#include "stir/info.h"

#include "stir/recon_buildblock/ProjectorByBinPair.h"

#include "stir/DiscretisedDensity.h"
#ifdef STIR_MPI
#include "stir/recon_buildblock/DistributedCachingInformation.h"
#endif
#include "stir/recon_buildblock/distributable.h"
// for get_symmetries_ptr()
#include "stir/DataSymmetriesForViewSegmentNumbers.h"
// include the following to set defaults
#ifndef USE_PMRT
#include "stir/recon_buildblock/ForwardProjectorByBinUsingRayTracing.h"
#include "stir/recon_buildblock/BackProjectorByBinUsingInterpolation.h"
#else
#include "stir/recon_buildblock/ForwardProjectorByBinUsingProjMatrixByBin.h"
#include "stir/recon_buildblock/BackProjectorByBinUsingProjMatrixByBin.h"
#include "stir/recon_buildblock/ProjMatrixByBinUsingRayTracing.h"
#endif
#include "stir/recon_buildblock/ProjectorByBinPairUsingSeparateProjectors.h"
#include "stir/recon_buildblock/find_basic_vs_nums_in_subsets.h"
#include "stir/recon_buildblock/BinNormalisationWithCalibration.h"

#include "stir/ProjDataInMemory.h"

#include "stir/Viewgram.h"
#include "stir/recon_array_functions.h"
#include "stir/is_null_ptr.h"
#include <iostream>
#include <algorithm>
#include <functional>
#include <sstream>
#ifdef STIR_MPI
#include "stir/recon_buildblock/distributed_functions.h"
#endif
#include "stir/CPUTimer.h"
#include "stir/info.h"
#include <boost/format.hpp>

#ifndef STIR_NO_NAMESPACES
using std::vector;
using std::pair;
using std::ends;
using std::max;
#endif


START_NAMESPACE_STIR

const int rim_truncation_sino = 0; // TODO get rid of this

template<typename TargetT>
const char * const 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
registered_name = 
"PoissonLogLikelihoodWithLinearModelForMeanAndProjData";

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_defaults()
{
  base_type::set_defaults();

  this->input_filename="";
  this->max_segment_num_to_process=-1;
  // KT 20/06/2001 disabled
  //num_views_to_add=1;  
  this->proj_data_sptr.reset(); //MJ added
  this->zero_seg0_end_planes = 0;

  this->additive_projection_data_filename = "0";
  this->additive_proj_data_sptr.reset();


  // set default for projector_pair_ptr
#ifndef USE_PMRT
  shared_ptr<ForwardProjectorByBin> forward_projector_ptr(new ForwardProjectorByBinUsingRayTracing());
  shared_ptr<BackProjectorByBin> back_projector_ptr(new BackProjectorByBinUsingInterpolation());
#else
  shared_ptr<ProjMatrixByBinUsingRayTracing> PM(new  ProjMatrixByBinUsingRayTracing());
  // PM->set_num_tangential_LORs(5);
  shared_ptr<ForwardProjectorByBin> forward_projector_ptr(new ForwardProjectorByBinUsingProjMatrixByBin(PM)); 
  shared_ptr<BackProjectorByBin> back_projector_ptr(new BackProjectorByBinUsingProjMatrixByBin(PM)); 
#endif

  this->projector_pair_ptr.reset(
				 new ProjectorByBinPairUsingSeparateProjectors(forward_projector_ptr, back_projector_ptr));

  this->normalisation_sptr.reset(new TrivialBinNormalisation);
  this->frame_num = 1;
  this->frame_definition_filename = "";
  // make a single frame starting from 0 to 1.
  vector<pair<double, double> > frame_times(1, pair<double,double>(0,1));
  this->frame_defs = TimeFrameDefinitions(frame_times);

  this->target_parameter_parser.set_defaults();
  
#ifdef STIR_MPI
  //distributed stuff
  this->distributed_cache_enabled = false;
  this->distributed_tests_enabled = false;
  this->message_timings_enabled = false;
  this->message_timings_threshold = 0.1;
  this->rpc_timings_enabled = false;
#endif
}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
initialise_keymap()
{
  base_type::initialise_keymap();
  this->parser.add_start_key("PoissonLogLikelihoodWithLinearModelForMeanAndProjData Parameters");
  this->parser.add_stop_key("End PoissonLogLikelihoodWithLinearModelForMeanAndProjData Parameters");
  this->parser.add_key("input file",&this->input_filename);
  // KT 20/06/2001 disabled
  //parser.add_key("mash x views", &num_views_to_add);

  this->parser.add_key("maximum absolute segment number to process", &this->max_segment_num_to_process);
  this->parser.add_key("zero end planes of segment 0", &this->zero_seg0_end_planes);

  this->target_parameter_parser.add_to_keymap(this->parser);

  this->parser.add_parsing_key("Projector pair type", &this->projector_pair_ptr);
  this->parser.add_key("additive sinogram",&this->additive_projection_data_filename);
  // normalisation (and attenuation correction)
  this->parser.add_key("time frame definition filename", &this->frame_definition_filename); 
  this->parser.add_key("time frame number", &this->frame_num);
  this->parser.add_parsing_key("Bin Normalisation type", &this->normalisation_sptr);

#ifdef STIR_MPI
  //distributed stuff 
  this->parser.add_key("enable distributed caching", &distributed_cache_enabled);
  this->parser.add_key("enable distributed tests", &distributed_tests_enabled);
  this->parser.add_key("enable message timings", &message_timings_enabled);
  this->parser.add_key("message timings threshold", &message_timings_threshold);
  this->parser.add_key("enable rpc timings", &rpc_timings_enabled);
#endif
}

template<typename TargetT>
bool
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
post_processing()
{
  if (base_type::post_processing() == true)
    return true;

  // KT 20/06/2001 disabled as not functional yet
#if 0
  if (num_views_to_add!=1 && (num_views_to_add<=0 || num_views_to_add%2 != 0))
  { warning("The 'mash x views' key has an invalid value (must be 1 or even number)"); return true; }
#endif
 
  if (this->input_filename.length() > 0 )
  {
    this->proj_data_sptr= ProjData::read_from_file(input_filename);

    if (is_null_ptr(this->proj_data_sptr))
        {
            error("Failed to read input file %s", input_filename.c_str());
            return true;
        }
  }

  target_parameter_parser.check_values();

  if (this->additive_projection_data_filename != "0")
  {
    info(boost::format("Reading additive projdata data %1%") % this->additive_projection_data_filename);
    this->additive_proj_data_sptr = 
      ProjData::read_from_file(this->additive_projection_data_filename);
  };

  // read time frame def 
   if (this->frame_definition_filename.size()!=0)
    this->frame_defs = TimeFrameDefinitions(this->frame_definition_filename);
   else
    {
      // make a single frame starting from 0 to 1.
      vector<pair<double, double> > frame_times(1, pair<double,double>(0,1));
      this->frame_defs = TimeFrameDefinitions(frame_times);
    } 
#ifndef STIR_MPI
#if 0
   //check caching enabled value
   if (this->distributed_cache_enabled==true) 
     {
       warning("STIR must be compiled with MPI-compiler to use distributed caching.\n\tDistributed Caching support will be disabled!");
       this->distributed_cache_enabled=false;
     }
   //check tests enabled value
   if (this->distributed_tests_enabled==true || rpc_timings_enabled==true || message_timings_enabled==true)
     {
       warning("STIR must be compiled with MPI-compiler and debug symbols to use distributed testing.\n\tDistributed tests will not be performed!");
       this->distributed_tests_enabled=false;
     }
#endif
#else 
   //check caching enabled value
   if (this->distributed_cache_enabled==true) 
     info("Will use distributed caching!");
   else info("Distributed caching is disabled. Will use standard distributed version without forced caching!");
   
#ifndef NDEBUG 
   //check tests enabled value
   if (this->distributed_tests_enabled==true) 
     {
       warning("\nWill perform distributed tests! Beware that this decreases the performance");
       distributed::test=true;
     }
#else 
   //check tests enabled value
   if (this->distributed_tests_enabled==true) 
     {
       warning("\nDistributed tests only abvailable in debug mode!");
       distributed::test=false;
     }
#endif
   
   //check timing values
   if (this->message_timings_enabled==true)
     {
       info("Will print timings of MPI-Messages! This is used to find bottlenecks!");
       distributed::test_send_receive_times=true;
     }
   //set timing threshold
   distributed::min_threshold=this->message_timings_threshold; 
   
   if (this->rpc_timings_enabled==true)
     {
       info("Will print run-times of processing RPC_process_related_viewgrams_gradient for every slave! This will give an idea of the parallelization effect!");
       distributed::rpc_time=true;
     }          
   
#endif

   //this->already_setup = false;
   return false;
}

template <typename TargetT>
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
PoissonLogLikelihoodWithLinearModelForMeanAndProjData()
{
  this->set_defaults();
}

template <typename TargetT>
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
~PoissonLogLikelihoodWithLinearModelForMeanAndProjData()
{
  end_distributable_computation();
}

template <typename TargetT>
TargetT *
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
construct_target_ptr() const
{
  return
    target_parameter_parser.create(this->get_input_data());
}

/***************************************************************
  get_ functions
***************************************************************/
template <typename TargetT>
const ProjData& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_proj_data() const
{ return *this->proj_data_sptr; }

template <typename TargetT>
const shared_ptr<ProjData>& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_proj_data_sptr() const
{ return this->proj_data_sptr; }

template <typename TargetT>
const int 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_max_segment_num_to_process() const
{ return this->max_segment_num_to_process; }

template <typename TargetT>
const bool 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_zero_seg0_end_planes() const
{ return this->zero_seg0_end_planes; }

template <typename TargetT>
const ProjData& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_additive_proj_data() const
{ return *this->additive_proj_data_sptr; }

template <typename TargetT>
const shared_ptr<ProjData>& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_additive_proj_data_sptr() const
{ return this->additive_proj_data_sptr; }

template <typename TargetT>
const ProjectorByBinPair& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_projector_pair() const
{ return *this->projector_pair_ptr; }

template <typename TargetT>
const shared_ptr<ProjectorByBinPair>& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_projector_pair_sptr() const
{ return this->projector_pair_ptr; }

template <typename TargetT>
const int 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_time_frame_num() const
{ return this->frame_num; }

template <typename TargetT>
const TimeFrameDefinitions& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_time_frame_definitions() const
{ return this->frame_defs; }

template <typename TargetT>
const BinNormalisation& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_normalisation() const
{ return *this->normalisation_sptr; }

template <typename TargetT>
const shared_ptr<BinNormalisation>& 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_normalisation_sptr() const
{ return this->normalisation_sptr; }


/***************************************************************
  set_ functions
***************************************************************/

template<typename TargetT>
int
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_num_subsets(const int new_num_subsets)
{
  this->num_subsets = std::max(new_num_subsets,1);
  return this->num_subsets;

}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_proj_data_sptr(const shared_ptr<ProjData>& arg)
{
  this->proj_data_sptr = arg;
}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_max_segment_num_to_process(const int arg)
{
  this->max_segment_num_to_process = arg;

}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_zero_seg0_end_planes(const bool arg)
{
  this->zero_seg0_end_planes = arg;
}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_additive_proj_data_sptr(const shared_ptr<ExamData> &arg)
{
    this->additive_proj_data_sptr = dynamic_pointer_cast<ProjData>(arg);
}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_projector_pair_sptr(const shared_ptr<ProjectorByBinPair>& arg) 
{
  this->projector_pair_ptr = arg;
}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_frame_num(const int arg)
{
  this->frame_num = arg;
}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_frame_definitions(const TimeFrameDefinitions& arg)
{
  this->frame_defs = arg;
}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_normalisation_sptr(const shared_ptr<BinNormalisation>& arg)
{
  this->normalisation_sptr = arg;
}

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_input_data(const shared_ptr<ExamData> & arg)
{
    this->proj_data_sptr = dynamic_pointer_cast<ProjData>(arg);
}

template<typename TargetT>
const ProjData&
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
get_input_data() const
{
  return *this->proj_data_sptr;
}


/***************************************************************
  subset balancing
 ***************************************************************/

template<typename TargetT>
bool
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
actual_subsets_are_approximately_balanced(std::string& warning_message) const
{
  assert(this->num_subsets>0);
  const DataSymmetriesForViewSegmentNumbers& symmetries =
    *this->projector_pair_ptr->get_back_projector_sptr()->get_symmetries_used();

  Array<1,int> num_vs_in_subset(this->num_subsets);
  num_vs_in_subset.fill(0);
  for (int subset_num=0; subset_num<this->num_subsets; ++subset_num)
    {
      for (int segment_num = -this->max_segment_num_to_process; 
           segment_num <= this->max_segment_num_to_process; 
           ++segment_num)
        for (int view_num = this->proj_data_sptr->get_min_view_num() + subset_num; 
             view_num <= this->proj_data_sptr->get_max_view_num(); 
             view_num += this->num_subsets)
          {
            const ViewSegmentNumbers view_segment_num(view_num, segment_num);
            if (!symmetries.is_basic(view_segment_num))
              continue;
            num_vs_in_subset[subset_num] +=
              symmetries.num_related_view_segment_numbers(view_segment_num);
          }
    }
  for (int subset_num=1; subset_num<this->num_subsets; ++subset_num)
    {
      if(num_vs_in_subset[subset_num] != num_vs_in_subset[0])
        { 
          std::stringstream str(warning_message);
          str <<"Number of subsets is such that subsets will be very unbalanced.\n"
              << "Number of viewgrams in each subset would be:\n"
              << num_vs_in_subset
              << "\nEither reduce the number of symmetries used by the projector, or\n"
            "change the number of subsets. It usually should be a divisor of\n"
              << this->proj_data_sptr->get_num_views()
              << "/4 (or if that's not an integer, a divisor of "
              << this->proj_data_sptr->get_num_views()
              << "/2 or "
              << this->proj_data_sptr->get_num_views() 
	      << ").\n";
          warning_message = str.str();
          return false;
        }
    }
  return true;
}

/***************************************************************
  set_up()
***************************************************************/
template<typename TargetT>
Succeeded 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
set_up_before_sensitivity(shared_ptr<const TargetT > const& target_sptr)
{
  if (is_null_ptr(this->proj_data_sptr))
	error("you need to set the input data before calling set_up");

  if (this->max_segment_num_to_process==-1)
    this->max_segment_num_to_process =
      this->proj_data_sptr->get_max_segment_num();

  if (this->max_segment_num_to_process > this->proj_data_sptr->get_max_segment_num()) 
    { 
      error("max_segment_num_to_process (%d) is too large",
              this->max_segment_num_to_process); 
      return Succeeded::no;
    }

  shared_ptr<ProjDataInfo> proj_data_info_sptr(this->proj_data_sptr->get_proj_data_info_sptr()->clone());
#if 0
  // KT 4/3/2017 disabled this. It isn't necessary and resolves modyfing the projectors in unexpected ways.
  proj_data_info_sptr->
    reduce_segment_range(-this->max_segment_num_to_process,
                         +this->max_segment_num_to_process);
#endif
  if (is_null_ptr(this->projector_pair_ptr))
    { error("You need to specify a projector pair"); return Succeeded::no; }

  // set projectors to be used for the calculations

  setup_distributable_computation(this->projector_pair_ptr,
                                  this->proj_data_sptr->get_exam_info_sptr(),
                                  this->proj_data_sptr->get_proj_data_info_sptr(),
                                  target_sptr,
                                  zero_seg0_end_planes,
                                  distributed_cache_enabled);
        
#ifdef STIR_MPI
  //set up distributed caching object
  if (distributed_cache_enabled) 
    {
      this->caching_info_ptr = new DistributedCachingInformation(distributed::num_processors);
    }
  else caching_info_ptr = NULL;
#else 
  //non parallel version
  caching_info_ptr = NULL;
#endif 

  this->projector_pair_ptr->set_up(proj_data_info_sptr, 
                                   target_sptr);
                                   
  // TODO check compatibility between symmetries for forward and backprojector
  this->symmetries_sptr.reset(
			      this->projector_pair_ptr->get_back_projector_sptr()->get_symmetries_used()->clone());

  if (is_null_ptr(this->normalisation_sptr))
  {
    error("Invalid normalisation object");
    return Succeeded::no;
  }

  if (this->normalisation_sptr->set_up(proj_data_sptr->get_exam_info_sptr(), proj_data_info_sptr) == Succeeded::no)
    return Succeeded::no;

  if (frame_num<=0)
    {
      error("frame_num should be >= 1");
      return Succeeded::no;
    }

  if (static_cast<unsigned>(frame_num)> frame_defs.get_num_frames())
    {
      error("frame_num is %d, but should be less than the number of frames %d.",
              frame_num, frame_defs.get_num_frames());
      return Succeeded::no;
    }

  return Succeeded::yes;
}

/***************************************************************
  functions that compute the value/gradient of the objective function etc
***************************************************************/

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
compute_sub_gradient_without_penalty_plus_sensitivity(TargetT& gradient, 
                                                      const TargetT &current_estimate, 
                                                      const int subset_num)
{
  assert(subset_num>=0);
  assert(subset_num<this->num_subsets);
  distributable_compute_gradient(this->projector_pair_ptr->get_forward_projector_sptr(), 
                                 this->projector_pair_ptr->get_back_projector_sptr(), 
                                 this->symmetries_sptr,
                                 gradient,
                                 current_estimate, 
                                 this->proj_data_sptr, 
                                 subset_num, 
                                 this->num_subsets, 
                                 -this->max_segment_num_to_process,
                                 this->max_segment_num_to_process, 
                                 this->zero_seg0_end_planes!=0, 
                                 NULL, 
                                 this->additive_proj_data_sptr 
                                 , caching_info_ptr
                                 );
  

}


template<typename TargetT>
double 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
actual_compute_objective_function_without_penalty(const TargetT& current_estimate,
                                                  const int subset_num)
{
  double accum=0.;  
  
  distributable_accumulate_loglikelihood(this->projector_pair_ptr->get_forward_projector_sptr(), 
                                         this->projector_pair_ptr->get_back_projector_sptr(), 
                                         this->symmetries_sptr,
                                         current_estimate,
                                         this->proj_data_sptr,
                                         subset_num, this->get_num_subsets(),
                                         -this->max_segment_num_to_process, 
                                         this->max_segment_num_to_process, 
                                         this->zero_seg0_end_planes != 0, &accum,
                                         this->additive_proj_data_sptr,
                                         this->normalisation_sptr, 
                                         this->get_time_frame_definitions().get_start_time(this->get_time_frame_num()),
                                         this->get_time_frame_definitions().get_end_time(this->get_time_frame_num()),
                                         this->caching_info_ptr
                                         );
                
    
  return accum;
}

#if 0
template<typename TargetT>
float 
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
sum_projection_data() const
{
  
  float counts=0.0F;
  
  for (int segment_num = -max_segment_num_to_process; segment_num <= max_segment_num_to_process; segment_num++)
  {
    for (int view_num = proj_data_sptr->get_min_view_num();
         view_num <= proj_data_sptr->get_max_view_num();
         ++view_num)
    {
      
      Viewgram<float>  viewgram=proj_data_sptr->get_viewgram(view_num,segment_num);
      
      //first adjust data
      
      // KT 05/07/2000 made parameters.zero_seg0_end_planes int
      if(segment_num==0 && zero_seg0_end_planes!=0)
      {
        viewgram[viewgram.get_min_axial_pos_num()].fill(0);
        viewgram[viewgram.get_max_axial_pos_num()].fill(0);
      } 
      
      truncate_rim(viewgram,rim_truncation_sino);
      
      //now take totals
      counts+=viewgram.sum();
    }
  }
  
  return counts;
  
}

#endif

template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
add_subset_sensitivity(TargetT& sensitivity, const int subset_num) const
{
  const int min_segment_num = -this->max_segment_num_to_process;
  const int max_segment_num = this->max_segment_num_to_process;

#if 1
     shared_ptr<TargetT> sensitivity_this_subset_sptr(sensitivity.clone());

     // have to create a ProjData object filled with 1 here because otherwise zero_seg0_endplanes will not be effective
     shared_ptr<ProjData> sens_proj_data_sptr(new ProjDataInMemory(
                                                  this->proj_data_sptr->get_exam_info_sptr(),
                                                  this->proj_data_sptr->get_proj_data_info_sptr()->create_shared_clone()));
     sens_proj_data_sptr->fill(1.0F);

     distributable_sensitivity_computation(this->projector_pair_ptr->get_forward_projector_sptr(), 
                                 this->projector_pair_ptr->get_back_projector_sptr(), 
                                 this->symmetries_sptr,
                                 *sensitivity_this_subset_sptr, 
                                 sensitivity, 
                                 sens_proj_data_sptr, 
                                 subset_num, 
                                 this->num_subsets, 
                                 min_segment_num,
                                 max_segment_num, 
                                 this->zero_seg0_end_planes!=0, 
                                 NULL, 
                                 this->additive_proj_data_sptr, 
                                 this->normalisation_sptr, 
                                 this->get_time_frame_definitions().get_start_time(this->get_time_frame_num()),
                                 this->get_time_frame_definitions().get_end_time(this->get_time_frame_num()),
                                 this->caching_info_ptr
                                 );
  std::transform(sensitivity.begin_all(), sensitivity.end_all(), 
                 sensitivity_this_subset_sptr->begin_all(), sensitivity.begin_all(), 
		 std::plus<typename TargetT::full_value_type>());
#else

  // warning: has to be same as subset scheme used as in distributable_computation
  for (int segment_num = min_segment_num; segment_num <= max_segment_num; ++segment_num)
  {
        //CPUTimer timer;
        //timer.start();
        
    for (int view = this->proj_data_sptr->get_min_view_num() + subset_num; 
        view <= this->proj_data_sptr->get_max_view_num(); 
        view += this->num_subsets)
    {
      const ViewSegmentNumbers view_segment_num(view, segment_num);
        
      if (!symmetries_sptr->is_basic(view_segment_num))
        continue;
      this->add_view_seg_to_sensitivity(sensitivity, view_segment_num);
    }
      //    cerr<<timer.value()<<endl;
  }
#endif
}

#if 0
template<typename TargetT>
void
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
add_view_seg_to_sensitivity(TargetT& sensitivity, const ViewSegmentNumbers& view_seg_nums) const
{
  RelatedViewgrams<float> viewgrams = 
    this->proj_data_sptr->get_empty_related_viewgrams(view_seg_nums,
                                                      this->symmetries_sptr);
  viewgrams.fill(1.F);
  // find efficiencies
  {      
    const double start_frame = this->frame_defs.get_start_time(this->frame_num);
    const double end_frame = this->frame_defs.get_end_time(this->frame_num);
    this->normalisation_sptr->undo(viewgrams,start_frame,end_frame);
  }
  // backproject
  {
    const int range_to_zero =
      view_seg_nums.segment_num() == 0 && this->zero_seg0_end_planes
      ? 1 : 0;
    const int min_ax_pos_num = 
      viewgrams.get_min_axial_pos_num() + range_to_zero;
    const int max_ax_pos_num = 
       viewgrams.get_max_axial_pos_num() - range_to_zero;

    this->projector_pair_ptr->get_back_projector_sptr()->
      back_project(sensitivity, viewgrams,
                   min_ax_pos_num, max_ax_pos_num);
  }
  
}
#endif

template<typename TargetT>
 std::unique_ptr<ExamInfo>
 PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
 get_exam_info_uptr_for_target()  const
{
     auto exam_info_uptr = this->get_exam_info_uptr_for_target();
     if (auto norm_ptr = dynamic_cast<BinNormalisationWithCalibration const * const>(get_normalisation_sptr().get()))
     {
       exam_info_uptr->set_calibration_factor(norm_ptr->get_calibration_factor());
       // somehow tell the image that it's calibrated 
     }
     else
     {
       exam_info_uptr->set_calibration_factor(-1.F);
       // somehow tell the image that it's not calibrated 
     }
    return exam_info_uptr;
}

template<typename TargetT>
Succeeded
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
actual_add_multiplication_with_approximate_sub_Hessian_without_penalty(TargetT& output,
                                                                       const TargetT& input,
                                                                       const int subset_num) const
{
  {
    std::string explanation;
    if (!input.has_same_characteristics(this->get_sensitivity(), 
                                        explanation))
      {
        error("PoissonLogLikelihoodWithLinearModelForMeanAndProjData:\n"
                "sensitivity and input for add_multiplication_with_approximate_Hessian_without_penalty\n"
                "should have the same characteristics.\n%s",
                explanation.c_str());
        return Succeeded::no;
      }
  }     

  shared_ptr<DataSymmetriesForViewSegmentNumbers> symmetries_sptr(
    this->get_projector_pair().get_symmetries_used()->clone());

  const double start_time =
    this->get_time_frame_definitions().get_start_time(this->get_time_frame_num());
  const double end_time =
    this->get_time_frame_definitions().get_end_time(this->get_time_frame_num());

  this->get_projector_pair().get_forward_projector_sptr()->set_input(input);
  this->get_projector_pair().get_back_projector_sptr()->start_accumulating_in_new_target();

  const std::vector<ViewSegmentNumbers> vs_nums_to_process =
    detail::find_basic_vs_nums_in_subset(* this->get_proj_data().get_proj_data_info_sptr(),
					 *symmetries_sptr,
					 -this->get_max_segment_num_to_process(),
					 this->get_max_segment_num_to_process(),
                                         subset_num, this->get_num_subsets());

  info("Forward projecting input image.", 2);
#ifdef STIR_OPENMP
#pragma omp parallel for schedule(runtime)
#endif
  // note: older versions of openmp need an int as loop
  for (int i=0; i<static_cast<int>(vs_nums_to_process.size()); ++i)
      {
#ifdef STIR_OPENMP
          const int thread_num = omp_get_thread_num();
          info(boost::format("Thread %d/%d calculating segment_num: %d, view_num: %d")
               % thread_num % omp_get_num_threads()
               % vs_nums_to_process[i].segment_num() % vs_nums_to_process[i].view_num(), 2);
#else
          info(boost::format("calculating segment_num: %d, view_num: %d")
               % vs_nums_to_process[i].segment_num() % vs_nums_to_process[i].view_num(), 2);
#endif
          const ViewSegmentNumbers view_segment_num=vs_nums_to_process[i];

          // first compute data-term: y*norm^2
          RelatedViewgrams<float> viewgrams =
            this->get_proj_data().get_related_viewgrams(view_segment_num, symmetries_sptr);
          // TODO add 1 for 1/(y+1) approximation

          this->get_normalisation().apply(viewgrams);

          // smooth TODO

          this->get_normalisation().apply(viewgrams);

          RelatedViewgrams<float> tmp_viewgrams;
          // set tmp_viewgrams to geometric forward projection of input
          {
            tmp_viewgrams = this->get_proj_data().get_empty_related_viewgrams(view_segment_num, symmetries_sptr);
            this->get_projector_pair().get_forward_projector_sptr()->
              forward_project(tmp_viewgrams);
          }
          
          // now divide by the data term
          {
            int tmp1=0, tmp2=0;// ignore counters returned by divide_and_truncate
            divide_and_truncate(tmp_viewgrams, viewgrams, 0, tmp1, tmp2);
          }

          // back-project
          this->get_projector_pair().get_back_projector_sptr()->
            back_project(tmp_viewgrams);

  } // end of loop over view/segments

  shared_ptr<TargetT> tmp(output.get_empty_copy());
  this->get_projector_pair().get_back_projector_sptr()->get_output(*tmp);
  // output += tmp;
  std::transform(output.begin_all(), output.end_all(),
                 tmp->begin_all(), output.begin_all(),
		 std::plus<typename TargetT::full_value_type>());

  return Succeeded::yes;
}


template<typename TargetT>
Succeeded
PoissonLogLikelihoodWithLinearModelForMeanAndProjData<TargetT>::
actual_accumulate_sub_Hessian_times_input_without_penalty(TargetT& output,
        const TargetT& current_image_estimate,
        const TargetT& input,
        const int subset_num) const
{
  { // check characteristics

    std::string explanation;
    if (!output.has_same_characteristics(this->get_sensitivity(),explanation))
    {
      error("PoissonLogLikelihoodWithLinearModelForMeanAndProjData:\n"
            "sensitivity and output for add_multiplication_with_approximate_Hessian_without_penalty\n"
            "should have the same characteristics.\n%s",
            explanation.c_str());
      return Succeeded::no;
    }

    if (!input.has_same_characteristics(this->get_sensitivity(),explanation))
    {
      error("PoissonLogLikelihoodWithLinearModelForMeanAndProjData:\n"
            "sensitivity and input for add_multiplication_with_approximate_Hessian_without_penalty\n"
            "should have the same characteristics.\n%s",
            explanation.c_str());
      return Succeeded::no;
    }

    if (!current_image_estimate.has_same_characteristics(this->get_sensitivity(),explanation))
    {
      error("PoissonLogLikelihoodWithLinearModelForMeanAndProjData:\n"
            "sensitivity and current_image_estimate for add_multiplication_with_approximate_Hessian_without_penalty\n"
            "should have the same characteristics.\n%s",
            explanation.c_str());
      return Succeeded::no;
    }
  }

  shared_ptr<DataSymmetriesForViewSegmentNumbers> symmetries_sptr(
          this->get_projector_pair().get_symmetries_used()->clone());

  this->get_projector_pair().get_forward_projector_sptr()->set_input(input);
  this->get_projector_pair().get_back_projector_sptr()->start_accumulating_in_new_target();

  const std::vector<ViewSegmentNumbers> vs_nums_to_process =
          detail::find_basic_vs_nums_in_subset(* this->get_proj_data().get_proj_data_info_sptr(),
                                               *symmetries_sptr,
                                               -this->get_max_segment_num_to_process(),
                                               this->get_max_segment_num_to_process(),
                                               subset_num, this->get_num_subsets());



  // Create and populate the input_viewgrams_vec with empty values.
  // This is needed to make the order of the vector correct w.r.t vs_nums_to_process.
  //OMP may mess this up
  // Try:  std::vector<RelatedViewgrams<float>> input_viewgrams_vec(vs_nums_to_process.size());
  std::vector<RelatedViewgrams<float>> input_viewgrams_vec;
  for (int i=0; i<static_cast<int>(vs_nums_to_process.size()); ++i)
  {
    const ViewSegmentNumbers view_segment_num = vs_nums_to_process[i];
    input_viewgrams_vec.push_back(this->get_proj_data().get_empty_related_viewgrams(view_segment_num, symmetries_sptr));
  }


  // Forward project input image
  info("Forward projecting input image.",2);
#ifdef STIR_OPENMP
#pragma omp parallel for schedule(runtime)
#endif
  for (int i=0; i<static_cast<int>(vs_nums_to_process.size()); ++i)
  {  // Loop over eah of the viewgrams in input_viewgrams_vec, forward projecting input into them
#ifdef STIR_OPENMP
    const int thread_num = omp_get_thread_num();
    info(boost::format("Thread %d/%d calculating segment_num: %d, view_num: %d")
         % thread_num % omp_get_num_threads()
         % vs_nums_to_process[i].segment_num() % vs_nums_to_process[i].view_num(), 2);
#else
    info(boost::format("calculating segment_num: %d, view_num: %d")
         % vs_nums_to_process[i].segment_num() % vs_nums_to_process[i].view_num(), 2);
#endif
    input_viewgrams_vec[i] = this->get_proj_data().get_empty_related_viewgrams(vs_nums_to_process[i], symmetries_sptr);
    this->get_projector_pair().get_forward_projector_sptr()->forward_project(input_viewgrams_vec[i]);
  }



  info("Forward projecting current image estimate and back projecting to output.", 2);
  this->get_projector_pair().get_forward_projector_sptr()->set_input(current_image_estimate);
#ifdef STIR_OPENMP
#pragma omp parallel for schedule(runtime)
#endif
  for (int i = 0; i < static_cast<int>(vs_nums_to_process.size()); ++i)
  {
#ifdef STIR_OPENMP
    const int thread_num = omp_get_thread_num();
    info(boost::format("Thread %d/%d calculating segment_num: %d, view_num: %d")
         % thread_num % omp_get_num_threads()
         % vs_nums_to_process[i].segment_num() % vs_nums_to_process[i].view_num(), 2);
#else
    info(boost::format("calculating segment_num: %d, view_num: %d")
         % vs_nums_to_process[i].segment_num() % vs_nums_to_process[i].view_num(), 2);
#endif
    // Compute ybar_sq_viewgram = [ F(current_image_est) + additive ]^2
    RelatedViewgrams<float> ybar_sq_viewgram;
    {
      ybar_sq_viewgram = this->get_proj_data().get_empty_related_viewgrams(vs_nums_to_process[i], symmetries_sptr);
      this->get_projector_pair().get_forward_projector_sptr()->forward_project(ybar_sq_viewgram);

      //add additive sinogram to forward projection
      if (!(is_null_ptr(this->get_additive_proj_data_sptr())))
        ybar_sq_viewgram += this->get_additive_proj_data().get_related_viewgrams(vs_nums_to_process[i], symmetries_sptr);
      // square ybar
      ybar_sq_viewgram *= ybar_sq_viewgram;
    }

    // Compute: final_viewgram * F(input) / ybar_sq_viewgram
    // final_viewgram starts as measured data
    RelatedViewgrams<float> final_viewgram = this->get_proj_data().get_related_viewgrams(vs_nums_to_process[i], symmetries_sptr);
    {
      // Mult input_viewgram
      final_viewgram *= input_viewgrams_vec[i];
      int tmp1 = 0, tmp2 = 0;// ignore counters returned by divide_and_truncate
      // Divide final_viewgeam by ybar_sq_viewgram
      divide_and_truncate(final_viewgram, ybar_sq_viewgram, 0, tmp1, tmp2);
    }

    // back-project final_viewgram
    this->get_projector_pair().get_back_projector_sptr()->
            back_project(final_viewgram);

  } // end of loop over view/segments

  shared_ptr<TargetT> tmp(output.get_empty_copy());
  this->get_projector_pair().get_back_projector_sptr()->get_output(*tmp);
  // output += tmp;
  std::transform(output.begin_all(), output.end_all(),
                 tmp->begin_all(), output.begin_all(),
                 std::plus<typename TargetT::full_value_type>());

  return Succeeded::yes;
}

/*********************** distributable_* ***************************/
// TODO all this stuff is specific to DiscretisedDensity, so wouldn't work for TargetT

#ifdef STIR_MPI
// make call-backs public for the moment

//! Call-back function for compute_gradient
RPC_process_related_viewgrams_type RPC_process_related_viewgrams_gradient;

//! Call-back function for accumulate_loglikelihood
RPC_process_related_viewgrams_type RPC_process_related_viewgrams_accumulate_loglikelihood;

//! Call-back function for sensitivity_computation
RPC_process_related_viewgrams_type RPC_process_related_viewgrams_sensitivity_computation;

#else 
//! Call-back function for compute_gradient
static RPC_process_related_viewgrams_type RPC_process_related_viewgrams_gradient;

//! Call-back function for accumulate_loglikelihood
static RPC_process_related_viewgrams_type RPC_process_related_viewgrams_accumulate_loglikelihood;

//! Call-back function for sensitivity_computation
static RPC_process_related_viewgrams_type RPC_process_related_viewgrams_sensitivity_computation;
#endif

void distributable_compute_gradient(const shared_ptr<ForwardProjectorByBin>& forward_projector_sptr,
                                    const shared_ptr<BackProjectorByBin>& back_projector_sptr,
                                    const shared_ptr<DataSymmetriesForViewSegmentNumbers>& symmetries_sptr,
                                    DiscretisedDensity<3,float>& output_image,
                                    const DiscretisedDensity<3,float>& input_image,
                                    const shared_ptr<ProjData>& proj_dat,
                                    int subset_num, int num_subsets,
                                    int min_segment, int max_segment,
                                    bool zero_seg0_end_planes,
                                    double* log_likelihood_ptr,
                                    shared_ptr<ProjData> const& additive_binwise_correction,
                                    DistributedCachingInformation* caching_info_ptr
                                    )
{
        
    distributable_computation(forward_projector_sptr,
                              back_projector_sptr,
                              symmetries_sptr,
                              &output_image, &input_image,
                              proj_dat, true, //i.e. do read projection data
                              subset_num, num_subsets,
                              min_segment, max_segment,
                              zero_seg0_end_planes,
                              log_likelihood_ptr,
                              additive_binwise_correction,
                              /* normalisation info to be ignored */ shared_ptr<BinNormalisation>(), 0., 0.,
                              &RPC_process_related_viewgrams_gradient,
                              caching_info_ptr
                              );
}


void distributable_accumulate_loglikelihood(
                                            const shared_ptr<ForwardProjectorByBin>& forward_projector_sptr,
                                            const shared_ptr<BackProjectorByBin>& back_projector_sptr,
                                            const shared_ptr<DataSymmetriesForViewSegmentNumbers>& symmetries_sptr,
                                            const DiscretisedDensity<3,float>& input_image,
                                            const shared_ptr<ProjData>& proj_dat,
                                            int subset_num, int num_subsets,
                                            int min_segment, int max_segment,
                                            bool zero_seg0_end_planes,
                                            double* log_likelihood_ptr,
                                            shared_ptr<ProjData> const& additive_binwise_correction,
                                            shared_ptr<BinNormalisation> const& normalisation_sptr,
                                            const double start_time_of_frame,
                                            const double end_time_of_frame,
                                            DistributedCachingInformation* caching_info_ptr
                                            )
                                            
{
          distributable_computation(forward_projector_sptr,
                                    back_projector_sptr,
                                    symmetries_sptr,
                                    NULL, &input_image, 
                                    proj_dat, true, //i.e. do read projection data
                                    subset_num, num_subsets,
                                    min_segment, max_segment,
                                    zero_seg0_end_planes,
                                    log_likelihood_ptr,
                                    additive_binwise_correction,
                                    normalisation_sptr,
                                    start_time_of_frame,
                                    end_time_of_frame,
                                    &RPC_process_related_viewgrams_accumulate_loglikelihood,
                                    caching_info_ptr
                                    );
}

void distributable_sensitivity_computation(
                                            const shared_ptr<ForwardProjectorByBin>& forward_projector_sptr,
                                            const shared_ptr<BackProjectorByBin>& back_projector_sptr,
                                            const shared_ptr<DataSymmetriesForViewSegmentNumbers>& symmetries_sptr,
                                            DiscretisedDensity<3,float>& sensitivity,
                                            const DiscretisedDensity<3,float>& input_image,
                                            const shared_ptr<ProjData>& proj_dat,
                                            int subset_num, int num_subsets,
                                            int min_segment, int max_segment,
                                            bool zero_seg0_end_planes,
                                            double* log_likelihood_ptr,
                                            shared_ptr<ProjData> const& additive_binwise_correction,
                                            shared_ptr<BinNormalisation> const& normalisation_sptr,
                                            const double start_time_of_frame,
                                            const double end_time_of_frame,
                                            DistributedCachingInformation* caching_info_ptr
                                            )

{
          distributable_computation(forward_projector_sptr,
                                    back_projector_sptr,
                                    symmetries_sptr,
                                    &sensitivity, &input_image,
                                    proj_dat, true, //i.e. do read projection data
                                    subset_num, num_subsets,
                                    min_segment, max_segment,
                                    zero_seg0_end_planes,
                                    log_likelihood_ptr,
                                    additive_binwise_correction,
                                    normalisation_sptr,
                                    start_time_of_frame,
                                    end_time_of_frame,
                                    &RPC_process_related_viewgrams_sensitivity_computation,
                                    caching_info_ptr
                                    );

}


//////////// RPC functions


void RPC_process_related_viewgrams_gradient(
                                            const shared_ptr<ForwardProjectorByBin>& forward_projector_sptr,
                                            const shared_ptr<BackProjectorByBin>& back_projector_sptr,
                                            RelatedViewgrams<float>* measured_viewgrams_ptr,
                                            int& count, int& count2, double* log_likelihood_ptr /* = NULL */,
                                            const RelatedViewgrams<float>* additive_binwise_correction_ptr,
                                            const RelatedViewgrams<float>* mult_viewgrams_ptr)
{       
  assert(measured_viewgrams_ptr != NULL);
  if (!is_null_ptr(mult_viewgrams_ptr))
    error("Internal error: mult_viewgrams_ptr should be zero when computing gradient");

  RelatedViewgrams<float> estimated_viewgrams = measured_viewgrams_ptr->get_empty_copy();
  
  /*if (distributed::first_iteration) 
    {
        stir::RelatedViewgrams<float>::iterator viewgrams_iter = measured_viewgrams_ptr->begin();
                stir::RelatedViewgrams<float>::iterator viewgrams_end = measured_viewgrams_ptr->end();
                while (viewgrams_iter!= viewgrams_end)
                {
                        printf("\nSLAVE VIEWGRAM\n");
                        int pos=0;
                        for ( int tang_pos = -144 ;tang_pos  <= 143 ;++tang_pos)  
                        for ( int ax_pos = 0; ax_pos <= 62 ;++ax_pos)
                        { 
                                        if (pos>3616 && pos <3632) printf("%f, ",(*viewgrams_iter)[ax_pos][tang_pos]);
                                        pos++;
                        }
                        viewgrams_iter++;
                }
    }
*/
  forward_projector_sptr->forward_project(estimated_viewgrams);
        
        
        
  if (additive_binwise_correction_ptr != NULL)
  {
    estimated_viewgrams += (*additive_binwise_correction_ptr);
  }
  

    



  // for sinogram division
      
  divide_and_truncate(*measured_viewgrams_ptr, estimated_viewgrams, rim_truncation_sino, count, count2, log_likelihood_ptr);
      
  back_projector_sptr->back_project(*measured_viewgrams_ptr);
};      


void RPC_process_related_viewgrams_accumulate_loglikelihood(
                                                            const shared_ptr<ForwardProjectorByBin>& forward_projector_sptr,
                                                            const shared_ptr<BackProjectorByBin>& back_projector_sptr,
                                                            RelatedViewgrams<float>* measured_viewgrams_ptr,
                                                            int& count, int& count2, double* log_likelihood_ptr,
                                                            const RelatedViewgrams<float>* additive_binwise_correction_ptr,
                                                            const RelatedViewgrams<float>* mult_viewgrams_ptr)
{
  assert(measured_viewgrams_ptr != NULL);
  assert(log_likelihood_ptr != NULL);

  RelatedViewgrams<float> estimated_viewgrams = measured_viewgrams_ptr->get_empty_copy();

  forward_projector_sptr->forward_project(estimated_viewgrams);
  
  if (additive_binwise_correction_ptr != NULL)
  {
    estimated_viewgrams += (*additive_binwise_correction_ptr);
  };
  
  if (mult_viewgrams_ptr != NULL)
  {
    estimated_viewgrams *= (*mult_viewgrams_ptr);
  }

  RelatedViewgrams<float>::iterator meas_viewgrams_iter = 
          measured_viewgrams_ptr->begin();
  RelatedViewgrams<float>::const_iterator est_viewgrams_iter = 
          estimated_viewgrams.begin();
  // call function that does the actual work, it sits in recon_array_funtions.cxx (TODO)
  for (;
       meas_viewgrams_iter != measured_viewgrams_ptr->end();
       ++meas_viewgrams_iter, ++est_viewgrams_iter)
    accumulate_loglikelihood(*meas_viewgrams_iter, 
                             *est_viewgrams_iter, 
                             rim_truncation_sino, log_likelihood_ptr);
};      

void RPC_process_related_viewgrams_sensitivity_computation(
                                                            const shared_ptr<ForwardProjectorByBin>& forward_projector_sptr,
                                                            const shared_ptr<BackProjectorByBin>& back_projector_sptr,
                                                            RelatedViewgrams<float>* measured_viewgrams_ptr,
                                                            int& count, int& count2, double* log_likelihood_ptr,
                                                            const RelatedViewgrams<float>* additive_binwise_correction_ptr,
                                                            const RelatedViewgrams<float>* mult_viewgrams_ptr)
{
  assert(measured_viewgrams_ptr != NULL);

  if( mult_viewgrams_ptr )
  {
    back_projector_sptr->back_project(*mult_viewgrams_ptr);
  }
  else
  {  
    back_projector_sptr->back_project(*measured_viewgrams_ptr);
  }

}

#  ifdef _MSC_VER
// prevent warning message on instantiation of abstract class 
#  pragma warning(disable:4661)
#  endif

template class PoissonLogLikelihoodWithLinearModelForMeanAndProjData<DiscretisedDensity<3,float> >;

END_NAMESPACE_STIR
