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
#ifndef MINDSPORE_LITE_NNACL_LAYER_NORM_PARAMETER_H_
#define MINDSPORE_LITE_NNACL_LAYER_NORM_PARAMETER_H_

#include "nnacl/op_base.h"
#include "nnacl/quantization/quantize.h"

typedef struct LayerNormParameter {
  // Primitive parameter
  OpParameter op_parameter_;
  float epsilon_;
  bool elementwise_affine_;
  // shape correlative
  int *normalized_shape_;
  int normalized_dims_;
  // other parameter
  int thread_count_;
  int thread_outsize_;
} LayerNormParameter;

typedef struct LayerNormQuantArg {
  QuantArg in_quant_arg_;
  QuantArg out_quant_arg_;
  QuantArg gamma_quant_arg_;

  int32_t multiplier_;
  int32_t shift_left_;
  int32_t shift_right_;

  int output_activation_min_;
  int output_activation_max_;
} LayerNormQuantArg;

#endif  // MINDSPORE_LITE_NNACL_LAYER_NORM_PARAMETER_H_
