/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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
#include "src/runtime/kernel/arm/int8/layer_norm_int8.h"
#include "src/runtime/runtime_api.h"

using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_LayerNorm;

namespace mindspore::kernel {
void LayerNormInt8CPUKernel::SetQuantArgs() {
  lite::Tensor *input = in_tensors_.at(0);
  lite::Tensor *output = out_tensors_.at(0);

  quant_param_.in_quant_arg_.zp_ = input->quant_params().front().zeroPoint;
  quant_param_.in_quant_arg_.scale_ = input->quant_params().front().scale;
  quant_param_.out_quant_arg_.zp_ = output->quant_params().front().zeroPoint;
  quant_param_.out_quant_arg_.scale_ = output->quant_params().front().scale;

  quant_param_.output_activation_min_ = std::numeric_limits<int8_t>::min();
  quant_param_.output_activation_max_ = std::numeric_limits<int8_t>::max();

  if (param_->elementwise_affine_) {
    lite::Tensor *gamma_tensor = out_tensors_.at(1);
    quant_param_.gamma_quant_arg_.zp_ = gamma_tensor->quant_params().front().zeroPoint;
    quant_param_.gamma_quant_arg_.scale_ = gamma_tensor->quant_params().front().scale;
  }

  double in_scale;
  if (param_->elementwise_affine_) {
    in_scale = static_cast<double>(quant_param_.in_quant_arg_.scale_ * quant_param_.gamma_quant_arg_.scale_);
  } else {
    in_scale = static_cast<double>(quant_param_.in_quant_arg_.scale_);
  }
  double real_multiplier = in_scale / static_cast<double>(quant_param_.out_quant_arg_.scale_);

  QuantizeRoundParameter(real_multiplier, &quant_param_.multiplier_, &quant_param_.shift_left_,
                         &quant_param_.shift_right_);
  return;
}

int LayerNormInt8CPUKernel::Init() {
  SetQuantArgs();

  if (!InferShapeDone()) {
    return RET_OK;
  }
  return ReSize();
}

int LayerNormInt8CPUKernel::ReSize() {
  auto shape = in_tensors_.front()->shape();
  outer_size_ = 1;
  inner_size_ = 1;
  for (size_t i = 0; i < shape.size(); ++i) {
    if (i + param_->normalized_dims_ < shape.size()) {
      outer_size_ *= shape.at(i);
    } else {
      inner_size_ *= shape.at(i);
    }
  }

  param_->thread_count_ = MSMIN(outer_size_, op_parameter_->thread_num_);
  param_->thread_outsize_ = UP_DIV(outer_size_, param_->thread_count_);
  return RET_OK;
}

int LayerNormInt8Run(void *cdata, int task_id) {
  auto kernel = reinterpret_cast<LayerNormInt8CPUKernel *>(cdata);
  kernel->DoExecute(task_id);
  return RET_OK;
}

int LayerNormInt8CPUKernel::DoExecute(int task_id) {
  int current_out_size = outer_size_ - task_id * param_->thread_outsize_;
  current_out_size = MSMIN(current_out_size, param_->thread_outsize_);
  if (current_out_size <= 0) {
    return RET_OK;
  }

  const int8_t *thread_src = src_ptr_ + task_id * param_->thread_outsize_ * inner_size_;
  MS_ASSERT(thread_src);
  int8_t *thread_dst = dst_ptr_ + task_id * param_->thread_outsize_ * inner_size_;
  MS_ASSERT(thread_dst);

  LayerNormInt8(thread_src, gamma_ptr_, beta_ptr_, thread_dst, param_->elementwise_affine_, current_out_size,
                inner_size_, &quant_param_);
  return RET_OK;
}

int LayerNormInt8CPUKernel::Run() {
  src_ptr_ = reinterpret_cast<int8_t *>(in_tensors_.at(0)->MutableData());
  dst_ptr_ = reinterpret_cast<int8_t *>(out_tensors_.at(0)->MutableData());
  if (param_->elementwise_affine_) {
    gamma_ptr_ = reinterpret_cast<int8_t *>(in_tensors_.at(1)->MutableData());
    MS_ASSERT(gamma_ptr_);
    beta_ptr_ = reinterpret_cast<int32_t *>(in_tensors_.at(2)->MutableData());
    MS_ASSERT(beta_ptr_);
  }

  auto ret = ParallelLaunch(this->context_->thread_pool_, LayerNormInt8Run, this, op_parameter_->thread_num_);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "LayerNormInt8Run error error_code[" << ret << "]";
    return ret;
  }
  return RET_OK;
}

kernel::LiteKernel *CpuLayerNormInt8KernelCreator(const std::vector<lite::Tensor *> &inputs,
                                                  const std::vector<lite::Tensor *> &outputs, OpParameter *parameter,
                                                  const lite::InnerContext *ctx, const KernelKey &desc,
                                                  const mindspore::lite::PrimitiveC *primitive) {
  auto *kernel = new (std::nothrow) LayerNormInt8CPUKernel(parameter, inputs, outputs, ctx, primitive);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "kernel is nullptr.";
    free(parameter);
    return nullptr;
  }
  auto ret = kernel->Init();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Init kernel failed, name: " << parameter->name_
                  << ", type: " << schema::EnumNamePrimitiveType(static_cast<schema::PrimitiveType>(parameter->type_));
    delete kernel;
    return nullptr;
  }
  return kernel;
}

REG_KERNEL(kCPU, kNumberTypeInt8, PrimitiveType_LayerNorm, CpuLayerNormInt8KernelCreator)
}  // namespace mindspore::kernel
