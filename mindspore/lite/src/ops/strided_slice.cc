/**
 * Copyright 2019-2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/ops/strided_slice.h"

#ifndef PRIMITIVE_WRITEABLE
#include "src/ops/ops_register.h"
#endif

namespace mindspore {
namespace lite {
#ifdef PRIMITIVE_WRITEABLE
int StridedSlice::GetBeginMask() const { return this->primitive_->value.AsStridedSlice()->beginMask; }
int StridedSlice::GetEndMask() const { return this->primitive_->value.AsStridedSlice()->endMask; }
int StridedSlice::GetEllipsisMask() const { return this->primitive_->value.AsStridedSlice()->ellipsisMask; }
int StridedSlice::GetNewAxisMask() const { return this->primitive_->value.AsStridedSlice()->newAxisMask; }
int StridedSlice::GetShrinkAxisMask() const { return this->primitive_->value.AsStridedSlice()->shrinkAxisMask; }
std::vector<int> StridedSlice::GetBegin() const { return this->primitive_->value.AsStridedSlice()->begin; }
std::vector<int> StridedSlice::GetEnd() const { return this->primitive_->value.AsStridedSlice()->end; }
std::vector<int> StridedSlice::GetStride() const { return this->primitive_->value.AsStridedSlice()->stride; }
std::vector<int> StridedSlice::GetIsScale() const { return this->primitive_->value.AsStridedSlice()->isScale; }

void StridedSlice::SetBeginMask(int begin_mask) { this->primitive_->value.AsStridedSlice()->beginMask = begin_mask; }
void StridedSlice::SetEndMask(int end_mask) { this->primitive_->value.AsStridedSlice()->endMask = end_mask; }
void StridedSlice::SetEllipsisMask(int ellipsis_mask) {
  this->primitive_->value.AsStridedSlice()->ellipsisMask = ellipsis_mask;
}
void StridedSlice::SetNewAxisMask(int new_axis_mask) {
  this->primitive_->value.AsStridedSlice()->newAxisMask = new_axis_mask;
}
void StridedSlice::SetShrinkAxisMask(int shrink_axis_mask) {
  this->primitive_->value.AsStridedSlice()->shrinkAxisMask = shrink_axis_mask;
}
void StridedSlice::SetBegin(const std::vector<int> &begin) { this->primitive_->value.AsStridedSlice()->begin = begin; }
void StridedSlice::SetEnd(const std::vector<int> &end) { this->primitive_->value.AsStridedSlice()->end = end; }
void StridedSlice::SetStride(const std::vector<int> &stride) {
  this->primitive_->value.AsStridedSlice()->stride = stride;
}
void StridedSlice::SetIsScale(const std::vector<int> &is_scale) {
  this->primitive_->value.AsStridedSlice()->isScale = is_scale;
}

int StridedSlice::UnPackAttr(const Primitive &prim, const std::vector<AnfNodePtr> &inputs) {
  if (this->primitive_ == nullptr) {
    this->primitive_ = new (std::nothrow) schema::PrimitiveT;
    if (this->primitive_ == nullptr) {
      MS_LOG(ERROR) << "new primitiveT failed";
      return RET_ERROR;
    }
    this->primitive_->value.type = schema::PrimitiveType_StridedSlice;
  }
  if (this->primitive_->value.type != schema::PrimitiveType_StridedSlice) {
    MS_LOG(ERROR) << "primitive_ type is error:" << this->primitive_->value.type;
    return RET_ERROR;
  }
  if (this->primitive_->value.value == nullptr) {
    auto attr = new (std::nothrow) schema::StridedSliceT();
    if (attr == nullptr) {
      MS_LOG(ERROR) << "new StridedSlice failed";
      return RET_ERROR;
    }
    attr->beginMask = GetValue<int>(prim.GetAttr("begin_mask"));
    attr->endMask = GetValue<int>(prim.GetAttr("end_mask"));
    attr->ellipsisMask = GetValue<int>(prim.GetAttr("ellipsis_mask"));
    attr->newAxisMask = GetValue<int>(prim.GetAttr("new_axis_mask"));
    attr->shrinkAxisMask = GetValue<int>(prim.GetAttr("shrink_axis_mask"));
    auto inputNodeFirst = inputs[kAnfPopulaterInputNumOne];
    std::vector<int> beginVec;
    GetAttrDataFromInput(inputNodeFirst, &beginVec);
    attr->begin = beginVec;

    auto inputNodeSecond = inputs[kAnfPopulaterInputNumTwo];
    std::vector<int> endVec;
    GetAttrDataFromInput(inputNodeSecond, &endVec);
    attr->end = endVec;

    auto inputNodeThird = inputs[kAnfPopulaterInputNumThree];
    std::vector<int> strideVec;
    GetAttrDataFromInput(inputNodeThird, &strideVec);
    attr->stride = strideVec;
    this->primitive_->value.value = attr;
    if (this->primitive_->value.value == nullptr) {
      MS_LOG(ERROR) << "new primitiveT value failed";
      return RET_ERROR;
    }
  }
  return RET_OK;
}

#else

int StridedSlice::GetBeginMask() const { return this->primitive_->value_as_StridedSlice()->beginMask(); }
int StridedSlice::GetEndMask() const { return this->primitive_->value_as_StridedSlice()->endMask(); }
int StridedSlice::GetEllipsisMask() const { return this->primitive_->value_as_StridedSlice()->ellipsisMask(); }
int StridedSlice::GetNewAxisMask() const { return this->primitive_->value_as_StridedSlice()->newAxisMask(); }
int StridedSlice::GetShrinkAxisMask() const { return this->primitive_->value_as_StridedSlice()->shrinkAxisMask(); }
std::vector<int> StridedSlice::GetBegin() const {
  auto fb_vector = this->primitive_->value_as_StridedSlice()->begin();
  return std::vector<int>(fb_vector->begin(), fb_vector->end());
}
std::vector<int> StridedSlice::GetEnd() const {
  auto fb_vector = this->primitive_->value_as_StridedSlice()->end();
  return std::vector<int>(fb_vector->begin(), fb_vector->end());
}
std::vector<int> StridedSlice::GetStride() const {
  auto fb_vector = this->primitive_->value_as_StridedSlice()->stride();
  return std::vector<int>(fb_vector->begin(), fb_vector->end());
}
std::vector<int> StridedSlice::GetIsScale() const {
  auto fb_vector = this->primitive_->value_as_StridedSlice()->isScale();
  return std::vector<int>(fb_vector->begin(), fb_vector->end());
}
int StridedSlice::UnPackToFlatBuilder(const schema::Primitive *primitive, flatbuffers::FlatBufferBuilder *fbb) {
  MS_ASSERT(nullptr != primitive);
  MS_ASSERT(nullptr != fbb);
  auto attr = primitive->value_as_StridedSlice();
  if (attr == nullptr) {
    MS_LOG(ERROR) << "value_as_StridedSlice return nullptr";
    return RET_ERROR;
  }
  std::vector<int32_t> begin;
  if (attr->begin() != nullptr) {
    for (int i = 0; i < static_cast<int>(attr->begin()->size()); i++) {
      begin.push_back(attr->begin()->data()[i]);
    }
  }
  std::vector<int32_t> end;
  if (attr->end() != nullptr) {
    for (int i = 0; i < static_cast<int>(attr->end()->size()); i++) {
      end.push_back(attr->end()->data()[i]);
    }
  }
  std::vector<int32_t> stride;
  if (attr->stride() != nullptr) {
    for (int i = 0; i < static_cast<int>(attr->stride()->size()); i++) {
      stride.push_back(attr->stride()->data()[i]);
    }
  }
  std::vector<int32_t> isScale;
  if (attr->isScale() != nullptr) {
    for (int i = 0; i < static_cast<int>(attr->isScale()->size()); i++) {
      isScale.push_back(attr->isScale()->data()[i]);
    }
  }
  auto val_offset =
    schema::CreateStridedSliceDirect(*fbb, attr->beginMask(), attr->endMask(), attr->ellipsisMask(),
                                     attr->newAxisMask(), attr->shrinkAxisMask(), &begin, &end, &stride, &isScale);
  auto prim_offset = schema::CreatePrimitive(*fbb, schema::PrimitiveType_StridedSlice, val_offset.o);
  fbb->Finish(prim_offset);
  return RET_OK;
}

PrimitiveC *StridedSliceCreator(const schema::Primitive *primitive) {
  return PrimitiveC::NewPrimitiveC<StridedSlice>(primitive);
}
Registry StridedSliceRegistry(schema::PrimitiveType_StridedSlice, StridedSliceCreator);
#endif

namespace {
constexpr size_t kStridedSliceOutputNum = 1;
constexpr size_t kStridedSliceInputNum = 1;
constexpr size_t kStridedSliceMultiInputNum = 4;
}  // namespace

void StridedSlice::ApplyNewAxisMask() {
  for (size_t i = 0; i < new_axis_mask_.size(); i++) {
    if (new_axis_mask_.at(i)) {
      ndim_ += 1;
      in_shape_.insert(in_shape_.begin() + i, 1);
      begins_.at(i) = 0;
      ends_.at(i) = 1;
      strides_.at(i) = 1;

      begins_.emplace_back(0);
      ends_.emplace_back(in_shape_.at(ndim_ - 1));
      strides_.emplace_back(1);

      begins_mask_.at(i) = false;
      ends_mask_.at(i) = false;
      ellipsis_mask_.at(i) = false;
      shrink_axis_mask_.at(i) = false;
    }
  }
}

std::vector<int> StridedSlice::ApplyShrinkMask(std::vector<int> out_shape) {
  auto old_out_shape = out_shape;
  out_shape.clear();
  for (size_t i = 0; i < shrink_axis_mask_.size(); i++) {
    if (shrink_axis_mask_.at(i)) {
      ends_.at(i) = begins_.at(i) + 1;
      strides_.at(i) = 1;
    } else {
      out_shape.emplace_back(old_out_shape.at(i));
    }
  }
  for (size_t i = shrink_axis_mask_.size(); i < old_out_shape.size(); i++) {
    out_shape.emplace_back(old_out_shape.at(i));
  }
  return out_shape;
}

/*only one bit will be used if multiple bits are true.*/
void StridedSlice::ApplyEllipsisMask() {
  for (size_t i = 0; i < ellipsis_mask_.size(); i++) {
    if (ellipsis_mask_.at(i)) {
      begins_.at(i) = 0;
      ends_.at(i) = in_shape_.at(i);
      break;
    }
  }
}

