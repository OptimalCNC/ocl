#define BOOST_TEST_MODULE taskbrowser_value_renderer
#include <boost/test/included/unit_test.hpp>

#include "taskbrowser/internal/StructuredValueRenderer.hpp"

#include <boost/intrusive_ptr.hpp>
#include <boost/serialization/nvp.hpp>
#include <rtt/internal/DataSources.hpp>
#include <rtt/typekit/RealTimeTypekit.hpp>
#include <rtt/types/SequenceTypeInfo.hpp>
#include <rtt/types/StructTypeInfo.hpp>
#include <rtt/types/TemplateTypeInfo.hpp>
#include <rtt/types/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <ostream>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace renderer_test {

struct Point { double x{0.0}; double y{0.0}; };
struct Envelope { Point point; std::int32_t quality{0}; };
struct Opaque { std::int32_t value{0}; };
struct Empty {};
struct TextValue { std::string text; };
struct Level4 { std::int32_t value{4}; };
struct Level3 { Level4 level4; };
struct Level2 { Level3 level3; };
struct Level1 { Level2 level2; };
struct WideValue {
  std::int32_t m00{0}, m01{1}, m02{2}, m03{3}, m04{4}, m05{5}, m06{6};
  std::int32_t m07{7}, m08{8}, m09{9}, m10{10}, m11{11}, m12{12}, m13{13};
  std::int32_t m14{14}, m15{15}, m16{16}, m17{17}, m18{18}, m19{19}, m20{20};
};

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
template <class Archive>
void serialize(Archive &archive, renderer_test::Level4 &value, const unsigned int) {
  archive & make_nvp("value", value.value);
}
template <class Archive>
void serialize(Archive &archive, renderer_test::Level3 &value, const unsigned int) {
  archive & make_nvp("level4", value.level4);
}
template <class Archive>
void serialize(Archive &archive, renderer_test::Level2 &value, const unsigned int) {
  archive & make_nvp("level3", value.level3);
}
template <class Archive>
void serialize(Archive &archive, renderer_test::Level1 &value, const unsigned int) {
  archive & make_nvp("level2", value.level2);
}
template <class Archive>
void serialize(Archive &archive, renderer_test::WideValue &value, const unsigned int) {
  archive & make_nvp("m00", value.m00);
  archive & make_nvp("m01", value.m01);
  archive & make_nvp("m02", value.m02);
  archive & make_nvp("m03", value.m03);
  archive & make_nvp("m04", value.m04);
  archive & make_nvp("m05", value.m05);
  archive & make_nvp("m06", value.m06);
  archive & make_nvp("m07", value.m07);
  archive & make_nvp("m08", value.m08);
  archive & make_nvp("m09", value.m09);
  archive & make_nvp("m10", value.m10);
  archive & make_nvp("m11", value.m11);
  archive & make_nvp("m12", value.m12);
  archive & make_nvp("m13", value.m13);
  archive & make_nvp("m14", value.m14);
  archive & make_nvp("m15", value.m15);
  archive & make_nvp("m16", value.m16);
  archive & make_nvp("m17", value.m17);
  archive & make_nvp("m18", value.m18);
  archive & make_nvp("m19", value.m19);
  archive & make_nvp("m20", value.m20);
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
    BOOST_REQUIRE(types->addType(new RTT::types::SequenceTypeInfo<std::vector<renderer_test::Point>>("/test/taskbrowser/PointArray")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Envelope, true>("/test/taskbrowser/Envelope")));
    BOOST_REQUIRE(types->addType(new RTT::types::TemplateTypeInfo<renderer_test::Opaque, true>("/test/taskbrowser/Opaque")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Empty, false>("/test/taskbrowser/Empty")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::TextValue, false>("/test/taskbrowser/TextValue")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Level4, false>("/test/taskbrowser/Level4")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Level3, false>("/test/taskbrowser/Level3")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Level2, false>("/test/taskbrowser/Level2")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::Level1, false>("/test/taskbrowser/Level1")));
    BOOST_REQUIRE(types->addType(new RTT::types::StructTypeInfo<renderer_test::WideValue, false>("/test/taskbrowser/WideValue")));
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

class MissingQualityDataSource final
    : public RTT::internal::ValueDataSource<renderer_test::Envelope> {
public:
  using RTT::internal::ValueDataSource<renderer_test::Envelope>::ValueDataSource;

  RTT::base::DataSourceBase::shared_ptr
  getMember(const std::string &name) override {
    if (name == "quality") {
      return {};
    }
    return RTT::internal::ValueDataSource<renderer_test::Envelope>::getMember(name);
  }
};

bool balancedDelimiters(const std::string &text) {
  std::vector<char> open;
  for (const char character : text) {
    if (character == '{' || character == '[') {
      open.push_back(character);
    } else if (character == '}' || character == ']') {
      if (open.empty()) return false;
      const char expected = character == '}' ? '{' : '[';
      if (open.back() != expected) return false;
      open.pop_back();
    }
  }
  return open.empty();
}

BOOST_AUTO_TEST_CASE(previews_zero_one_three_and_four_sequence_items) {
  loadRendererTypes();
  using PointArray = std::vector<renderer_test::Point>;

  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(PointArray{})).text == "[]");
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(PointArray{{1.0, 2.0}})).text ==
             "[[0]: {x: 1.0, y: 2.0}]");
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(
                 PointArray{{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}})).text ==
             "[[0]: {x: 1.0, y: 2.0}, [1]: {x: 3.0, y: 4.0}, "
             "[2]: {x: 5.0, y: 6.0}]");

  const auto four = OCL::detail::renderStructuredValue(valueSource(PointArray{
      {1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}, {7.0, 8.0}}));
  BOOST_TEST(four.text.find("[3]") == std::string::npos);
  BOOST_TEST(four.text.find("... 1 items omitted") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(previews_a_thousand_sequence_items_with_a_bounded_compact_form) {
  loadRendererTypes();
  using PointArray = std::vector<renderer_test::Point>;
  PointArray values(1000, renderer_test::Point{1.0, 2.0});
  BOOST_TEST(OCL::detail::renderStructuredValue(valueSource(values)).text ==
             "[[0]: {x: 1.0, y: 2.0}, [1]: {x: 1.0, y: 2.0}, "
             "[2]: {x: 1.0, y: 2.0}, ... 997 items omitted]");
}

BOOST_AUTO_TEST_CASE(collapses_structural_depth_four) {
  loadRendererTypes();
  const auto result = OCL::detail::renderStructuredValue(
      valueSource(renderer_test::Level1{}));
  BOOST_TEST(result.text == "{level2: {level3: {level4: {...}}}}");
}

BOOST_AUTO_TEST_CASE(limits_a_structure_to_twenty_members) {
  loadRendererTypes();
  const auto result = OCL::detail::renderStructuredValue(
      valueSource(renderer_test::WideValue{}));
  BOOST_TEST(result.text.find("m19: 19") != std::string::npos);
  BOOST_TEST(result.text.find("m20: 20") == std::string::npos);
  BOOST_TEST(result.text.find("... 1 members omitted") != std::string::npos);
  BOOST_TEST(result.text.find('\n') != std::string::npos);
}

BOOST_AUTO_TEST_CASE(switches_to_multiline_above_one_hundred_characters) {
  loadRendererTypes();
  const auto at_limit = OCL::detail::renderStructuredValue(
      valueSource(renderer_test::TextValue{std::string(89, 'a')}));
  const auto above_limit = OCL::detail::renderStructuredValue(
      valueSource(renderer_test::TextValue{std::string(90, 'a')}));

  BOOST_TEST(at_limit.text.find('\n') == std::string::npos);
  BOOST_TEST(at_limit.text.size() + 3U == 100U);
  BOOST_TEST(above_limit.text ==
             "{\n  text: " + std::string(90, 'a') + "\n}");
}

BOOST_AUTO_TEST_CASE(continues_after_an_unavailable_member) {
  loadRendererTypes();
  auto snapshot = new MissingQualityDataSource(
      renderer_test::Envelope{{3.0, 4.0}, 5});
  BOOST_TEST(OCL::detail::renderStructuredSnapshotForTest(snapshot) ==
             "{point: {x: 3.0, y: 4.0}, quality: <unavailable>}");
}

BOOST_AUTO_TEST_CASE(enforces_a_delimiter_safe_byte_budget) {
  loadRendererTypes();
  const auto result = OCL::detail::renderStructuredValue(
      valueSource(renderer_test::TextValue{std::string(6000, 'x')}));
  BOOST_TEST(result.text.size() + 3U <= 4096U);
  BOOST_TEST(result.text.find("bytes omitted") != std::string::npos);
  BOOST_TEST(result.text.front() == '{');
  BOOST_TEST(result.text.back() == '}');
  BOOST_TEST(balancedDelimiters(result.text));

  const std::regex omitted_pattern(R"(\.\.\. ([0-9]+) bytes omitted)");
  std::smatch omitted_match;
  BOOST_REQUIRE(std::regex_search(result.text, omitted_match, omitted_pattern));
  const std::size_t value_begin = result.text.find("text: ") + 6U;
  const std::size_t marker_begin =
      static_cast<std::size_t>(omitted_match.position(0));
  const std::size_t retained = marker_begin - value_begin;
  const std::size_t omitted =
      static_cast<std::size_t>(std::stoull(omitted_match.str(1)));
  BOOST_TEST(retained + omitted == 6000U);
}

BOOST_AUTO_TEST_CASE(truncates_structural_output_without_breaking_delimiters) {
  loadRendererTypes();
  OCL::detail::StructuredValueRenderOptions small_budget;
  small_budget.max_result_bytes = 80;
  const auto bounded = OCL::detail::renderStructuredValue(
      valueSource(renderer_test::WideValue{}), small_budget);
  BOOST_TEST(bounded.text.size() + 3U <= 80U);
  BOOST_TEST(bounded.text.find("... output omitted") != std::string::npos);
  BOOST_TEST(balancedDelimiters(bounded.text));
}

BOOST_AUTO_TEST_CASE(snapshots_a_structured_source_exactly_once) {
  loadRendererTypes();
  boost::intrusive_ptr<CountingDataSource<renderer_test::Envelope>> source(
      new CountingDataSource<renderer_test::Envelope>(
          renderer_test::Envelope{{3.0, 4.0}, 5}));
  const auto result = OCL::detail::renderStructuredValue(source);
  BOOST_TEST(result.status == OCL::detail::StructuredValueRenderStatus::rendered);
  BOOST_TEST(source->evaluationCount() == 1U);
  BOOST_TEST(result.text == "{point: {x: 3.0, y: 4.0}, quality: 5}");
}

BOOST_AUTO_TEST_CASE(never_traverses_or_mutates_the_source) {
  loadRendererTypes();
  boost::intrusive_ptr<MutationProbeDataSource<renderer_test::Envelope>> source(
      new MutationProbeDataSource<renderer_test::Envelope>(
          renderer_test::Envelope{{3.0, 4.0}, 5}));
  const auto result = OCL::detail::renderStructuredValue(source);
  BOOST_TEST(result.text == "{point: {x: 3.0, y: 4.0}, quality: 5}");
  BOOST_TEST(source->evaluationCount() == 1U);
  BOOST_TEST(source->setValueCount() == 0U);
  BOOST_TEST(source->mutableReferenceCount() == 0U);
  BOOST_TEST(source->updatedCount() == 0U);
}
