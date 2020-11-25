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
#include <math.h>
#include "nnacl/int8/resize_int8.h"
#include "nnacl/common_func.h"
#include "nnacl/quantization/fixed_point.h"
#include "nnacl/errorcode.h"

int ResizeBilinearInt8(const int8_t *input_data, int8_t *output_data, const int *input_shape, const int *output_shape,
                       const bool align_corners, QuantArg *quant_in, QuantArg *quant_out, const QuantMulArg *mul_arg,
                       int tid, int thread_num) {
  if (input_data == NULL || output_data == NULL || input_shape == NULL || output_shape == NULL) {
    return NNACL_NULL_PTR;
  }

  int32_t in_n = input_shape[0];
  int32_t in_h = input_shape[1];
  int32_t in_w = input_shape[2];
  int32_t in_c = input_shape[3];

  int32_t new_height = output_shape[1];
  int32_t new_width = output_shape[2];
  int32_t height_scale = 0, width_scale = 0;
  ComputeScale(in_h, new_height, align_corners, &height_scale);
  ComputeScale(in_w, new_width, align_corners, &width_scale);

  int n, h, w, c;
  for (n = 0; n < in_n; n++) {
    for (h = tid; h < new_height; h += thread_num) {
      const int base_offset = 20;
      int scaled_actual_y;
      int bottom, top;
      int scaled_bottom_weight, scaled_top_weight;
      ComputeInterpolationArgs(h, height_scale, in_h, &scaled_actual_y, &bottom, &scaled_bottom_weight, &top,
                               &scaled_top_weight);
      for (w = 0; w < new_width; w++) {
        int scaled_actual_x;
        int left, right;
        int scaled_left_weight, scaled_right_weight;
        ComputeInterpolationArgs(w, width_scale, in_w, &scaled_actual_x, &left, &scaled_left_weight, &right,
                                 &scaled_right_weight);
        for (c = 0; c < in_c; c++) {
          const int64_t bottom_left_value =
            (int64_t)(input_data[offset(input_shape, n, bottom, left, c)] - quant_in->zp_) * scaled_bottom_weight *
            scaled_left_weight;
          const int64_t bottom_right_value =
            (int64_t)(input_data[offset(input_shape, n, bottom, right, c)] - quant_in->zp_) * scaled_bottom_weight *
            scaled_right_weight;
          const int64_t top_left_value = (int64_t)(input_data[offset(input_shape, n, top, left, c)] - quant_in->zp_) *
                                         scaled_top_weight * scaled_left_weight;
          const int64_t top_right_value = (int64_t)(input_data[offset(input_shape, n, top, right, c)] - quant_in->zp_) *
                                          scaled_top_weight * scaled_right_weight;
          const int64_t scaled_interp_value = bottom_left_value + bottom_right_value + top_left_value + top_right_value;
          int32_t interp_value;
          if (scaled_interp_value >= 0) {
            interp_value = (scaled_interp_value + (1 << 19)) / (1 << 20);
          } else {
            interp_value = (scaled_interp_value - (1 << 19)) / (1 << 20);
          }

          const int out_interp_value =
            MultiplyByQuantizedMultiplier(interp_value, mul_arg->multiplier_, mul_arg->left_shift_ + base_offset,
                                          mul_arg->right_shift_ - base_offset) +
            quant_out->zp_;
          int8_t out_value;
          out_value = out_interp_value > INT8_MAX ? INT8_MAX : out_interp_value;
          out_value = out_value < INT8_MIN ? INT8_MIN : out_value;
          output_data[offset(output_shape, n, h, w, c)] = out_value;
        }
      }
    }
  }
  return NNACL_OK;
}