void StridedSlice::ApplyBeginMask() {
  for (int i = 0; i < ndim_; i++) {
    if (begins_mask_.at(i)) {
      begins_.at(i) = 0;
    }
  }
}

void StridedSlice::ApplyEndMask() {
  for (int i = 0; i < ndim_; i++) {
    if (ends_mask_.at(i)) {
      ends_.at(i) = in_shape_.at(i);
    }
  }
}

void StridedSlice::TransIndexToPositive() {
  for (int i = 0; i < static_cast<int>(begins_.size()); ++i) {
    if (begins_.at(i) < 0) {
      begins_.at(i) += in_shape_.at(i);
    }
    if (ends_.at(i) < 0) {
      ends_.at(i) += in_shape_.at(i);
    }
  }
}

int StridedSlice::InferShape(std::vector<lite::Tensor *> inputs, std::vector<lite::Tensor *> outputs) {
  MS_ASSERT(this->primitive_ != nullptr);
  if (outputs.size() != kStridedSliceOutputNum) {
    MS_LOG(ERROR) << "Invalid output size:" << outputs.size();
    return RET_PARAM_INVALID;
  }
  if (inputs.size() != kStridedSliceInputNum && inputs.size() != kStridedSliceMultiInputNum) {
    MS_LOG(ERROR) << "Invalid input size " << inputs.size();
    return RET_PARAM_INVALID;
  }
  auto input = inputs.at(0);
  outputs.front()->set_data_type(input->data_type());
  outputs.at(0)->set_format(input->format());
  MS_ASSERT(input != nullptr);
  auto input_shape = input->shape();
  auto inferflag = infer_flag();

  if (inputs.size() == kStridedSliceInputNum) {
    ndim_ = static_cast<int>(GetBegin().size());

    for (int i = 0; i < ndim_; i++) {
      if (inferflag) {
        in_shape_.emplace_back(input_shape.at(i));
      }
      begins_.emplace_back((GetBegin()).at(i));
      ends_.emplace_back((GetEnd()).at(i));
      strides_.emplace_back((GetStride()).at(i));
    }
  } else {
    auto begin_tensor = inputs.at(1);
    int *begin_data = reinterpret_cast<int *>(begin_tensor->MutableData());
    auto end_tensor = inputs.at(2);
    int *end_data = reinterpret_cast<int *>(end_tensor->MutableData());
    auto stride_tensor = inputs.at(3);
    int *stride_data = reinterpret_cast<int *>(stride_tensor->MutableData());
    if (begin_data == nullptr || end_data == nullptr || stride_data == nullptr) {
      return RET_INFER_ERR;
    }
    ndim_ = begin_tensor->ElementsNum();
    for (int i = 0; i < ndim_; ++i) {
      if (inferflag) {
        in_shape_.emplace_back(input_shape.at(i));
      }
      begins_.emplace_back(begin_data[i]);
      ends_.emplace_back(end_data[i]);
      strides_.emplace_back(stride_data[i]);
    }
  }

  // set all mask to original input shape
  begins_mask_.resize(ndim_);
  ends_mask_.resize(ndim_);
  ellipsis_mask_.resize(ndim_);
  new_axis_mask_.resize(ndim_);
  shrink_axis_mask_.resize(ndim_);

  //   convert bit to vector
  for (int i = 0; i < ndim_; i++) {
    begins_mask_.at(i) = static_cast<uint32_t>(GetBeginMask()) & (1 << i);
    ends_mask_.at(i) = static_cast<uint32_t>(GetEndMask()) & (1 << i);
    ellipsis_mask_.at(i) = static_cast<uint32_t>(GetEllipsisMask()) & (1 << i);
    new_axis_mask_.at(i) = static_cast<uint32_t>(GetNewAxisMask()) & (1 << i);
    shrink_axis_mask_.at(i) = static_cast<uint32_t>(GetShrinkAxisMask()) & (1 << i);
  }

  ApplyNewAxisMask();
  ApplyBeginMask();
  ApplyEndMask();
  ApplyEllipsisMask();

  if (!inferflag) {
    return RET_OK;
  }
  std::vector<int> output_shape;
  output_shape.clear();
  output_shape.resize(in_shape_.size());

  TransIndexToPositive();
  for (int i = 0; i < static_cast<int>(in_shape_.size()); i++) {
    if (i < ndim_ && new_axis_mask_.at(i)) {
      output_shape.at(i) = 1;
    } else {
      output_shape.at(i) = (ends_.at(i) - begins_.at(i)) / strides_.at(i);
    }
  }

  output_shape = ApplyShrinkMask(output_shape);

  outputs.front()->set_shape(output_shape);

  return RET_OK;
}
}  // namespace lite
}  // namespace mindspore
