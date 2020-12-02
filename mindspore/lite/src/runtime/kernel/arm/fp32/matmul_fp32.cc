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

#include "src/runtime/kernel/arm/fp32/matmul_fp32.h"
#include "include/errorcode.h"
#include "nnacl/fp32/matmul.h"
#include "src/runtime/runtime_api.h"
#include "src/kernel_registry.h"
#include "src/runtime/kernel/arm/base/dequant.h"

using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_INPUT_TENSOR_ERROR;
using mindspore::lite::RET_MEMORY_FAILED;
using mindspore::lite::RET_OK;

using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::schema::PrimitiveType_MatMul;

namespace mindspore::kernel {
MatmulCPUKernel::~MatmulCPUKernel() { FreeTmpBuffer(); }

void MatmulCPUKernel::FreeTmpBuffer() {
  if (a_pack_ptr_ != nullptr) {
    free(a_pack_ptr_);
    a_pack_ptr_ = nullptr;
  }
  if (b_pack_ptr_ != nullptr) {
    free(b_pack_ptr_);
    b_pack_ptr_ = nullptr;
  }
  if (bias_ptr_ != nullptr) {
    free(bias_ptr_);
    bias_ptr_ = nullptr;
  }
}

int MatmulCPUKernel::MallocMatrixABuffer() {
  auto a_shape = in_tensors_.at(0)->shape();
  int batch = 1;
  MS_ASSERT(a_shape.size() >= 2);
  for (size_t i = 0; i < a_shape.size() - 2; ++i) {
    batch *= a_shape[i];
  }
  params_->batch = batch;
  params_->row_ = params_->a_transpose_ ? a_shape[a_shape.size() - 1] : a_shape[a_shape.size() - 2];
#ifdef ENABLE_ARM64
  if (params_->a_init_shape_ && params_->row_ == 1) {
    is_vector_a_ = true;
  }
#endif
  params_->deep_ = params_->a_transpose_ ? a_shape[a_shape.size() - 2] : a_shape[a_shape.size() - 1];
  params_->row_4_ = UP_ROUND(params_->row_, C4NUM);
  params_->row_12_ = UP_ROUND(params_->row_, C12NUM);

#ifdef ENABLE_ARM32
  a_pack_ptr_ = reinterpret_cast<float *>(malloc(params_->batch * params_->row_4_ * params_->deep_ * sizeof(float)));
  if (a_pack_ptr_ == nullptr) {
    FreeTmpBuffer();
    return RET_MEMORY_FAILED;
  }
  memset(a_pack_ptr_, 0, params_->row_4_ * params_->deep_ * sizeof(float));
#else
  int row_tmp = is_vector_a_ ? 1 : params_->row_12_;
  a_pack_ptr_ = reinterpret_cast<float *>(malloc(params_->batch * row_tmp * params_->deep_ * sizeof(float)));
  if (a_pack_ptr_ == nullptr) {
    FreeTmpBuffer();
    return RET_MEMORY_FAILED;
  }
  memset(a_pack_ptr_, 0, params_->batch * row_tmp * params_->deep_ * sizeof(float));
#endif
  return RET_OK;
}

int MatmulCPUKernel::MallocMatrixBBuffer() {
  auto b_shape = in_tensors_.at(1)->shape();
  if (b_shape.empty()) {
    return RET_OK;
  }
  int batch = 1;
  MS_ASSERT(b_shape.size() >= 2);
  for (size_t i = 0; i < b_shape.size() - 2; ++i) {
    batch *= b_shape[i];
  }
  params_->batch = batch;
  params_->col_ = params_->b_transpose_ ? b_shape[b_shape.size() - 2] : b_shape[b_shape.size() - 1];
  params_->col_8_ = UP_ROUND(params_->col_, 8);
  params_->deep_ = params_->b_transpose_ ? b_shape[b_shape.size() - 1] : b_shape[b_shape.size() - 2];

  int col_tmp = is_vector_a_ ? params_->col_ : params_->col_8_;
  b_pack_ptr_ = reinterpret_cast<float *>(malloc(params_->batch * col_tmp * params_->deep_ * sizeof(float)));
  if (b_pack_ptr_ == nullptr) {
    FreeTmpBuffer();
    return RET_MEMORY_FAILED;
  }
  memset(b_pack_ptr_, 0, params_->batch * col_tmp * params_->deep_ * sizeof(float));

  thread_count_ = MSMIN(thread_count_, UP_DIV(params_->col_8_, 8));
  thread_stride_ = UP_DIV(UP_DIV(params_->col_8_, 8), thread_count_);
  return RET_OK;
}

int MatmulCPUKernel::InitBias() {
  if (in_tensors_.size() == 3) {
    auto c_shape = out_tensors_.at(0)->shape();
    auto bias_shape = in_tensors_[1]->shape();
    MS_ASSERT(bias_shape.size() >= 1);
    MS_ASSERT(c_shape.size() >= 1);
    if (bias_shape[bias_shape.size() - 1] != c_shape[c_shape.size() - 1]) {
      MS_LOG(ERROR) << "The bias'dimension is not equal with colum";
      FreeTmpBuffer();
      return RET_INPUT_TENSOR_ERROR;
    }
    auto col = c_shape[c_shape.size() - 1];
    auto col_8 = UP_ROUND(col, 8);
    auto col_tmp = is_vector_a_ ? col : col_8;
    bias_ptr_ = reinterpret_cast<float *>(malloc(col_tmp * sizeof(float)));
    if (bias_ptr_ == nullptr) {
      FreeTmpBuffer();
      return RET_MEMORY_FAILED;
    }
    memcpy(bias_ptr_, in_tensors_[2]->data_c(), in_tensors_[2]->ElementsNum() * sizeof(float));
  }
  return RET_OK;
}

int MatmulCPUKernel::ReSize() {
  if (params_->a_const_ == false || params_->a_init_shape_ == false) {
    if (a_pack_ptr_ != nullptr) {
      free(a_pack_ptr_);
      a_pack_ptr_ = nullptr;
    }
    auto ret = MallocMatrixABuffer();
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Matmul fp32 malloc matrix a buffer failed";
      return RET_ERROR;
    }
  }
  if (params_->b_const_ == false || params_->b_init_shape_ == false) {
    if (b_pack_ptr_ != nullptr) {
      free(b_pack_ptr_);
      b_pack_ptr_ = nullptr;
    }
    auto ret = MallocMatrixBBuffer();
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Matmul fp32 malloc matrix b buffer failed";
      return RET_ERROR;
    }
  }
  if (bias_ptr_ != nullptr) {
    free(bias_ptr_);
    bias_ptr_ = nullptr;
  }
  auto ret = InitBias();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Matmul fp32 init bias failed";
    return RET_ERROR;
  }
  return RET_OK;
}