int ResizeBilinearInt8WithFloatWeight(const int8_t *input_data, int8_t *output_data, const int *input_shape,
                                      const int *output_shape, const bool align_corners, QuantArg *quant_in,
                                      QuantArg *quant_out, const QuantMulArg *mul_arg, int tid, int thread_num) {
  if (input_data == NULL || output_data == NULL || input_shape == NULL || output_shape == NULL) {
    return NNACL_NULL_PTR;
  }

  int32_t in_n = input_shape[0];
  int32_t in_h = input_shape[1];
  int32_t in_w = input_shape[2];
  int32_t in_c = input_shape[3];

  int32_t new_height = output_shape[1];
  int32_t new_width = output_shape[2];
  float height_scale, width_scale;
  int ret = ComputeScaleFloat(in_h, new_height, align_corners, &height_scale);
  if (ret != NNACL_OK) {
    return ret;
  }
  ret = ComputeScaleFloat(in_w, new_width, align_corners, &width_scale);
  if (ret != NNACL_OK) {
    return ret;
  }

  int n, h, w, c;
  for (n = 0; n < in_n; n++) {
    for (h = tid; h < new_height; h += thread_num) {
      float actual_y;
      int bottom, top;
      float bottom_weight, top_weight;
      ComputeInterpolationArgsFloatWeight(h, height_scale, in_h, &actual_y, &bottom, &bottom_weight, &top, &top_weight);
      for (w = 0; w < new_width; w++) {
        float actual_x;
        int left, right;
        float left_weight, right_weight;
        ComputeInterpolationArgsFloatWeight(w, width_scale, in_w, &actual_x, &left, &left_weight, &right,
                                            &right_weight);
        for (c = 0; c < in_c; c++) {
          float bottom_left_value = ((int32_t)input_data[offset(input_shape, n, bottom, left, c)] - quant_in->zp_) *
                                    bottom_weight * left_weight;
          float bottom_right_value = ((int32_t)input_data[offset(input_shape, n, bottom, right, c)] - quant_in->zp_) *
                                     bottom_weight * right_weight;
          float top_left_value =
            ((int32_t)input_data[offset(input_shape, n, top, left, c)] - quant_in->zp_) * top_weight * left_weight;
          float top_right_value =
            ((int32_t)input_data[offset(input_shape, n, top, right, c)] - quant_in->zp_) * top_weight * right_weight;
          float interp_value = bottom_left_value + bottom_right_value + top_left_value + top_right_value;

          const int out_interp_value = MultiplyByQuantizedMultiplier((int32_t)interp_value, mul_arg->multiplier_,
                                                                     mul_arg->left_shift_, mul_arg->right_shift_) +
                                       quant_out->zp_;
          int8_t out_value;
          out_value = out_interp_value > INT8_MAX ? INT8_MAX : out_interp_value;
          out_value = out_value < INT8_MIN ? INT8_MIN : out_value;
          output_data[offset(output_shape, n, h, w, c)] = out_value;
        }
      }
    }
  }
  return NNACL_OK;
}

int ResizeNearestNeighborInt8Simple(const int8_t *input_data, int8_t *output_data, const int *input_shape,
                                    const int *output_shape, const bool align_corners, int tid, int thread_num) {
  int batch, y, x, c;
  c = output_shape[3];
  int in_h, in_w, new_height, new_width;
  in_h = input_shape[1];
  in_w = input_shape[2];
  new_height = output_shape[1];
  new_width = output_shape[2];

  for (batch = 0; batch < output_shape[0]; batch++) {
    for (y = tid; y < output_shape[1]; y += thread_num) {
      int input_y = 0;
      ComputeNearestNeighborInt(y, in_h, new_height, align_corners, &input_y);
      for (x = 0; x < output_shape[2]; x++) {
        int input_x = 0;
        ComputeNearestNeighborInt(x, in_w, new_width, align_corners, &input_x);
        int in_offset = offset(input_shape, batch, input_y, input_x, 0);
        int out_offset = offset(output_shape, batch, y, x, 0);
        memcpy(output_data + out_offset, input_data + in_offset, c * sizeof(int8_t));
      }
    }
  }

  return NNACL_OK;
}

