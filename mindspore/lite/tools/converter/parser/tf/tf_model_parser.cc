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
 * distributed under the License is distributed on an AS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tools/converter/parser/tf/tf_model_parser.h"
#include <functional>
#include <regex>
#include <set>
#include "src/common/log_adapter.h"
#include "src/common/utils.h"
#include "src/param_value_lite.h"
#include "tools/common/graph_util.h"
#include "tools/common/protobuf_utils.h"
#include "tools/converter/parser/tf/tf_node_parser_registry.h"

namespace mindspore {
namespace lite {
namespace {
// subgraph node input may be a:output:0/a:z:0
std::string GetFlattenNodeName(std::string input_name) {
  std::regex re("\\:+");
  std::vector<std::string> input_splits(std::sregex_token_iterator(input_name.begin(), input_name.end(), re, -1),
                                        std::sregex_token_iterator());
  if (input_splits.size() == 3) {
    if (input_splits[2] == "0") {
      input_name = input_splits[0];
    } else {
      input_name = input_splits[0] + input_splits[2];  // multi output node
    }
  }
  return input_name;
}
AnfNodePtr GetAnfNode(const std::string &name, const std::unordered_map<std::string, AnfNodePtr> &anf_node_map) {
  AnfNodePtr ret = nullptr;
  if (anf_node_map.find(name) != anf_node_map.end()) {
    ret = anf_node_map.at(name);
  } else if (anf_node_map.find(name + ":0") != anf_node_map.end()) {
    ret = anf_node_map.at(name + ":0");
  }
  return ret;
}

std::string GetOriginInputName(const tensorflow::NodeDef &node,
                               const std::map<std::string, const tensorflow::NodeDef *> &tf_graph_nodes) {
  if (node.op() != "Identity" && node.op() != "StopGradient") {
    return node.name();
  }
  auto tmp_node = &node;
  while (tmp_node->op() == "Identity" || tmp_node->op() == "StopGradient") {
    if (tf_graph_nodes.find(tmp_node->input(0)) == tf_graph_nodes.end()) {
      return tmp_node->input(0);
    }
    tmp_node = tf_graph_nodes.at(tmp_node->input(0));
  }
  return tmp_node->name();
}
}  // namespace

STATUS TFModelParser::ConvertConstTensor(const tensorflow::AttrValue &attr_value, const TypeId &type,
                                         const ParameterPtr &parameter, std::vector<int64_t> *shape_vector) {
  MS_ASSERT(parameter != nullptr);
  MS_ASSERT(shape_vector != nullptr);
  const tensorflow::TensorProto &tensor_proto = attr_value.tensor();
  const tensorflow::TensorShapeProto &tensor_shape = tensor_proto.tensor_shape();
  int shape_size = 1;
  shape_vector->clear();
  for (int i = 0; i < tensor_shape.dim_size(); i++) {
    shape_vector->push_back(tensor_shape.dim(i).size());
    shape_size *= tensor_shape.dim(i).size();
  }

  int tensor_size;
  auto param_value = std::make_shared<ParamValueLite>();
  if (param_value == nullptr) {
    MS_LOG(ERROR) << "param_value is nullptr";
    return RET_ERROR;
  }
  if (type == kNumberTypeFloat32 || type == kNumberTypeFloat) {
    auto tensor_data = new (std::nothrow) float[shape_size];
    if (tensor_proto.float_val_size() == 1) {
      float value = tensor_proto.float_val(0);
      for (int i = 0; i < shape_size; i++) {
        tensor_data[i] = value;
      }
    }
    if (tensor_proto.tensor_content().size() == shape_size * sizeof(float)) {
      const auto addr = reinterpret_cast<const float *>(tensor_proto.tensor_content().data());
      auto ret = ::memcpy_s(tensor_data, shape_size * sizeof(float), addr, shape_size * sizeof(float));
      if (ret != EOK) {
        MS_LOG(ERROR) << "memcpy_s failed";
        delete[] tensor_data;
        return RET_ERROR;
      }
    }
    tensor_size = shape_size * sizeof(float);
    param_value->SetTensorData(tensor_data, tensor_size);
  } else if (type == kNumberTypeInt32) {
    auto tensor_data = new (std::nothrow) int[shape_size];
    if (tensor_proto.int_val_size() == 1) {
      int value = tensor_proto.int_val(0);
      for (int i = 0; i < shape_size; i++) {
        tensor_data[i] = value;
      }
    }
    if (tensor_proto.tensor_content().size() == shape_size * sizeof(int32_t)) {
      const auto addr = reinterpret_cast<const int32_t *>(tensor_proto.tensor_content().data());
      auto ret = ::memcpy_s(tensor_data, shape_size * sizeof(int32_t), addr, shape_size * sizeof(int32_t));
      if (ret != EOK) {
        MS_LOG(ERROR) << "memcpy_s failed";
        delete[] tensor_data;
        return RET_ERROR;
      }
    }
    tensor_size = shape_size * sizeof(int);
    param_value->SetTensorData(tensor_data, tensor_size);
  } else if (type == kNumberTypeBool) {
    auto tensor_data = new (std::nothrow) int[shape_size];
    if (tensor_proto.bool_val_size() == 1) {
      int value = tensor_proto.bool_val(0);
      for (int i = 0; i < shape_size; i++) {
        tensor_data[i] = value;
      }
    }
    tensor_size = shape_size * sizeof(int);
    param_value->SetTensorData(tensor_data, tensor_size);
  } else {
    MS_LOG(ERROR) << "Unsupport dataType: " << type;
    return RET_ERROR;
  }

  std::vector<int> param_shape(shape_vector->begin(), shape_vector->end());
  param_value->set_tensor_shape(param_shape);
  param_value->set_tensor_type(type);
  param_value->set_format(schema::Format::Format_NHWC);
  parameter->set_default_param(param_value);
  return RET_OK;
}

STATUS TFModelParser::ConvertParameter(const tensorflow::NodeDef &node, const ParameterPtr &parameter,
                                       std::unordered_map<std::string, AnfNodePtr> *anf_node_map) {
  MS_ASSERT(node != nullptr);
  MS_ASSERT(parameter != nullptr);

  tensorflow::AttrValue attr_value;
  TypeId type = kNumberTypeFloat32;
  if (TensorFlowUtils::FindAttrValue(node, "dtype", &attr_value)) {
    type = TensorFlowUtils::GetTFDataType(attr_value.type());
  }
  auto type_ptr = TypeIdToType(type);

  std::vector<int> shape;
  if (TensorFlowUtils::FindAttrValue(node, "shape", &attr_value)) {
    auto &shape_attr = attr_value.shape();
    for (int i = 0; i < shape_attr.dim_size(); ++i) {
      shape.push_back(shape_attr.dim(i).size());
    }
  }
  std::vector<int64_t> shape_vector(shape.begin(), shape.end());

  if (TensorFlowUtils::FindAttrValue(node, "value", &attr_value)) {
    MS_LOG(INFO) << "Found value attr, means it has default value";
    auto status = ConvertConstTensor(attr_value, type, parameter, &shape_vector);
    if (status != RET_OK) {
      return status;
    }
  } else {
    graph_input_names_.emplace_back(node.name());  // only root graph need set graph input names
  }

  auto abstract_tensor = std::make_shared<abstract::AbstractTensor>(type_ptr, shape_vector);
  if (abstract_tensor == nullptr) {
    MS_LOG(ERROR) << "abstract_tensor is nullptr";
    return RET_ERROR;
  }
  parameter->set_name(node.name());
  parameter->set_abstract(abstract_tensor);

  (*anf_node_map)[node.name()] = parameter;
  (*anf_node_map)[node.name() + ":0"] = parameter;

  return RET_OK;
}

STATUS TFModelParser::ConvertGraphInputsAndConsts(
  const std::map<std::string, const tensorflow::NodeDef *> &tf_graph_nodes, const FuncGraphPtr &anf_graph,
  std::unordered_map<std::string, AnfNodePtr> *anf_node_map) {
  for (auto &pair : tf_graph_nodes) {
    bool have_data_depend = false;
    for (int i = 0; i < pair.second->input_size(); ++i) {
      auto name = pair.second->input(i);
      if (!name.empty() && name[0] != '^') {  // control_depend input start with "^"
        have_data_depend = true;
        break;
      }
    }
    if (!have_data_depend) {
      auto parameter = anf_graph->add_parameter();
      if (ConvertParameter(*pair.second, parameter, anf_node_map) != RET_OK) {
        MS_LOG(ERROR) << "convert Parameter Node failed";
        return RET_ERROR;
      }
    }
  }
  return RET_OK;
}
FuncGraphPtr paserTfFuction() { return nullptr; }
FuncGraphPtr TFModelParser::Parse(const std::string &modelFile, const std::string &weightFile,
                                  const QuantType &quantType) {
  auto status = ValidateFileStr(modelFile, ".pb");
  if (status != RET_OK) {
    MS_LOG(ERROR) << "INPUT ILLEGAL: modelFile must be *.pb";
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(status);
    return nullptr;
  }
  tf_root_graph_ = std::make_unique<tensorflow::GraphDef>();
  if (tf_root_graph_ == nullptr) {
    MS_LOG(ERROR) << "tf_root_graph_ is nullptr";
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(RET_ERROR);
    return nullptr;
  }
  status = ReadProtoFromBinaryFile((const char *)modelFile.c_str(), tf_root_graph_.get());
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Open modelFile for TF converter failed!";
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(RET_ERROR);
    return nullptr;
  }
  anf_root_graph_ = std::make_shared<FuncGraph>();
  if (anf_root_graph_ == nullptr) {
    MS_LOG(ERROR) << "funGraphPtr is nullptr";
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(RET_ERROR);
    return nullptr;
  }

