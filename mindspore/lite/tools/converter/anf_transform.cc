/**
 * Copyright 2019 Huawei Technologies Co., Ltd
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

#include "tools/converter/anf_transform.h"
#include <memory>
#include <string>
#include "src/common/log_adapter.h"
#include "tools/optimizer/fusion/conv_biasadd_fusion.h"
#include "tools/optimizer/fusion/conv_activation_fusion.h"
#include "tools/optimizer/fusion/conv_tuple_activation_fusion.h"
#include "tools/optimizer/fusion/conv_scale_fusion.h"
#include "tools/optimizer/fusion/conv_bn_fusion.h"
#include "tools/optimizer/fusion/constant_folding_fusion.h"
#include "tools/optimizer/fusion/quant_dtype_cast_fusion.h"
#include "tools/optimizer/fusion/layer_norm_fusion.h"
#include "tools/optimizer/fusion/batchmatmul_fusion.h"
#include "tools/optimizer/fusion/sigmoid_mul_fusion.h"
#include "tools/optimizer/graph/identity_remove_pass.h"
#include "tools/optimizer/graph/weight_format_hardcode_pass.h"
#include "tools/optimizer/graph/weight_format_transform_pass.h"
#include "tools/optimizer/graph/clip_convert_activation_pass.h"
#include "tools/optimizer/graph/unused_cast_node_remove_pass.h"
#include "tools/optimizer/graph/unused_transpose_node_remove_pass.h"
#include "tools/converter/quantizer/post_training_quantizer.h"
#include "tools/converter/quantizer/quant_cast.h"
#include "tools/converter/quantizer/weight_quantizer.h"

using std::string;
namespace mindspore {
namespace lite {
AnfTransform::AnfTransform() = default;

AnfTransform::~AnfTransform() = default;

FuncGraphPtr AnfTransform::Transform(const FuncGraphPtr &old_graph, const converter::Flags *config) {
  MS_ASSERT(nullptr != old_graph);
  if (config == nullptr) {
    MS_LOG(ERROR) << "config shoud be specified";
    return nullptr;
  }
  // fusion const_fold
  auto optimizer = std::make_shared<opt::GraphOptimizer>();
  auto fusion_pm = std::make_shared<opt::PassManager>("anf fusion pass manager", false);
  auto graph_pm = std::make_shared<opt::PassManager>("anf graph pass manager", true);

  // for now - trainning is not supporting fuse operations
  if (config != nullptr && !config->trainModel) {
    // remove quantdtype when awaretraining
    if (config->fmk == lite::converter::FmkType_ONNX) {
      auto remove_identity_pass = std::make_shared<opt::RemoveIdentityOpPass>();
      remove_identity_pass->SetFmkType(config->fmk);
      fusion_pm->AddPass(remove_identity_pass);
    }
    fusion_pm->AddPass(std::make_shared<opt::ConvBiasaddFusion>());
    fusion_pm->AddPass(std::make_shared<opt::ConvBatchNormFusion>());
    fusion_pm->AddPass(std::make_shared<opt::ConvScaleFusion>());
    fusion_pm->AddPass(std::make_shared<opt::LayerNormFusion>());
    fusion_pm->AddPass(std::make_shared<opt::BatchMatMulFusion>());
    fusion_pm->AddPass(std::make_shared<opt::SigmoidMulFusion>());
    fusion_pm->AddPass(std::make_shared<opt::ConvActivationFusion>());
    fusion_pm->AddPass(std::make_shared<opt::ConvTupleActivationFusion>());
  }
  auto weight_format_hardcode_pass = std::make_shared<opt::WeightFormatHardCodePass>();
  weight_format_hardcode_pass->SetFmkType(config->fmk);
  weight_format_hardcode_pass->SetQuantType(config->quantType);
  graph_pm->AddPass(weight_format_hardcode_pass);
  auto weight_format_transform_pass = std::make_shared<opt::WeightFormatTransformPass>();
  weight_format_transform_pass->SetFmkType(config->fmk);
  weight_format_transform_pass->SetQuantType(config->quantType);
  graph_pm->AddPass(weight_format_transform_pass);

  if (config->fmk == lite::converter::FmkType_MS) {
    auto remove_unused_cast_pass = std::make_shared<opt::RemoveUnusedCastOpPass>();
    if (remove_unused_cast_pass == nullptr) {
      MS_LOG(ERROR) << "RemoveUnusedCastOpPass shoud be specified";
      return nullptr;
    }
    remove_unused_cast_pass->SetFmkType(config->fmk);
    fusion_pm->AddPass(remove_unused_cast_pass);
  }
  if (config->fmk == lite::converter::FmkType_ONNX) {
    auto remove_unused_transpose_pass = std::make_shared<opt::RemoveUnusedTransposeOpPass>();
    if (remove_unused_transpose_pass == nullptr) {
      MS_LOG(ERROR) << "RemoveUnusedTransposeOpPass shoud be specified";
      return nullptr;
    }
    remove_unused_transpose_pass->SetFmkType(config->fmk);
    fusion_pm->AddPass(remove_unused_transpose_pass);
  }
  auto const_fold_pm = std::make_shared<opt::PassManager>("const fold fusion pass manager", false);
  auto inne_context_ptr = std::make_shared<lite::InnerContext>();
  inne_context_ptr->Init();
  const_fold_pm->AddPass(std::make_shared<opt::ConstFoldPass>(inne_context_ptr));
  auto convert_pm = std::make_shared<opt::PassManager>("anf graph convert pass manager", true);
  convert_pm->AddPass(std::make_shared<opt::ClipConvertActivationPass>());
  optimizer->AddPassManager(const_fold_pm);
  optimizer->AddPassManager(convert_pm);
  optimizer->AddPassManager(fusion_pm);
  optimizer->AddPassManager(graph_pm);
  auto new_graph = optimizer->Optimize(old_graph);
  if (new_graph == nullptr) {
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(RET_NULL_PTR);
    return nullptr;
  }
  // quant
  if (config->quantType == schema::QuantType_PostTraining) {
    this->mQuantizer = std::make_unique<quant::PostTrainingQuantizer>(new_graph, config->configFile, 8);
    if (mQuantizer == nullptr) {
      MS_LOG(ERROR) << "New PostTrainingQuantizer failed";
      ReturnCode::GetSingleReturnCode()->UpdateReturnCode(RET_MEMORY_FAILED);
      return nullptr;
    }
  } else if (config->quantType == schema::QuantType_WeightQuant) {
    if (quant::WeightQuantizer::WeightQuantInputCheck(config) != RET_OK) {
      MS_LOG(ERROR) << "weight quant input param error";
      ReturnCode::GetSingleReturnCode()->UpdateReturnCode(RET_ERROR);
      return nullptr;
    }
    this->mQuantizer = std::make_unique<quant::WeightQuantizer>(new_graph, config->quantWeightSize,
                                                                config->quantWeightChannel, config->bitNum);
    if (mQuantizer == nullptr) {
      MS_LOG(ERROR) << "New WeightQuantizer failed";
      ReturnCode::GetSingleReturnCode()->UpdateReturnCode(RET_MEMORY_FAILED);
      return nullptr;
    }
  }
  if (mQuantizer != nullptr) {
    mQuantizer->flags = *config;
    auto status = mQuantizer->DoQuantize(new_graph);
    if (status != RET_OK) {
      MS_LOG(ERROR) << "Quant failed " << status;
      ReturnCode::GetSingleReturnCode()->UpdateReturnCode(status);
      return nullptr;
    }
  }

  return new_graph;
}
}  // namespace lite
}  // namespace mindspore
