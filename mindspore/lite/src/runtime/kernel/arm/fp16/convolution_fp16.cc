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

#include "src/runtime/kernel/arm/fp16/convolution_fp16.h"
#include <vector>
#include "src/runtime/kernel/arm/fp16/convolution_winograd_fp16.h"
#include "src/runtime/kernel/arm/fp16/convolution_1x1_fp16.h"
#include "src/runtime/kernel/arm/fp16/group_convolution_fp16.h"
#include "nnacl/fp16/conv_fp16.h"
#include "nnacl/fp16/cast_fp16.h"
#include "nnacl/fp16/pack_fp16.h"
#include "src/runtime/kernel/arm/fp16/layout_transform_fp16.h"
#include "schema/model_generated.h"
#include "src/kernel_registry.h"
#include "include/errorcode.h"
#include "src/runtime/runtime_api.h"
#include "nnacl/fp16/winograd_utils_fp16.h"
#include "src/runtime/kernel/arm/base/dequant.h"

using mindspore::kernel::KERNEL_ARCH::kCPU;
using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_Conv2D;
using mindspore::schema::Format::Format_NHWC;

namespace mindspore::kernel {
int ConvolutionFP16CPUKernel::InitWeightBias() {
  auto filter_tensor = in_tensors_.at(kWeightIndex);
  int kernel_h = filter_tensor->Height();
  int kernel_w = filter_tensor->Width();
  int in_channel = filter_tensor->Channel();
  int out_channel = filter_tensor->Batch();
  conv_param_->input_channel_ = in_channel;
  conv_param_->output_channel_ = out_channel;
  int oc8 = UP_DIV(out_channel, C8NUM);
  int kernel_plane = kernel_h * kernel_w;
  int pack_weight_size = oc8 * C8NUM * in_channel * kernel_plane;

  // init weight
  auto ret = ConvolutionBaseFP16CPUKernel::GetExecuteFilter();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Get Execute filter failed.";
    return ret;
  }
  packed_weight_ = reinterpret_cast<float16_t *>(malloc(pack_weight_size * sizeof(float16_t)));
  if (packed_weight_ == nullptr) {
    MS_LOG(ERROR) << "malloc packed_weight_ failed.";
    return RET_ERROR;
  }
  memset(packed_weight_, 0, pack_weight_size * sizeof(float16_t));
  RowMajor2Col8MajorFp16(execute_weight_, packed_weight_, out_channel, in_channel * kernel_plane, false);
  if (fp16_weight_ != nullptr) {
    free(fp16_weight_);
    fp16_weight_ = nullptr;
  }

  // init bias
  bias_data_ = malloc(oc8 * C8NUM * sizeof(float16_t));
  if (bias_data_ == nullptr) {
    MS_LOG(ERROR) << "malloc bias_data_ failed.";
    return RET_ERROR;
  }
  memset(bias_data_, 0, oc8 * C8NUM * sizeof(float16_t));
  auto fp16_bias_data = reinterpret_cast<float16_t *>(bias_data_);
  if (in_tensors_.size() == kInputSize2) {
    auto ori_bias = reinterpret_cast<float *>(in_tensors_.at(kBiasIndex)->MutableData());
    for (int i = 0; i < out_channel; ++i) {
      fp16_bias_data[i] = (float16_t)ori_bias[i];
    }
  } else {
    MS_ASSERT(inputs_.size() == kInputSize1);
  }
  return RET_OK;
}

int ConvolutionFP16CPUKernel::InitTmpBuffer() {
  const int cal_num = 16;
  int in_channel = conv_param_->input_channel_;
  int kernel_plane = conv_param_->kernel_h_ * conv_param_->kernel_w_;
  int unit_size = kernel_plane * in_channel * cal_num * thread_count_;

  packed_input_ = reinterpret_cast<float16_t *>(ctx_->allocator->Malloc(unit_size * sizeof(float16_t)));
  if (packed_input_ == nullptr) {
    MS_LOG(ERROR) << "malloc packed_input_ failed.";
    return RET_ERROR;
  }

  col_major_input_ = reinterpret_cast<float16_t *>(ctx_->allocator->Malloc(unit_size * sizeof(float16_t)));
  if (col_major_input_ == nullptr) {
    MS_LOG(ERROR) << "malloc col_major_input_ failed.";
    return RET_ERROR;
  }
  return RET_OK;
}

