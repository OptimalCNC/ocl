#define BOOST_TEST_MODULE taskbrowser_value_renderer
#include <boost/test/included/unit_test.hpp>

#include "taskbrowser/internal/StructuredValueRenderer.hpp"

#include <boost/serialization/nvp.hpp>
#include <rtt/internal/DataSources.hpp>
#include <rtt/typekit/RealTimeTypekit.hpp>
#include <rtt/types/StructTypeInfo.hpp>
#include <rtt/types/TemplateTypeInfo.hpp>
#include <rtt/types/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <ostream>
#include <string>
#include <utility>

namespace renderer_test {

struct Point { double x{0.0}; double y{0.0}; };
struct Envelope { Point point; std::int32_t quality{0}; };
struct Opaque { std::int32_t value{0}; };
struct Empty {};
struct TextValue { std::string text; };

std::ostream &operator<<(std::ostream &stream, const Point &value) {
  return stream << "Point{" << value.x << ", " << value.y << '}';
}
std::ostream &operator<<(std::ostream &stream, const Envelope &value) {
  return stream << "Envelope{" << value.point << ", " << value.quality << '}';
}
std::ostream &operator<<(std::ostream &stream, const Opaque &value) {
  return stream << "Opaque{" << value.value << '}';
}

std::istream &operator>>(std::istream &stream, Point &) { return stream; }
std::istream &operator>>(std::istream &stream, Envelope &) { return stream; }
std::istream &operator>>(std::istream &stream, Opaque &) { return stream; }

} // namespace renderer_test

namespace OCL::detail {

std::ostream &operator<<(std::ostream &stream, StructuredValueRenderStatus status) {
  return stream << static_cast<int>(status);
}

} // namespace OCL::detail

namespace boost::serialization {

template <class Archive>
void serialize(Archive &archive, renderer_test::Point &value, const unsigned int) {
  archive & make_nvp("x", value.x);
  archive & make_nvp("y", value.y);
}
template <class Archive>
void serialize(Archive &archive, renderer_test::Envelope &value, const unsigned int) {
  archive & make_nvp("point", value.point);
  archive & make_nvp("quality", value.quality);
}
template <class Archive>
void serialize(Archive &, renderer_test::Empty &, const unsigned int) {}
template <class Archive>
void serialize(Archive &archive, renderer_test::TextValue &value, const unsigned int) {
  archive & make_nvp("text", value.text);
}

} // namespace boost::serialization

namespace {

void loadRendererTypes() {
  auto types = RTT::types::Types();
  if (types->type("Float64") == nullptr) {
    RTT::types::RealTimeTypekitPlugin().loadTypes();
  }
  if (types->type("/test/taskbrowser/Point") == nullptr) {
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Point, true>("/test/taskbrowser/Point")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Envelope, true>("/test/taskbrowser/Envelope")));
    BOOST_REQUIRE(types->addType(new RTT::types::TemplateTypeInfo<renderer_test::Opaque, true>("/test/taskbrowser/Opaque")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Empty, false>("/test/taskbrowser/Empty")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::TextValue, false>("/test/taskbrowser/TextValue")));
  }
}

template <typename T>
RTT::base::DataSourceBase::shared_ptr valueSource(T value) {
  return new RTT::internal::ValueDataSource<T>(std::move(value));
}

template <typename T>
class CountingDataSource final : public RTT::internal::DataSource<T> {
public:
  explicit CountingDataSource(T value, bool succeeds = true)
      : value_(std::move(value)), succeeds_(succeeds) {}
  bool evaluate() const override { ++evaluation_count_; return succeeds_; }
  typename RTT::internal::DataSource<T>::result_t get() const override { return value_; }
  typename RTT::internal::DataSource<T>::result_t value() const override { return value_; }
  typename RTT::internal::DataSource<T>::const_reference_t rvalue() const override { return value_; }
  CountingDataSource *clone() const override { return new CountingDataSource(value_, succeeds_); }
  CountingDataSource *copy(std::map<const RTT::base::DataSourceBase *, RTT::base::DataSourceBase *> &copies) const override {
    const auto existing = copies.find(this);
    if (existing != copies.end()) return static_cast<CountingDataSource *>(existing->second);
    auto *copy = new CountingDataSource(value_, succeeds_);
    copies[this] = copy;
    return copy;
  }
  std::size_t evaluationCount() const { return evaluation_count_; }
private:
  T value_;
  bool succeeds_;
  mutable std::size_t evaluation_count_{0};
};