void MatmulCPUKernel::InitMatrixA(const float *src_ptr, float *dst_ptr) {
  if (is_vector_a_) {
    memcpy(dst_ptr, src_ptr, params_->batch * params_->deep_ * sizeof(float));
    return;
  }

  for (int i = 0; i < params_->batch; i++) {
    const float *src = src_ptr + i * params_->deep_ * params_->row_;
#ifdef ENABLE_ARM32
    float *dst = dst_ptr + i * params_->deep_ * params_->row_4_;
    if (params_->a_transpose_) {
      RowMajor2Row4Major(src, dst, params_->deep_, params_->row_);
    } else {
      RowMajor2Col4Major(src, dst, params_->row_, params_->deep_);
    }
#else
    float *dst = dst_ptr + i * params_->deep_ * params_->row_12_;
    if (params_->a_transpose_) {
      RowMajor2Row12Major(src, dst, params_->deep_, params_->row_);
    } else {
      RowMajor2Col12Major(src, dst, params_->row_, params_->deep_);
    }
#endif
  }
  return;
}

void MatmulCPUKernel::InitMatrixB(const float *src_ptr, float *dst_ptr) {
  if (is_vector_a_) {
    if (params_->b_transpose_) {
      memcpy(dst_ptr, src_ptr, params_->batch * params_->col_ * params_->deep_ * sizeof(float));
    } else {
      for (int i = 0; i < params_->batch; i++) {
        const float *src = src_ptr + i * params_->deep_ * params_->col_;
        float *dst = dst_ptr + i * params_->deep_ * params_->col_;
        RowMajor2ColMajor(src, dst, params_->deep_, params_->col_);
      }
    }
    return;
  }

  for (int i = 0; i < params_->batch; i++) {
    const float *src = src_ptr + i * params_->deep_ * params_->col_;
    float *dst = dst_ptr + i * params_->deep_ * params_->col_8_;
    if (params_->b_transpose_) {
      RowMajor2Col8Major(src, dst, params_->col_, params_->deep_);
    } else {
      RowMajor2Row8Major(src, dst, params_->deep_, params_->col_);
    }
  }
  return;
}

