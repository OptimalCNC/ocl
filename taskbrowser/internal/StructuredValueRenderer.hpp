#pragma once

#include <rtt/base/DataSourceBase.hpp>

#include <cstddef>
#include <string>

namespace OCL::detail {

struct StructuredValueRenderOptions {
  std::size_t compact_width{100};
  std::size_t sequence_items{3};
  std::size_t max_structural_depth{3};
  std::size_t structure_members{20};
  std::size_t max_result_bytes{4096};
  std::size_t indentation{2};
  bool hexadecimal{false};
};

enum class StructuredValueRenderStatus {
  rendered,
  evaluation_failed,
};

struct StructuredValueRenderResult {
  StructuredValueRenderStatus status;
  std::string text;
};

StructuredValueRenderResult renderStructuredValue(
    RTT::base::DataSourceBase::shared_ptr source,
    const StructuredValueRenderOptions &options = {});

} // namespace OCL::detail