  for (int i = 0; i < tf_root_graph_->node_size(); i++) {
    auto &node_def = tf_root_graph_->node(i);
    tf_root_graph_nodes_[node_def.name()] = &node_def;
  }

  status = ConvertGraphInputsAndConsts(tf_root_graph_nodes_, anf_root_graph_, &anf_root_node_map_);
  if (status != RET_OK) {
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(status);
    return nullptr;
  }
  bool success_flag = true;
  for (int i = 0; i < tf_root_graph_->node_size(); i++) {
    auto &node_def = tf_root_graph_->node(i);
    status = ConvertOps(node_def, tf_root_graph_nodes_, anf_root_graph_, &anf_root_node_map_);
    if (status != RET_OK) {
      success_flag = false;
    }
  }
  if (!success_flag) {
    MS_LOG(ERROR) << "Convert ops failed.";
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(status);
    return nullptr;
  }
  status = ConvertRootGraphOutputs();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Convert graph outputs failed.";
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(status);
    return nullptr;
  }

  status = ConvertSubgraph();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Convert subgraph failed.";
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(status);
    return nullptr;
  }

  return anf_root_graph_;
}
STATUS TFModelParser::ConvertSubgraph() {
  auto graph_def_liarary = tf_root_graph_->library();
  auto subgraph_size = graph_def_liarary.function_size();
  std::map<CNodePtr, FuncGraphPtr> while_cond_map;
  std::map<CNodePtr, FuncGraphPtr> while_body_map;
  std::vector<ParameterPtr> sub_graph_inputs;
  for (int i = 0; i < subgraph_size; i++) {
    auto &tf_sub_fuction = graph_def_liarary.function(i);
    auto &tf_sub_signature = tf_sub_fuction.signature();
    auto input_arg_size = tf_sub_signature.input_arg_size();

    auto &sub_graph_name = tf_sub_signature.name();
    if (!function_while_map_.count(sub_graph_name)) {
      MS_LOG(ERROR) << "function map not contains sub graph name." << sub_graph_name;
      return RET_ERROR;
    }
    auto while_cnode = function_while_map_[sub_graph_name]->cast<CNodePtr>();
    if (while_cnode == nullptr || static_cast<int>(while_cnode->inputs().size()) != input_arg_size + 1) {
      MS_LOG(ERROR) << "while cnode  not equal input arg size";
      return RET_ERROR;
    }

    FuncGraphPtr sub_func_graph = std::make_shared<FuncGraph>();
    std::unordered_map<std::string, AnfNodePtr> anf_sub_node_map;
    // convert sub graph inputs
    for (int j = 0; j < input_arg_size; j++) {
      auto &input_arg = tf_sub_signature.input_arg(j);
      auto paramter = sub_func_graph->add_parameter();
      paramter->set_name(input_arg.name());
      anf_sub_node_map[input_arg.name()] = paramter;
      sub_graph_inputs.emplace_back(paramter);
    }
    std::map<std::string, const tensorflow::NodeDef *> tf_sub_node_map;
    for (int j = 0; j < tf_sub_fuction.node_def_size(); j++) {
      auto &node_def = tf_sub_fuction.node_def(j);
      tf_sub_node_map[node_def.name()] = &node_def;
    }
    STATUS status = RET_OK;
    status = ConvertGraphInputsAndConsts(tf_sub_node_map, sub_func_graph, &anf_sub_node_map);
    if (status != RET_OK) {
      MS_LOG(ERROR) << "Convert subgraph consts failed";
      return status;
    }
    // convert sub graph ops
    for (int j = 0; j < tf_sub_fuction.node_def_size(); j++) {
      auto &node_def = tf_sub_fuction.node_def(j);
      status = ConvertOps(node_def, tf_sub_node_map, sub_func_graph, &anf_sub_node_map);
      if (status != RET_OK) {
        MS_LOG(ERROR) << "Convert subgraph ops failed.";
        ReturnCode::GetSingleReturnCode()->UpdateReturnCode(status);
        return RET_ERROR;
      }
    }

    // convert subgraph outputs
    std::vector<AnfNodePtr> sub_output_nodes;
    auto &subgraph_ret = tf_sub_fuction.ret();
    for (auto &t : subgraph_ret) {
      MS_LOG(INFO) << "subret " << t.first << " " << t.second;
      auto tf_output_name = GetFlattenNodeName(t.second);
      AnfNodePtr anf_node = nullptr;
      if (tf_sub_node_map.find(tf_output_name) == tf_sub_node_map.end()) {
        anf_node = GetAnfNode(tf_output_name, anf_sub_node_map);
      } else {
        auto tf_real_name = GetOriginInputName(*tf_sub_node_map[tf_output_name], tf_sub_node_map);
        anf_node = GetAnfNode(tf_real_name, anf_sub_node_map);
      }
      if (anf_node == nullptr) {
        MS_LOG(ERROR) << "can't find anf node,tf node flatten name" << tf_output_name;
        return RET_ERROR;
      }
      sub_output_nodes.push_back(anf_node);
    }
    status = MakeAnfGraphOutputs(&sub_output_nodes, sub_func_graph);
    if (status != RET_OK) {
      MS_LOG(ERROR) << "cmake anf graph outputs node error";
      return status;
    }

    // add while cond body function to while node input
    if (sub_graph_name.find("cond") != std::string::npos) {
      while_cond_map[while_cnode] = sub_func_graph;
    } else {
      while_body_map[while_cnode] = sub_func_graph;
    }
    // hardcode subgraph inputs name
    for (size_t j = 0; j < sub_graph_inputs.size(); j++) {
      sub_graph_inputs[j]->set_name("graph" + std::to_string(i) + "_input_" + std::to_string(j) + "parameter");
    }
    MS_LOG(INFO) << "parse subgraph end:" << sub_graph_name;
  }
  auto status = WhileNodePostProcess(while_cond_map, while_body_map);
  if (status != RET_OK) {
    MS_LOG(ERROR) << "while node post process failed";
    return status;
  }
  return RET_OK;
}
STATUS TFModelParser::WhileNodePostProcess(const std::map<CNodePtr, FuncGraphPtr> &while_cond_map,
                                           const std::map<CNodePtr, FuncGraphPtr> &while_body_map) {
  if (while_cond_map.size() != while_body_map.size()) {
    MS_LOG(ERROR) << "while cond body size error";
    return RET_ERROR;
  }
  std::vector<FuncGraphPtr> roots = {anf_root_graph_};
  auto root_func_manager = std::make_shared<FuncGraphManager>(roots);
  anf_root_graph_->set_manager(root_func_manager);
  for (auto &kv : while_cond_map) {
    auto while_node = kv.first;
    auto &cond_sub_graph = kv.second;
    auto &body_sub_graph = while_body_map.at(while_node);
    cond_sub_graph->set_manager(root_func_manager);
    body_sub_graph->set_manager(root_func_manager);
    auto cond_value_node = NewValueNode(cond_sub_graph);
    auto body_value_node = NewValueNode(body_sub_graph);
    auto new_while_inputs = while_node->cast<CNodePtr>()->inputs();
    new_while_inputs[0] = cond_value_node;
    new_while_inputs.insert(new_while_inputs.begin() + 1, body_value_node);
    auto new_while_node = anf_root_graph_->NewCNode(new_while_inputs);
    new_while_node->set_abstract(while_node->abstract());
    root_func_manager->Replace(while_node, new_while_node);
  }
  return RET_OK;
}