int ConvolutionFP16CPUKernel::Init() {
  auto ret = InitWeightBias();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Init weight bias failed.";
    return RET_ERROR;
  }
  if (!InferShapeDone()) {
    return RET_OK;
  }
  return ReSize();
}

int ConvolutionFP16CPUKernel::ReSize() {
  auto ret = ConvolutionBaseCPUKernel::CheckResizeValid();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Resize is invalid.";
    return ret;
  }

  ret = ConvolutionBaseCPUKernel::Init();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "ConvolutionBase init fail!ret: " << ret;
    return ret;
  }
  return RET_OK;
}

int ConvolutionFP16CPUKernel::RunImpl(int task_id) {
  ConvFp16(execute_input_, packed_input_, packed_weight_, reinterpret_cast<float16_t *>(bias_data_), col_major_input_,
           execute_output_, task_id, conv_param_);
  return RET_OK;
}

static int ConvolutionFp16Impl(void *cdata, int task_id) {
  auto conv = reinterpret_cast<ConvolutionFP16CPUKernel *>(cdata);
  auto error_code = conv->RunImpl(task_id);
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "ConvolutionFp16 Run error task_id[" << task_id << "] error_code[" << error_code << "]";
    return RET_ERROR;
  }
  return RET_OK;
}

int ConvolutionFP16CPUKernel::Run() {
  auto ret = ConvolutionBaseFP16CPUKernel::GetExecuteTensor();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Get Execute tensor failed.";
    ConvolutionBaseFP16CPUKernel::FreeTmpBuffer();
    return ret;
  }

  ret = InitTmpBuffer();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Init tmp buffer failed.";
    ConvolutionBaseFP16CPUKernel::FreeTmpBuffer();
    FreeTmpBuffer();
    return RET_ERROR;
  }

  ret = ParallelLaunch(this->context_->thread_pool_, ConvolutionFp16Impl, this, thread_count_);
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "conv fp16 error ret[" << ret << "]";
  }
  ConvolutionBaseFP16CPUKernel::IfCastOutput();
  ConvolutionBaseFP16CPUKernel::FreeTmpBuffer();
  FreeTmpBuffer();
  return ret;
}

ConvParameter *CreateNewConvParameterFp16(ConvParameter *parameter) {
  auto conv_parameter = reinterpret_cast<ConvParameter *>(malloc(sizeof(ConvParameter)));
  if (conv_parameter == nullptr) {
    MS_LOG(ERROR) << "Malloc new conv parameter failed.";
    return nullptr;
  }
  memcpy(conv_parameter, parameter, sizeof(ConvParameter));
  return conv_parameter;
}

kernel::LiteKernel *CpuConvFp16KernelSelect(const std::vector<lite::Tensor *> &inputs,
                                            const std::vector<lite::Tensor *> &outputs, OpParameter *op_parameter,
                                            const InnerContext *ctx, const mindspore::lite::PrimitiveC *primitive,
                                            bool use_winograd, int out_unit) {
  auto conv_param = reinterpret_cast<ConvParameter *>(op_parameter);
  if (conv_param->kernel_h_ == 1 && conv_param->kernel_w_ == 1) {
    return new (std::nothrow) kernel::Convolution1x1FP16CPUKernel(op_parameter, inputs, outputs, ctx, primitive);
  } else if (use_winograd) {
    return new (std::nothrow)
      kernel::ConvolutionWinogradFP16CPUKernel(op_parameter, inputs, outputs, ctx, primitive, out_unit);
  } else {
    return new (std::nothrow) kernel::ConvolutionFP16CPUKernel(op_parameter, inputs, outputs, ctx, primitive);
  }
  return nullptr;
}

