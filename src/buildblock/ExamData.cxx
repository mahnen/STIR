/*
    Copyright (C) 2016, University College London
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
  \ingroup buildblock
  \brief implementation of stir::ExamData

  \author Nikos Efthimiou
*/
#include "stir/ExamData.h"


#include <iostream>
START_NAMESPACE_STIR

ExamData::
ExamData():
    exam_info_sptr(new ExamInfo())
{}


ExamData::ExamData(const shared_ptr<const ExamInfo> &_this_exam) :
    exam_info_sptr(_this_exam)
{}


ExamData::~ExamData()
{}

void
ExamData::set_exam_info(ExamInfo const& new_exam_info)
{
  this->exam_info_sptr.reset(new ExamInfo(new_exam_info));
}

void
ExamData::set_exam_info_sptr(shared_ptr<const ExamInfo>  new_exam_info_sptr)
{
  this->exam_info_sptr=new_exam_info_sptr;
}

const ExamInfo&
ExamData::get_exam_info() const
{
  return *exam_info_sptr.get();
}

shared_ptr<const ExamInfo>
ExamData::get_exam_info_sptr() const
{
  return exam_info_sptr;
}

END_NAMESPACE_STIR
