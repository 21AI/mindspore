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

#include "tools/converter/parser/tf/tf_util.h"
#include <cstdio>
#include <fstream>
#include <string>
#include "google/protobuf/io/zero_copy_stream_impl.h"

namespace mindspore {
namespace lite {
bool TensorFlowUtils::FindAttrValue(const tensorflow::NodeDef &nodeDef, const std::string &attr_name,
                                    tensorflow::AttrValue *attr_value) {
  const google::protobuf::Map<std::string, tensorflow::AttrValue> &attr = nodeDef.attr();
  const google::protobuf::Map<std::string, tensorflow::AttrValue>::const_iterator it = attr.find(attr_name);
  if (it != attr.end()) {
    *attr_value = it->second;
    return true;
  }
  return false;
}
}  // namespace lite
}  // namespace mindspore