void ComputeScale(const int32_t in_value, const int32_t out_value, const bool align_corners, int32_t *scale) {
  if (out_value == 0) {
    return;
  }
  *scale = (in_value * (1 << 10) + out_value / 2) / out_value;
  if (align_corners && out_value > 1) {
    *scale = ((in_value - 1) * (1 << 10) + (out_value - 1) / 2) / (out_value - 1);
  }
}

void ComputeInterpolationArgs(const int32_t pos, const int32_t scale, const int32_t size, int32_t *scaled_pos,
                              int32_t *low, int32_t *scaled_low_weight, int32_t *high, int32_t *scaled_high_weight) {
  *scaled_pos = pos * scale;
  int scale_back = *scaled_pos / (1 << 10);
  *low = scale_back > 0 ? scale_back : 0;
  *scaled_low_weight = (1 << 10) - (*scaled_pos - (1 << 10) * (*low));
  *high = scale_back + 1 < size ? scale_back + 1 : size - 1;
  *scaled_high_weight = *scaled_pos - (1 << 10) * (*low);
}

int ComputeScaleFloat(const int32_t in_value, const int32_t out_value, const bool align_corners, float *scale) {
  if (out_value == 0) {
    return NNACL_ERRCODE_DIVISOR_ZERO;
  }
  *scale = (float)in_value / out_value;
  if (align_corners && out_value > 1) {
    *scale = (float)(in_value - 1) / (out_value - 1);
  }
  return NNACL_OK;
}

void ComputeInterpolationArgsFloatWeight(const int32_t pos, const float scale, const int32_t size, float *actual_pos,
                                         int32_t *low, float *low_weight, int32_t *high, float *high_weight) {
  *actual_pos = pos * scale;
  *low = *actual_pos > 0 ? floor(*actual_pos) : 0;
  *low_weight = 1.0 - (*actual_pos - *low);
  *high = *low + 1 < size ? *low + 1 : size - 1;
  *high_weight = *actual_pos - (*low);
}

void ComputeNearestNeighborInt(const int32_t pos, const int in_size, const int32_t new_size, const bool align_corners,
                               int32_t *nearest) {
  if (new_size == 0) {
    return;
  }
  *nearest = (in_size * pos) / new_size;
  if (align_corners && new_size != 1) {
    *nearest = ((in_size - 1) * pos + (new_size - 1) / 2) / (new_size - 1);
  }
  *nearest = *nearest < in_size ? *nearest : in_size - 1;
}

int ResizeNearestNeighborInt8(const int8_t *input_data, int8_t *output_data, const int *input_shape,
                              const int *output_shape, const bool align_corners, const QuantMulArg *multiplier,
                              QuantArg *quant_in, QuantArg *quant_out, int tid, int thread_num) {
  const int base_offset = 20;
  int32_t batch, y, x, c;
  int32_t in_h, in_w, new_height, new_width;
  in_h = input_shape[1];
  in_w = input_shape[2];
  new_height = output_shape[1];
  new_width = output_shape[2];

  for (batch = 0; batch < output_shape[0]; batch++) {
    for (y = tid; y < output_shape[1]; y += thread_num) {
      int input_y = 0;
      ComputeNearestNeighborInt(y, in_h, new_height, align_corners, &input_y);
      for (x = 0; x < output_shape[2]; x++) {
        int input_x = 0;
        ComputeNearestNeighborInt(x, in_w, new_width, align_corners, &input_x);
        for (c = 0; c < output_shape[3]; c++) {
          int in_offset = offset(input_shape, batch, input_y, input_x, c);
          int out_offset = offset(output_shape, batch, y, x, c);

          int32_t out_value = MultiplyByQuantizedMultiplier(
                                input_data[in_offset] - quant_in->zp_, multiplier->multiplier_,
                                multiplier->left_shift_ + base_offset, multiplier->right_shift_ - base_offset) +
                              quant_out->zp_;
          out_value = out_value > INT8_MAX ? INT8_MAX : out_value;
          out_value = out_value < INT8_MIN ? INT8_MIN : out_value;
          output_data[out_offset] = (int8_t)out_value;
        }
      }
    }
  }

  return NNACL_OK;
}