int MatmulCPUKernel::Init() {
  params_->a_init_shape_ = (in_tensors_.at(0)->shape().size() != 0);
  params_->b_init_shape_ = (in_tensors_.at(1)->shape().size() != 0);
  if (params_->a_init_shape_ == true) {
    auto ret = MallocMatrixABuffer();
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Matmul fp32 malloc matrix a buffer failed";
      return RET_ERROR;
    }
  }
  if (params_->b_init_shape_ == true) {
    auto ret = MallocMatrixBBuffer();
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Matmul fp32 malloc matrix b buffer failed";
      return RET_ERROR;
    }
  }

  params_->a_const_ = (in_tensors_.at(0)->data_c() != nullptr);
  params_->b_const_ = (in_tensors_.at(1)->data_c() != nullptr);
  if (params_->a_const_ == true) {
    InitMatrixA(reinterpret_cast<float *>(in_tensors_.at(0)->data_c()), a_pack_ptr_);
    a_ptr_ = a_pack_ptr_;
  }
  if (params_->b_const_ == true) {
    InitMatrixB(reinterpret_cast<float *>(in_tensors_.at(1)->data_c()), b_pack_ptr_);
    b_ptr_ = b_pack_ptr_;
  }
  if (!InferShapeDone()) {
    return RET_OK;
  }
  auto ret = InitBias();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Matmul fp32 init bias failed";
    return RET_ERROR;
  }
  return RET_OK;
}

int MatmulCPUKernel::RunImpl(int task_id) {
  int cur_oc = MSMIN(thread_stride_ * C8NUM, params_->col_ - task_id * thread_stride_ * C8NUM);
  if (cur_oc <= 0) {
    return RET_OK;
  }
  auto b = cur_b_ptr_ + task_id * thread_stride_ * C8NUM * params_->deep_;
  auto c = cur_c_ptr_ + task_id * thread_stride_ * C8NUM;
  auto bias = bias_ptr_ ? bias_ptr_ + task_id * thread_stride_ * C8NUM : NULL;
  MS_ASSERT(cur_a_ptr_);
  MS_ASSERT(b);
  MS_ASSERT(c);
  MS_ASSERT(bias);
  if (is_vector_a_) {
    MatVecMul(cur_a_ptr_, b, c, bias, ActType_No, params_->deep_, cur_oc);
  } else {
    MatMulOpt(cur_a_ptr_, b, c, bias, ActType_No, params_->deep_, params_->row_, cur_oc, params_->col_, OutType_Nhwc);
  }
  return RET_OK;
}

int MatmulFloatRun(void *cdata, int task_id) {
  auto op = reinterpret_cast<MatmulCPUKernel *>(cdata);
  auto error_code = op->RunImpl(task_id);
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "MatmulFp32Run error task_id[" << task_id << "] error_code[" << error_code << "]";
    return RET_ERROR;
  }
  return RET_OK;
}

