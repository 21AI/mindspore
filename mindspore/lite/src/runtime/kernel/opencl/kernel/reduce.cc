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

#include <set>
#include <string>
#include <map>
#include "include/errorcode.h"
#include "src/kernel_registry.h"
#include "src/runtime/kernel/opencl/kernel/reduce.h"
#include "src/runtime/kernel/opencl/cl/reduce.cl.inc"

using mindspore::kernel::KERNEL_ARCH::kGPU;
using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_NULL_PTR;
using mindspore::lite::RET_OK;
using mindspore::lite::RET_PARAM_INVALID;
using mindspore::schema::PrimitiveType_Mean;
using mindspore::schema::PrimitiveType_Reduce;
using mindspore::schema::ReduceMode;
using mindspore::schema::ReduceMode_ReduceMax;
using mindspore::schema::ReduceMode_ReduceMean;
using mindspore::schema::ReduceMode_ReduceMin;
using mindspore::schema::ReduceMode_ReduceProd;
using mindspore::schema::ReduceMode_ReduceSum;
using mindspore::schema::ReduceMode_ReduceSumSquare;

namespace mindspore::kernel {

int ReduceOpenCLKernel::Init() {
  if (in_tensors_.size() != 1 || out_tensors_.size() != 1) {
    MS_LOG(ERROR) << "Invalid input size: " << in_tensors_.size() << ", output size: " << out_tensors_.size();
    return RET_ERROR;
  }
  InitNHWCShape();
  auto reduce_param = reinterpret_cast<ReduceParameter *>(op_parameter_);
  if (reduce_param == nullptr) {
    return RET_NULL_PTR;
  }
  std::map<int, std::string> reduce_type2str{{ReduceMode_ReduceMean, "mean"}, {ReduceMode_ReduceSum, "sum"}};
  if (reduce_type2str.find(reduce_param->mode_) == reduce_type2str.end()) {
    MS_LOG(ERROR) << "not supported reduce type:" << reduce_param->mode_;
    return RET_PARAM_INVALID;
  }
  if (reduce_param->num_axes_ != 2) {
    MS_LOG(ERROR) << "reduce op only support axes=2";
    return RET_PARAM_INVALID;
  }
  bool hw_reduce = (reduce_param->axes_[0] == 1 && reduce_param->axes_[1] == 2) ||
                   (reduce_param->axes_[0] == 2 && reduce_param->axes_[1] == 1);
  wc_reduce_ = (reduce_param->axes_[0] == 2 && reduce_param->axes_[1] == 3) ||
               (reduce_param->axes_[0] == 3 && reduce_param->axes_[1] == 2);
  if (!hw_reduce && !wc_reduce_) {
    MS_LOG(ERROR) << "reduce op only support axis (1,2) or (2,3)";
    return RET_PARAM_INVALID;
  }
  if (wc_reduce_ && reduce_param->keep_dims_ == false) {
    MS_LOG(ERROR) << "reduce axis (2,3) should keep dims";
    return RET_PARAM_INVALID;
  }
  std::string kernel_name = reduce_type2str.at(reduce_param->mode_);
  if (wc_reduce_) {
    kernel_name += "_WC";
  }
  if (in_tensors_[0]->shape()[reduce_param->axes_[0]] >= LOCAL_CACHE_THREAD ||
      in_tensors_[0]->shape()[reduce_param->axes_[1]] >= LOCAL_CACHE_THREAD) {
    use_local_ = true;
    kernel_name += "_local";
  }
  kernel_name += "_NHWC4";
  enable_fp16_ = ocl_runtime_->GetFp16Enable();

#ifdef PROGRAM_WITH_IL
  kernel_ = ocl_runtime_->GetKernelFromBinary(kernel_name);
#else
  std::set<std::string> build_options;
  std::string source = reduce_source;
  std::string program_name = "Reduce";
  ocl_runtime_->LoadSource(program_name, source);
  ocl_runtime_->BuildKernel(kernel_, program_name, kernel_name, build_options);
#endif
  MS_LOG(DEBUG) << kernel_name << " Init Done!";
  return mindspore::lite::RET_OK;
}

void ReduceOpenCLKernel::InitNHWCShape() {
  std::vector<int> shapex = out_tensors_[0]->shape();
  size_t n = 1, h = 1, w = 1, c = 1;
  if (shapex.size() == 2) {
    n = shapex[0];
    c = shapex[1];
  } else if (shapex.size() == 4) {
    n = shapex[0];
    h = shapex[1];
    w = shapex[2];
    c = shapex[3];
  }
  nhwc_shape_ = {n, h, w, c};
}

int ReduceOpenCLKernel::Run() {
  MS_LOG(DEBUG) << this->name() << " Running!";
  std::vector<int> shapex = in_tensors_[0]->shape();
  int h = shapex[1];
  int w = shapex[2];
  int c = shapex[3];
  int c4 = UP_DIV(c, C4NUM);
  std::vector<size_t> local = {};
  if (use_local_) {
    local = {1, LOCAL_CACHE_THREAD, LOCAL_CACHE_THREAD};
  }
  std::vector<size_t> global = {static_cast<size_t>(c4), 1, 1};
  if (wc_reduce_) {
    global = {static_cast<size_t>(h), 1, 1};
  }
  cl_int4 size = {h, w, c4, c};
  int arg_idx = 0;
  ocl_runtime_->SetKernelArg(kernel_, arg_idx++, in_tensors_[0]->data_c());
  ocl_runtime_->SetKernelArg(kernel_, arg_idx++, out_tensors_[0]->data_c());
  ocl_runtime_->SetKernelArg(kernel_, arg_idx++, size);
  ocl_runtime_->RunKernel(kernel_, global, local, nullptr);
  return mindspore::lite::RET_OK;
}

kernel::LiteKernel *OpenCLReduceKernelCreator(const std::vector<lite::Tensor *> &inputs,
                                              const std::vector<lite::Tensor *> &outputs, OpParameter *opParameter,
                                              const lite::InnerContext *ctx, const kernel::KernelKey &desc,
                                              const mindspore::lite::PrimitiveC *primitive) {
  auto *kernel = new (std::nothrow) ReduceOpenCLKernel(reinterpret_cast<OpParameter *>(opParameter), inputs, outputs);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "kernel " << opParameter->name_ << " create failed.";
    free(opParameter);
    return nullptr;
  }
  auto ret = kernel->Init();
  if (ret != mindspore::lite::RET_OK) {
    delete kernel;
    return nullptr;
  }
  return kernel;
}

REG_KERNEL(kGPU, kNumberTypeFloat32, PrimitiveType_Mean, OpenCLReduceKernelCreator)
REG_KERNEL(kGPU, kNumberTypeFloat16, PrimitiveType_Mean, OpenCLReduceKernelCreator)
REG_KERNEL(kGPU, kNumberTypeFloat32, PrimitiveType_Reduce, OpenCLReduceKernelCreator)
REG_KERNEL(kGPU, kNumberTypeFloat16, PrimitiveType_Reduce, OpenCLReduceKernelCreator)
}  // namespace mindspore::kernel