schema::MetaGraphT *TFModelParser::ParseToFb(const std::string &modelFile, const std::string &weightFile,
                                             const QuantType &quantType) {
  MS_LOG(ERROR) << "TF Model Parser not return MetaGraph, use TFModelParser::Parse instead";
  return nullptr;
}

STATUS TFModelParser::ConvertInputNodes(const tensorflow::NodeDef &node_def,
                                        const std::vector<std::string> &input_names,
                                        const std::map<std::string, const tensorflow::NodeDef *> &tf_node_map,
                                        const std::unordered_map<std::string, AnfNodePtr> &anf_node_map,
                                        std::vector<AnfNodePtr> *inputs) {
  MS_ASSERT(node_def != nullptr);
  // parse inputs
  for (size_t j = 0; j < input_names.size(); j++) {
    std::string input_name = input_names[j];  // input may be produced by multi-outputs node
    // subgraph input name x:output:index,need flatten
    auto flatten_input_name = GetFlattenNodeName(input_name);
    if (tf_node_map.find(flatten_input_name) != tf_node_map.end()) {
      auto input_node = tf_node_map.at(flatten_input_name);
      flatten_input_name = GetOriginInputName(*input_node, tf_node_map);
    }
    auto input = GetAnfNode(flatten_input_name, anf_node_map);
    if (input == nullptr) {
      MS_LOG(ERROR) << node_def.name() << " input " << j << ": " << input_name << " can't find parsed in_nodes";
      return RET_ERROR;
    }
    inputs->emplace_back(input);
  }
  return RET_OK;
}