void FreeMemoryFp16(std::vector<kernel::LiteKernel *> group_convs, std::vector<lite::Tensor *> new_inputs,
                    std::vector<lite::Tensor *> new_outputs) {
  for (auto sub_conv : group_convs) {
    if (sub_conv != nullptr) {
      delete sub_conv;
    }
  }
  for (auto in_tensor : new_inputs) {
    if (in_tensor != nullptr) {
      delete in_tensor;
    }
  }
  for (auto out_tensor : new_outputs) {
    if (out_tensor != nullptr) {
      delete out_tensor;
    }
  }
}

kernel::LiteKernel *CpuGroupConvFp16KernelCreator(const std::vector<lite::Tensor *> &inputs,
                                                  const std::vector<lite::Tensor *> &outputs, OpParameter *op_parameter,
                                                  const InnerContext *ctx, const mindspore::lite::PrimitiveC *primitive,
                                                  int group) {
  std::vector<kernel::LiteKernel *> group_convs;
  std::vector<int> in_shape;
  std::vector<int> filter_shape;
  std::vector<int> bias_shape;
  std::vector<int> out_shape;

  auto conv_param = reinterpret_cast<ConvParameter *>(op_parameter);
  int out_channel = inputs.at(kWeightIndex)->Batch();
  int new_in_channel = inputs.at(kWeightIndex)->Channel();
  int new_out_channel = 0;
  if (group == 0) {
    MS_LOG(ERROR) << "Divisor 'group' cannot be 0.";
    return nullptr;
  } else {
    new_out_channel = out_channel / group;
  }
  int kernel_h = conv_param->kernel_h_;
  int kernel_w = conv_param->kernel_w_;
  int input_num = inputs.size();
  int output_num = outputs.size();
  bool has_bias = input_num == 3;
  bool use_winograd = false;
  int out_unit;
  bool infered_flag = (primitive != nullptr && primitive->GetInferFlag());

  if (infered_flag) {
    int batch = inputs.front()->Batch();
    int in_h = inputs.front()->Height();
    int in_w = inputs.front()->Width();
    conv_param->input_channel_ = new_in_channel;
    conv_param->output_channel_ = new_out_channel;
    CheckIfUseWinogradFp16(&use_winograd, &out_unit, conv_param);
    in_shape = {batch, in_h, in_w, new_in_channel};
    out_shape = {batch, conv_param->output_h_, conv_param->output_w_, new_out_channel};
  }

  filter_shape = {new_out_channel, kernel_h, kernel_w, new_in_channel};
  bias_shape = {new_out_channel};

  for (int i = 0; i < group; ++i) {
    std::vector<lite::Tensor *> new_inputs;
    std::vector<lite::Tensor *> new_outputs;
    auto new_conv_parameter = CreateNewConvParameterFp16(conv_param);
    if (new_conv_parameter == nullptr) {
      FreeMemoryFp16(group_convs, new_inputs, new_outputs);
      MS_LOG(ERROR) << "Get new conv parameter failed.";
      return nullptr;
    }
    // get new input for each group
    auto in_tensor =
      new (std::nothrow) lite::Tensor(inputs.front()->data_type(), in_shape, Format_NHWC, lite::Tensor::Category::VAR);
    if (in_tensor == nullptr) {
      delete new_conv_parameter;
      FreeMemoryFp16(group_convs, new_inputs, new_outputs);
      MS_LOG(ERROR) << "new in_tensor failed.";
      return nullptr;
    }
    if (infered_flag) {
      auto ret = in_tensor->MallocData();
      if (ret != RET_OK) {
        delete new_conv_parameter;
        delete in_tensor;
        FreeMemoryFp16(group_convs, new_inputs, new_outputs);
        MS_LOG(ERROR) << "in tensor malloc failed.";
        return nullptr;
      }
    }
    new_inputs.emplace_back(in_tensor);

    // new weight
    auto filter_tensor = new (std::nothrow) lite::Tensor(inputs.at(kWeightIndex)->data_type(), filter_shape,
                                                         Format_NHWC, lite::Tensor::Category::CONST_TENSOR);
    if (filter_tensor == nullptr) {
      delete new_conv_parameter;
      FreeMemoryFp16(group_convs, new_inputs, new_outputs);
      MS_LOG(ERROR) << "new filter_tensor failed.";
      return nullptr;
    }
    auto ret = filter_tensor->MallocData();
    if (ret != RET_OK) {
      delete new_conv_parameter;
      delete filter_tensor;
      FreeMemoryFp16(group_convs, new_inputs, new_outputs);
      MS_LOG(ERROR) << "filter_tensor malloc failed.";
      return nullptr;
    }
    int copy_length = kernel_h * kernel_w * new_in_channel * new_out_channel;
    auto filter_data_type = inputs.at(kWeightIndex)->data_type();
    if (filter_data_type == kNumberTypeFloat16) {
      auto *origin_weight = reinterpret_cast<float16_t *>(inputs.at(kWeightIndex)->data_c());
      memcpy(filter_tensor->data_c(), origin_weight + i * copy_length, copy_length * sizeof(float16_t));
    } else {
      MS_ASSERT(filter_data_type == kNumberTypeFloat32);
      auto *origin_weight = reinterpret_cast<float *>(inputs.at(kWeightIndex)->data_c());
      memcpy(filter_tensor->data_c(), origin_weight + i * copy_length, copy_length * sizeof(float));
    }
    new_inputs.emplace_back(filter_tensor);

    // if has bias, set new bias
    if (has_bias) {
      auto *origin_bias = inputs.at(kBiasIndex)->data_c();
      auto bias_data_type = inputs.at(kBiasIndex)->data_type();
      auto bias_tensor = new (std::nothrow)
        lite::Tensor(inputs.at(kBiasIndex)->data_type(), bias_shape, Format_NHWC, lite::Tensor::Category::CONST_TENSOR);
      if (bias_tensor == nullptr) {
        delete new_conv_parameter;
        FreeMemoryFp16(group_convs, new_inputs, new_outputs);
        MS_LOG(ERROR) << "new bias_tensor failed.";
        return nullptr;
      }
      ret = bias_tensor->MallocData();
      if (ret != RET_OK) {
        delete new_conv_parameter;
        delete bias_tensor;
        FreeMemoryFp16(group_convs, new_inputs, new_outputs);
        MS_LOG(ERROR) << "bias_tensor malloc failed.";
        return nullptr;
      }
      if (bias_data_type == kNumberTypeFloat16) {
        auto bias_data = reinterpret_cast<float16_t *>(origin_bias);
        memcpy(bias_tensor->data_c(), bias_data + i * new_out_channel, new_out_channel * sizeof(float16_t));
      } else {
        MS_ASSERT(bias_data_type == kNumberTypeFloat32);
        auto bias_data = reinterpret_cast<float *>(origin_bias);
        memcpy(bias_tensor->data_c(), bias_data + i * new_out_channel, new_out_channel * sizeof(float));
      }
      new_inputs.emplace_back(bias_tensor);
    }

    // set new output tensor
    for (int j = 0; j < output_num; ++j) {
      auto tmp_out_tensor = new (std::nothrow) lite::Tensor();
      if (tmp_out_tensor == nullptr) {
        delete new_conv_parameter;
        FreeMemoryFp16(group_convs, new_inputs, new_outputs);
        MS_LOG(ERROR) << "new tmp_out_tensor failed.";
        return nullptr;
      }
      tmp_out_tensor->set_data_type(outputs.at(j)->data_type());
      tmp_out_tensor->SetFormat(outputs.at(j)->GetFormat());
      if (infered_flag) {
        tmp_out_tensor->set_shape(out_shape);
        ret = tmp_out_tensor->MallocData();
        if (ret != RET_OK) {
          delete new_conv_parameter;
          delete tmp_out_tensor;
          FreeMemoryFp16(group_convs, new_inputs, new_outputs);
          MS_LOG(ERROR) << "tmp_out_tensor malloc data failed.";
          return nullptr;
        }
      }
      new_outputs.emplace_back(tmp_out_tensor);
    }

    group_convs.emplace_back(CpuConvFp16KernelSelect(new_inputs, new_outputs,
                                                     reinterpret_cast<OpParameter *>(new_conv_parameter), ctx,
                                                     primitive, use_winograd, out_unit));
  }
  return new (std::nothrow)
    GroupConvolutionFP16CPUKernel(op_parameter, inputs, outputs, ctx, primitive, group_convs, group);
}

