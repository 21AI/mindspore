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
#include "tools/converter/parser/tf/tf_node_parser.h"
#include <string>
#include <vector>

namespace mindspore {
namespace lite {
STATUS TFNodeParser::AddOpInput(const tensorflow::NodeDef &tf_op, const int idx, std::vector<std::string> *inputs) {
  if (tf_op.input_size() <= idx) {
    MS_LOG(ERROR) << "input idx is greater than op input size";
    return RET_PARAM_INVALID;
  }
  inputs->push_back(tf_op.input(idx));
  return RET_OK;
}
}  // namespace lite
}  // namespace mindspore