STATUS TFModelParser::ConvertOutputTensor(const tensorflow::NodeDef &op, const CNodePtr &anf_node,
                                          std::unordered_map<std::string, AnfNodePtr> *anf_node_map,
                                          const FuncGraphPtr &anf_graph, int output_size) {
  MS_ASSERT(op != nullptr);
  MS_ASSERT(anf_node != nullptr);
  MS_ASSERT(anf_graph != nullptr);
  if (output_size == 1) {
    std::vector<int64_t> shape_vector;
    anf_node->set_abstract(std::make_shared<abstract::AbstractTensor>(kFloat32, shape_vector));
    anf_node_map->insert(std::pair(op.name(), anf_node));
  } else {
    AbstractBasePtrList abstractList;
    for (int output_idx = 0; output_idx < output_size; output_idx++) {
      std::vector<int64_t> shape_vector;
      abstractList.emplace_back(std::make_shared<abstract::AbstractTensor>(kFloat32, shape_vector));
      auto tupleGetItemPrimPtr = GetTupleGetItemPrim();
      if (tupleGetItemPrimPtr == nullptr) {
        MS_LOG(ERROR) << "GetTupleGetItemPrim return nullptr";
        return RET_NULL_PTR;
      }
      auto tupleGetItemPrim = NewValueNode(tupleGetItemPrimPtr);
      auto getItemValue = NewValueNode(MakeValue<int>(output_idx));
      std::vector<AnfNodePtr> inputs{tupleGetItemPrim, anf_node, getItemValue};
      CNodePtr getItemCNode = anf_graph->NewCNode(inputs);
      std::string output_item_name = anf_node->fullname_with_scope() + "_getitem_" + std::to_string(output_idx);
      getItemCNode->set_fullname_with_scope(output_item_name);
      anf_node_map->insert(std::pair(op.name() + ":" + std::to_string(output_idx), getItemCNode));
    }
    anf_node->set_abstract(std::make_shared<abstract::AbstractTuple>(abstractList));
  }
  return RET_OK;
}