int MatmulCPUKernel::Run() {
  auto a_src = reinterpret_cast<float *>(in_tensors_.at(0)->data_c());
  auto b_src = reinterpret_cast<float *>(in_tensors_.at(1)->data_c());
  auto c_src = reinterpret_cast<float *>(out_tensors_.at(0)->data_c());

  if (params_->a_const_ == false || is_train()) {
    if (is_vector_a_) {
      a_ptr_ = a_src;
    } else {
      InitMatrixA(a_src, a_pack_ptr_);
      a_ptr_ = a_pack_ptr_;
    }
  }
  if (params_->b_const_ == false || is_train()) {
    if (is_vector_a_ && params_->b_transpose_) {
      b_ptr_ = b_src;
    } else {
      InitMatrixB(b_src, b_pack_ptr_);
      b_ptr_ = b_pack_ptr_;
    }
  }

  for (int i = 0; i < params_->batch; ++i) {
    if (is_vector_a_) {
      cur_a_ptr_ = a_ptr_ + i * params_->deep_;
      cur_b_ptr_ = b_ptr_ + i * params_->deep_ * params_->col_;
      cur_c_ptr_ = c_src + i * params_->row_ * params_->col_;
    } else {
      cur_a_ptr_ = a_ptr_ + i * params_->row_12_ * params_->deep_;
      cur_b_ptr_ = b_ptr_ + i * params_->deep_ * params_->col_8_;
      cur_c_ptr_ = c_src + i * params_->row_ * params_->col_;
    }
    ParallelLaunch(this->context_->thread_pool_, MatmulFloatRun, this, thread_count_);
  }
  return RET_OK;
}

void MatmulCPUKernel::eval() {
  // Copy weights after training
  LiteKernel::eval();
  if (params_->a_const_ == true) {
    InitMatrixA(reinterpret_cast<float *>(in_tensors_.at(0)->MutableData()), a_pack_ptr_);
  }
  if (params_->b_const_ == true) {
    InitMatrixB(reinterpret_cast<float *>(in_tensors_.at(1)->MutableData()), b_pack_ptr_);
  }
}

kernel::LiteKernel *CpuMatmulFp32KernelCreator(const std::vector<lite::Tensor *> &inputs,
                                               const std::vector<lite::Tensor *> &outputs, OpParameter *opParameter,
                                               const lite::InnerContext *ctx, const kernel::KernelKey &desc,
                                               const mindspore::lite::PrimitiveC *primitive) {
  MS_ASSERT(opParameter != nullptr);
  MS_ASSERT(desc.type == schema::PrimitiveType_MatMul);

  auto *weight_tensor = inputs.at(kWeightIndex);
  auto *restore_data = weight_tensor->data_c();
  auto restore_type = weight_tensor->data_type();
  bool dequant_flag =
    !weight_tensor->quant_params().empty() && weight_tensor->quant_params().front().inited && restore_data != nullptr;
  if (dequant_flag) {
    auto *dequant_weight = kernel::DequantUtil::DequantWeight(weight_tensor);
    if (dequant_weight == nullptr) {
      MS_LOG(ERROR) << "dequant data is nullptr.";
      free(opParameter);
      return nullptr;
    }
    weight_tensor->set_data(dequant_weight);
  }

  auto kernel = new (std::nothrow) MatmulCPUKernel(opParameter, inputs, outputs, ctx, primitive);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "kernel is nullptr.";
    if (dequant_flag) {
      weight_tensor->FreeData();
      weight_tensor->set_data(restore_data);
      weight_tensor->set_data_type(restore_type);
    }
    free(opParameter);
    return nullptr;
  }
  auto ret = kernel->Init();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Init kernel failed, name: " << opParameter->name_ << ", type: "
                  << schema::EnumNamePrimitiveType(static_cast<schema::PrimitiveType>(opParameter->type_));
    if (dequant_flag) {
      weight_tensor->FreeData();
      weight_tensor->set_data(restore_data);
      weight_tensor->set_data_type(restore_type);
    }
    delete kernel;
    return nullptr;
  }

  if (dequant_flag) {
    weight_tensor->FreeData();
    weight_tensor->set_data(restore_data);
    weight_tensor->set_data_type(restore_type);
  }

  return kernel;
}

REG_KERNEL(kCPU, kNumberTypeFloat32, PrimitiveType_MatMul, CpuMatmulFp32KernelCreator)
}  // namespace mindspore::kernel