template <typename T>
class MutationProbeDataSource final : public RTT::internal::AssignableDataSource<T> {
public:
  explicit MutationProbeDataSource(T value) : value_(std::move(value)) {}
  bool evaluate() const override { ++evaluation_count_; return true; }
  typename RTT::internal::DataSource<T>::result_t get() const override { return value_; }
  typename RTT::internal::DataSource<T>::result_t value() const override { return value_; }
  typename RTT::internal::DataSource<T>::const_reference_t rvalue() const override { return value_; }
  void set(typename RTT::internal::AssignableDataSource<T>::param_t value) override { ++set_value_count_; value_ = value; }
  typename RTT::internal::AssignableDataSource<T>::reference_t set() override { ++mutable_reference_count_; return value_; }
  void updated() override { ++updated_count_; }
  MutationProbeDataSource *clone() const override { return new MutationProbeDataSource(value_); }
  MutationProbeDataSource *copy(std::map<const RTT::base::DataSourceBase *, RTT::base::DataSourceBase *> &copies) const override {
    const auto existing = copies.find(this);
    if (existing != copies.end()) return static_cast<MutationProbeDataSource *>(existing->second);
    auto *copy = new MutationProbeDataSource(value_);
    copies[this] = copy;
    return copy;
  }
  std::size_t evaluationCount() const { return evaluation_count_; }
  std::size_t setValueCount() const { return set_value_count_; }
  std::size_t mutableReferenceCount() const { return mutable_reference_count_; }
  std::size_t updatedCount() const { return updated_count_; }
private:
  T value_;
  mutable std::size_t evaluation_count_{0};
  std::size_t set_value_count_{0};
  std::size_t mutable_reference_count_{0};
  std::size_t updated_count_{0};
};

} // namespace

BOOST_AUTO_TEST_CASE(renders_named_structures_instead_of_stream_operators) {
  loadRendererTypes();
  const auto result = OCL::detail::renderStructuredValue(valueSource(renderer_test::Envelope{{3.0, 4.0}, 5}));
  BOOST_TEST(result.status == OCL::detail::StructuredValueRenderStatus::rendered);
  BOOST_TEST(result.text == "{point: {x: 3.0, y: 4.0}, quality: 5}");
}

BOOST_AUTO_TEST_CASE(preserves_a_decimal_marker_for_floating_values) {
  loadRendererTypes();
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(3.0F)).text == "3.0");
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(-4.0)).text == "-4.0");
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(1.25)).text == "1.25");
}

BOOST_AUTO_TEST_CASE(keeps_opaque_stream_representation) {
  loadRendererTypes();
  const auto result = OCL::detail::renderStructuredValue(valueSource(renderer_test::Opaque{7}));
  BOOST_TEST(result.text == "Opaque{7}");
}

BOOST_AUTO_TEST_CASE(preserves_scalar_and_hexadecimal_representation) {
  loadRendererTypes();
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(true)).text == "true");
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(std::string("alpha"))).text == "alpha");
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource('x')).text == "x");
  OCL::detail::StructuredValueRenderOptions options;
  options.hexadecimal = true;
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(std::int32_t{26}), options).text == "1a");
}

BOOST_AUTO_TEST_CASE(renders_an_empty_member_aware_structure) {
  loadRendererTypes();
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(renderer_test::Empty{})).text == "{}");
}

BOOST_AUTO_TEST_CASE(snapshots_a_structured_source_exactly_once) {
  loadRendererTypes();
  auto source = new CountingDataSource<renderer_test::Envelope>(renderer_test::Envelope{{3.0, 4.0}, 5});
  const auto result = OCL::detail::renderStructuredValue(source);
  BOOST_TEST(result.status == OCL::detail::StructuredValueRenderStatus::rendered);
  BOOST_TEST(source->evaluationCount() == 1U);
  BOOST_TEST(result.text == "{point: {x: 3.0, y: 4.0}, quality: 5}");
}

BOOST_AUTO_TEST_CASE(never_traverses_or_mutates_the_source) {
  loadRendererTypes();
  auto source = new MutationProbeDataSource<renderer_test::Envelope>(renderer_test::Envelope{{3.0, 4.0}, 5});
  const auto result = OCL::detail::renderStructuredValue(source);
  BOOST_TEST(result.text == "{point: {x: 3.0, y: 4.0}, quality: 5}");
  BOOST_TEST(source->evaluationCount() == 1U);
  BOOST_TEST(source->setValueCount() == 0U);
  BOOST_TEST(source->mutableReferenceCount() == 0U);
  BOOST_TEST(source->updatedCount() == 0U);
}