STATUS TFModelParser::ConvertOps(const tensorflow::NodeDef &node_def,
                                 const std::map<std::string, const tensorflow::NodeDef *> &tf_node_map,
                                 const FuncGraphPtr &func_graph_ptr,
                                 std::unordered_map<std::string, AnfNodePtr> *anf_node_map) {
  MS_ASSERT(node_def != nullptr);
  MS_ASSERT(func_graph_ptr != nullptr);
  NoSupportOp::GetInstance()->SetFmkType("TF");
  STATUS status = RET_OK;
  const auto &op_type = node_def.op();
  if (op_type == "Placeholder" || op_type == "Const" || op_type == "Identity" || op_type == "StopGradient") {
    return RET_OK;
  }

  auto node_parser = TFNodeParserRegistry::GetInstance()->GetNodeParser(op_type);
  if (node_parser == nullptr) {
    NoSupportOp::GetInstance()->InsertOp(op_type);
    MS_LOG(ERROR) << "cannot find node parser:" << op_type;
    return RET_NOT_FIND_OP;
  }
  PrimitiveC *primitiveC = nullptr;
  int output_size;
  std::vector<std::string> input_names;
  status = node_parser->Parse(node_def, tf_node_map, &primitiveC, &input_names, &output_size);
  if (status != RET_OK) {
    MS_LOG(ERROR) << "node " << op_type << " parser failed";
    return RET_ERROR;
  }
  auto value_node = NewValueNode(std::shared_ptr<PrimitiveC>(primitiveC));
  if (value_node == nullptr) {
    MS_LOG(ERROR) << "value_node is nullptr";
    return RET_ERROR;
  }
  std::vector<AnfNodePtr> inputs = {value_node};
  status = ConvertInputNodes(node_def, input_names, tf_node_map, *anf_node_map, &inputs);
  if (status != RET_OK) {
    return status;
  }
  // control_depends are not processed currently
  auto anf_node = func_graph_ptr->NewCNode(inputs);
  anf_node->set_fullname_with_scope(node_def.name());
  if (op_type == "StatelessWhile" || op_type == "while") {
    MS_LOG(INFO) << "find while node:" << node_def.name();
    tensorflow::AttrValue attr_value;
    if (TensorFlowUtils::FindAttrValue(node_def, "body", &attr_value)) {
      auto body_name = attr_value.func().name();
      function_while_map_[body_name] = anf_node;
      MS_LOG(DEBUG) << "parse body name:" << body_name;
    }
    if (TensorFlowUtils::FindAttrValue(node_def, "cond", &attr_value)) {
      auto cond_name = attr_value.func().name();
      function_while_map_[cond_name] = anf_node;
      MS_LOG(DEBUG) << "parse cond name:" << cond_name;
    }
  }

  status = ConvertOutputTensor(node_def, anf_node, anf_node_map, func_graph_ptr, output_size);
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Convert output tensors for " << anf_node->fullname_with_scope() << " failed.";
    ReturnCode::GetSingleReturnCode()->UpdateReturnCode(status);
    return RET_ERROR;
  }
  return status;
}

