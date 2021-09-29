/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/core/common/balanced_splitter.h"
#include "oneflow/core/framework/nd_sbp.h"
#include "oneflow/core/boxing/eager_boxing_interpreter.h"
#include "oneflow/core/common/decorator.h"
#include "oneflow/core/functional/functional.h"

namespace oneflow {

namespace {

bool RawIsPartialSumSbp(Symbol<cfg::SbpParallel> sbp_parallel) {
  return sbp_parallel->has_partial_sum_parallel();
}

static constexpr auto* IsPartialSumSbp = DECORATE(&RawIsPartialSumSbp, ThreadLocal);

bool RawIsSplitSbp(Symbol<cfg::SbpParallel> sbp_parallel) {
  return sbp_parallel->has_split_parallel();
}

static constexpr auto* IsSplitSbp = DECORATE(&RawIsSplitSbp, ThreadLocal);

Maybe<void> RawCheckNaivePToS(Symbol<PlacedNdSbp> in, Symbol<PlacedNdSbp> out) {
  CHECK_EQ_OR_RETURN(in->nd_sbp()->sbp_parallel_size(), 1);
  CHECK_EQ_OR_RETURN(out->nd_sbp()->sbp_parallel_size(), 1);

  CHECK_OR_RETURN(IsPartialSumSbp(in->nd_sbp()->sbp_parallel(0)));
  CHECK_OR_RETURN(IsSplitSbp(out->nd_sbp()->sbp_parallel(0)));

  CHECK_OR_RETURN(in->placement() != out->placement());
  CHECK_EQ_OR_RETURN(in->placement()->device_tag(), out->placement()->device_tag());
  return Maybe<void>::Ok();
}

static constexpr auto* CheckNaivePToS = DECORATE(&RawCheckNaivePToS, ThreadLocal);

}  // namespace

Maybe<one::Tensor> NaivePToS(const std::shared_ptr<one::Tensor>& tensor, Symbol<PlacedNdSbp> in,
                             Symbol<PlacedNdSbp> out) {
  const auto& tensor_nd_sbp = JUST(tensor->nd_sbp());
  CHECK_OR_RETURN(tensor_nd_sbp == in->nd_sbp());
  const auto& tensor_placement = JUST(tensor->parallel_desc());
  CHECK_OR_RETURN(tensor_placement == in->placement());
  const auto& sbp_list = JUST(GetSbpList(out->nd_sbp()));
  std::shared_ptr<one::Tensor> local_tensor = JUST(tensor->cur_rank_phy_tensor());
  {
    const auto& in_parallel_id = JUST(GetParallelId4CurrentProcessCtx(tensor_placement));
    const auto& out_parallel_id = JUST(GetParallelId4CurrentProcessCtx(out->placement()));
    if (in_parallel_id->has_value() || out_parallel_id->has_value()) {
      local_tensor = JUST(one::functional::EagerPToS(
          local_tensor, tensor_placement, out->placement(), *sbp_list, *tensor->shape()));
    }
  }

  return JUST(one::functional::LocalToConsistent(local_tensor, out->placement(), *sbp_list,
                                                 *tensor->shape(), tensor->dtype()));
}

COMMAND(RegisterBoxingFunction("naive-p-to-s", CheckNaivePToS, &NaivePToS));

}  // namespace oneflow