kernel::LiteKernel *CpuConvFp16KernelCreator(const std::vector<lite::Tensor *> &inputs,
                                             const std::vector<lite::Tensor *> &outputs, OpParameter *opParameter,
                                             const InnerContext *ctx, const kernel::KernelKey &desc,
                                             const mindspore::lite::PrimitiveC *primitive) {
  MS_ASSERT(opParameter != nullptr);
  MS_ASSERT(desc.type == schema::PrimitiveType_Conv2D);

  auto *weight_tensor = inputs.at(kWeightIndex);
  auto *restore_data = weight_tensor->data_c();
  auto restore_type = weight_tensor->data_type();
  bool dequant_flag = !weight_tensor->GetQuantParams().empty() && weight_tensor->GetQuantParams().front().inited &&
                      restore_data != nullptr;
  if (dequant_flag) {
    auto *dequant_weight = kernel::DequantUtil::DequantWeight(weight_tensor);
    if (dequant_weight == nullptr) {
      MS_LOG(ERROR) << "dequant data is nullptr.";
      free(opParameter);
      return nullptr;
    }
    weight_tensor->set_data_type(kNumberTypeFloat32);
    weight_tensor->set_data(dequant_weight);
  }

  auto conv_param = reinterpret_cast<ConvParameter *>(opParameter);
  bool use_winograd = false;
  int out_unit;
  if (primitive != nullptr && primitive->GetInferFlag()) {
    conv_param->input_h_ = inputs.front()->Height();
    conv_param->input_w_ = inputs.front()->Width();
    conv_param->input_channel_ = inputs.front()->Channel();
    conv_param->output_h_ = outputs.front()->Height();
    conv_param->output_w_ = outputs.front()->Width();
    conv_param->output_channel_ = outputs.front()->Channel();
    conv_param->op_parameter_.thread_num_ = ctx->thread_num_;
    CheckIfUseWinogradFp16(&use_winograd, &out_unit, conv_param);
  }
  int group = conv_param->group_;
  kernel::LiteKernel *kernel = nullptr;
  if (group == 1) {
    kernel = CpuConvFp16KernelSelect(inputs, outputs, opParameter, ctx, primitive, use_winograd, out_unit);
  } else {
    kernel = CpuGroupConvFp16KernelCreator(inputs, outputs, opParameter, ctx, primitive, group);
  }

  if (kernel == nullptr) {
    MS_LOG(DEBUG) << "Create conv fp16 kernel failed.";
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
    delete kernel;
    MS_LOG(INFO) << "Init fp16 kernel failed, name: " << opParameter->name_
                 << ", type: " << schema::EnumNamePrimitiveType(static_cast<schema::PrimitiveType>(opParameter->type_));
    if (dequant_flag) {
      weight_tensor->FreeData();
      weight_tensor->set_data(restore_data);
      weight_tensor->set_data_type(restore_type);
    }
    return nullptr;
  }
  if (dequant_flag) {
    weight_tensor->FreeData();
    weight_tensor->set_data(restore_data);
    weight_tensor->set_data_type(restore_type);
  }
  return kernel;
}
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Conv2D, CpuConvFp16KernelCreator)
}  // namespace mindspore::kernel
