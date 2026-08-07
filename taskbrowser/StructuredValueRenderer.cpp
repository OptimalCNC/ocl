#include "internal/StructuredValueRenderer.hpp"

#include <rtt/internal/DataSource.hpp>
#include <rtt/internal/DataSources.hpp>
#include <rtt/types/TypeInfo.hpp>

#include <cmath>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace {

using DataSourcePtr = RTT::base::DataSourceBase::shared_ptr;

struct RenderNode {
  enum class Kind { scalar, structure };
  Kind kind{Kind::scalar};
  std::string scalar;
  std::vector<std::pair<std::string, RenderNode>> children;
};

enum class SnapshotStatus { ready, unavailable, evaluation_failed };

struct SnapshotResult {
  SnapshotStatus status;
  DataSourcePtr value;
};

SnapshotResult snapshot(const DataSourcePtr &source) {
  if (!source || source->getTypeInfo() == nullptr) {
    return {SnapshotStatus::evaluation_failed, {}};
  }

  source->reset();
  DataSourcePtr local = source->getTypeInfo()->buildValue();
  if (!local || !local->isAssignable()) {
    return {SnapshotStatus::unavailable, {}};
  }
  if (!local->update(source.get())) {
    return {SnapshotStatus::evaluation_failed, {}};
  }
  return {SnapshotStatus::ready, std::move(local)};
}

template <typename T>
std::optional<std::string> floatingText(const DataSourcePtr &source) {
  auto typed = boost::dynamic_pointer_cast<RTT::internal::DataSource<T>>(source);
  if (!typed) {
    return std::nullopt;
  }

  const T value = typed->value();
  std::ostringstream stream;
  stream << value;
  std::string text = stream.str();
  if (std::isfinite(value) && std::floor(value) == value &&
      text.find('.') == std::string::npos) {
    const std::size_t exponent = text.find_first_of("eE");
    text.insert(exponent == std::string::npos ? text.size() : exponent, ".0");
  }
  return text;
}

std::string scalarText(const DataSourcePtr &source, bool hexadecimal) {
  if (auto text = floatingText<float>(source)) {
    return *text;
  }
  if (auto text = floatingText<double>(source)) {
    return *text;
  }

  std::ostringstream stream;
  stream << (hexadecimal ? std::hex : std::dec) << source;
  return stream.str();
}

RenderNode captureNode(const DataSourcePtr &source,
                       const OCL::detail::StructuredValueRenderOptions &options) {
  RenderNode node;
  if (!source || source->getTypeInfo() == nullptr) {
    return node;
  }

  const auto memberFactory = source->getTypeInfo()->getMemberFactory();
  if (!memberFactory || source->getTypeName() == "String") {
    node.scalar = scalarText(source, options.hexadecimal);
    return node;
  }

  node.kind = RenderNode::Kind::structure;
  const std::vector<std::string> names = source->getMemberNames();
  node.children.reserve(names.size());
  for (std::size_t index = 0; index < names.size(); ++index) {
    node.children.emplace_back(names[index],
                               captureNode(source->getMember(names[index]), options));
  }
  return node;
}

std::string renderCompact(const RenderNode &node) {
  if (node.kind == RenderNode::Kind::scalar) {
    return node.scalar;
  }

  std::string output{"{"};
  for (std::size_t index = 0; index < node.children.size(); ++index) {
    if (index != 0U) {
      output += ", ";
    }
    output += node.children[index].first;
    output += ": ";
    output += renderCompact(node.children[index].second);
  }
  output += "}";
  return output;
}

} // namespace

namespace OCL::detail {

StructuredValueRenderResult renderStructuredValue(
    DataSourcePtr source, const StructuredValueRenderOptions &options) {
  try {
    const SnapshotResult local = snapshot(source);
    if (local.status == SnapshotStatus::evaluation_failed) {
      return {StructuredValueRenderStatus::evaluation_failed, {}};
    }
    if (local.status == SnapshotStatus::unavailable) {
      if (!source->evaluate()) {
        return {StructuredValueRenderStatus::evaluation_failed, {}};
      }
      return {StructuredValueRenderStatus::rendered,
              scalarText(source, options.hexadecimal)};
    }
    return {StructuredValueRenderStatus::rendered,
            renderCompact(captureNode(local.value, options))};
  } catch (const std::exception &) {
    return {StructuredValueRenderStatus::evaluation_failed, {}};
  } catch (...) {
    return {StructuredValueRenderStatus::evaluation_failed, {}};
  }
}

} // namespace OCL::detail
