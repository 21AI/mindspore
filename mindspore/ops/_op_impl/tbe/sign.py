# Copyright 2020 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

"""Sign op"""
from mindspore.ops.op_info_register import op_info_register, TBERegOp, DataType

sign_op_info = TBERegOp("Sign") \
    .fusion_type("ELEMWISE") \
    .async_flag(False) \
    .binfile_name("sign.so") \
    .compute_cost(10) \
    .kernel_name("sign") \
    .partial_flag(True) \
    .op_pattern("formatAgnostic") \
    .input(0, "x", None, "required", None) \
    .output(0, "y", True, "required", "all") \
    .dtype_format(DataType.F16_Default, DataType.F16_Default) \
    .dtype_format(DataType.F32_Default, DataType.F32_Default) \
    .dtype_format(DataType.I32_Default, DataType.I32_Default) \
    .get_op_info()


@op_info_register(sign_op_info)
def _sign_tbe():
    """Sign TBE register"""
    return