STATUS TFModelParser::ConvertRootGraphOutputs() {
  // because output of intermediate node in anf graph may also be output tensors, we search output tensors in
  // tf_root_graph_nodes_ but not anf_root_node_map_
  std::set<std::string> all_node_inputs;
  std::vector<AnfNodePtr> output_nodes;
  for (auto &pair : tf_root_graph_nodes_) {
    for (int i = 0; i < pair.second->input_size(); ++i) {
      all_node_inputs.insert(pair.second->input(i));
    }
  }
  for (auto &pair : tf_root_graph_nodes_) {
    auto it = all_node_inputs.find(pair.first);
    if (it == all_node_inputs.end() && pair.second->input_size() > 0) {  // output node not constraint to Identity
      auto origin_name = GetOriginInputName(*(pair.second), tf_root_graph_nodes_);
      auto anf_node = GetAnfNode(origin_name, anf_root_node_map_);
      if (anf_node == nullptr) {
        MS_LOG(ERROR) << "can't find anf node";
        return RET_ERROR;
      }
      output_nodes.push_back(anf_node);
      graph_output_names_.push_back(anf_node->fullname_with_scope());
    }
  }
  auto status = MakeAnfGraphOutputs(&output_nodes, anf_root_graph_);
  if (status != RET_OK) {
    MS_LOG(ERROR) << "make anf graph outputs node error";
    return status;
  }
  return RET_OK;
}
STATUS TFModelParser::MakeAnfGraphOutputs(std::vector<AnfNodePtr> *output_nodes, const FuncGraphPtr &anf_graph) {
  if (output_nodes->empty() || anf_graph == nullptr) {
    MS_LOG(ERROR) << "anf output nodes empty or  null anf graph";
    return RET_ERROR;
  }
  if (output_nodes->size() > 1) {
    std::vector<AnfNodePtr> *make_tuple_inputs = output_nodes;
    auto make_tuple_prim_ptr = GetMakeTuplePrim();
    if (make_tuple_prim_ptr == nullptr) {
      MS_LOG(ERROR) << "GetMakeTuplePrim return nullptr";
      return RET_NULL_PTR;
    }
    auto make_tuple_prim = NewValueNode(make_tuple_prim_ptr);
    make_tuple_inputs->insert(make_tuple_inputs->begin(), make_tuple_prim);
    auto make_tuple_cnode = anf_graph->NewCNode(*make_tuple_inputs);
    make_tuple_cnode->set_fullname_with_scope("return tuple");

    auto return_prim_ptr = GetReturnPrim();
    if (return_prim_ptr == nullptr) {
      MS_LOG(ERROR) << "GetReturnPrim return nullptr";
      return RET_NULL_PTR;
    }
    auto value_node = NewValueNode(return_prim_ptr);
    std::vector<AnfNodePtr> op_inputs = {value_node, make_tuple_cnode};
    auto cnode = anf_graph->NewCNode(op_inputs);
    cnode->set_fullname_with_scope("return");
    anf_graph->set_return(cnode);
  } else {
    auto return_prim_ptr = GetReturnPrim();
    if (return_prim_ptr == nullptr) {
      MS_LOG(ERROR) << "GetReturnPrim return nullptr";
      return RET_NULL_PTR;
    }
    auto value_node = NewValueNode(return_prim_ptr);
    std::vector<AnfNodePtr> op_inputs{value_node, output_nodes->front()};
    auto return_cnode = anf_graph->NewCNode(op_inputs);
    return_cnode->set_fullname_with_scope("return");
    anf_graph->set_return(return_cnode);
  }
  return RET_OK;
}
}  // namespace lite
}  // namespace mindspore
