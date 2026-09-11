#include "statewright/egcf/internet_polynomial.hpp"
#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <chrono>
#include <regex>
#include <string>

namespace statewright::egcf {
namespace {
using contracts::Json;

void require(bool condition, std::string_view reason) {
  if (!condition)
    throw common::Error(common::ErrorCode::invalid_argument, std::string(reason));
}

std::size_t bits(const mpz_class &value) {
  return mpz_sizeinbase(value.get_mpz_t(), 2);
}

void bounded(const mpq_class &value, std::size_t limit) {
  require(value.get_den() > 0, "POLYNOMIAL_INVALID_DENOMINATOR");
  require(bits(value.get_num()) <= limit && bits(value.get_den()) <= limit,
          "POLYNOMIAL_INTEGER_RESOURCE_LIMIT");
}

void validate(const InternetExactPolynomialProgram &program) {
  require(!program.coefficients.empty() && program.coefficients.size() <= 17,
          "POLYNOMIAL_DEGREE_OUT_OF_RANGE");
  require(program.maximum_integer_bits >= 1 && program.maximum_integer_bits <= 16384,
          "POLYNOMIAL_INVALID_RESOURCE_BUDGET");
  for (const auto &value : program.coefficients) {
    bounded(value, 256);
    bounded(value, program.maximum_integer_bits);
  }
}

mpq_class rational(const Json &value) {
  require(value.is_string(), "POLYNOMIAL_RATIONAL_STRING_REQUIRED");
  const auto text = value.get<std::string>();
  require(text.size() <= 160, "POLYNOMIAL_CONSTANT_BYTE_LIMIT");
  static const std::regex syntax("-?[0-9]+(/[1-9][0-9]*)?");
  require(std::regex_match(text, syntax), "POLYNOMIAL_INVALID_RATIONAL");
  mpq_class result(text, 10);
  bounded(result, 256);
  result.canonicalize();
  require(result.get_str() == text, "POLYNOMIAL_NONCANONICAL_RATIONAL");
  return result;
}

Json coefficient_values(const InternetExactPolynomialProgram &program) {
  Json values = Json::array();
  for (auto value : program.coefficients) {
    value.canonicalize();
    values.push_back(value.get_str());
  }
  return values;
}

mpq_class bounded_product(const mpq_class &a, const mpq_class &b, std::size_t limit) {
  require(bits(a.get_num()) + bits(b.get_num()) <= limit &&
              bits(a.get_den()) + bits(b.get_den()) <= limit,
          "POLYNOMIAL_INTEGER_RESOURCE_LIMIT");
  mpq_class result = a * b;
  result.canonicalize();
  bounded(result, limit);
  return result;
}

mpq_class bounded_sum(const mpq_class &a, const mpq_class &b, std::size_t limit) {
  require(std::max(bits(a.get_num()) + bits(b.get_den()),
                   bits(b.get_num()) + bits(a.get_den())) + 1 <= limit &&
              bits(a.get_den()) + bits(b.get_den()) <= limit,
          "POLYNOMIAL_INTEGER_RESOURCE_LIMIT");
  mpq_class result = a + b;
  result.canonicalize();
  bounded(result, limit);
  return result;
}
} // namespace

Json internet_polynomial_contract(const InternetExactPolynomialProgram &program) {
  validate(program);
  const auto coefficients = coefficient_values(program);
  return {{"version", program.direct_power_sum ? internet_polynomial_reference_version : internet_polynomial_version},
          {"numeric_type", "exact_rational"},
          {"coefficients", coefficients},
          {"coefficient_sha256", contracts::sha256_json(coefficients)},
          {"input_minimum", "-1"}, {"input_maximum", "1"},
          {"maximum_input_bits", 256},
          {"maximum_integer_bits", program.maximum_integer_bits},
          {"evaluation_order", program.direct_power_sum ? "ascending_direct_power_sum" : "descending_horner"}};
}

namespace {
Json polynomial_ir(const InternetExactPolynomialProgram &program) {
  const auto &coefficients = program.coefficients;
  const auto contract = internet_polynomial_contract(program);
  const auto values = coefficient_values(program);
  const auto degree = coefficients.size() - 1;
  Json nodes = Json::array();
  if (program.direct_power_sum) {
    nodes.push_back({{"id", "seed"}, {"primitive", "CONST"},
                     {"operands", {{{"constant", "0"}}}}});
    std::string total = "seed";
    for (std::size_t i = 0; i <= degree; ++i) {
      const auto prefix = "term_" + std::to_string(i);
      std::string power = prefix + "_one";
      nodes.push_back({{"id", power}, {"primitive", "CONST"},
                       {"operands", {{{"constant", "1"}}}}});
      for (std::size_t j = 0; j < i; ++j) {
        const auto next = prefix + "_power_" + std::to_string(j + 1);
        nodes.push_back({{"id", next}, {"primitive", "MULTIPLY"},
                         {"operands", {{{"node", power}}, {{"input", 0}}}}});
        power = next;
      }
      nodes.push_back({{"id", prefix}, {"primitive", "MULTIPLY"},
                       {"operands", {{{"node", power}}, {{"constant", values.at(i)}}}}});
      const auto sum = prefix + "_sum";
      nodes.push_back({{"id", sum}, {"primitive", "ADD"},
                       {"operands", {{{"node", total}}, {{"node", prefix}}}}});
      total = sum;
    }
    return {{"name", "internet-exact-polynomial-reference"},
            {"entry_nodes", {"seed"}},
            {"inputs", {{{"name", "x"}, {"position", 0}}}},
            {"outputs", {{{"name", "y"}, {"position", 0}, {"source", {{"node", total}}}}}},
            {"nodes", nodes}, {"metadata", {{"internet_exact_polynomial", contract}}}};
  }
  nodes.push_back({{"id", "seed"}, {"primitive", "CONST"},
                   {"operands", {{{"constant", values.at(degree)}}}}});
  std::string previous = "seed";
  for (std::size_t i = degree; i > 0; --i) {
    const auto mul = "mul_" + std::to_string(i - 1);
    const auto add = "add_" + std::to_string(i - 1);
    nodes.push_back({{"id", mul}, {"primitive", "MULTIPLY"},
                     {"operands", {{{"node", previous}}, {{"input", 0}}}}});
    nodes.push_back({{"id", add}, {"primitive", "ADD"},
                     {"operands", {{{"node", mul}}, {{"constant", values.at(i - 1)}}}}});
    previous = add;
  }
  return {{"name", "internet-exact-polynomial"},
          {"entry_nodes", {"seed"}},
          {"inputs", {{{"name", "x"}, {"position", 0}}}},
          {"outputs", {{{"name", "y"}, {"position", 0},
                         {"source", {{"node", previous}}}}}},
          {"nodes", nodes},
          {"metadata", {{"internet_exact_polynomial", contract}}}};
}
} // namespace

Json internet_exact_polynomial_ir(const std::vector<mpq_class> &coefficients,
                                  std::size_t maximum_integer_bits) {
  return polynomial_ir({coefficients, maximum_integer_bits, false});
}

Json internet_exact_polynomial_reference_ir(const std::vector<mpq_class> &coefficients,
                                            std::size_t maximum_integer_bits) {
  return polynomial_ir({coefficients, maximum_integer_bits, true});
}

InternetExactPolynomialProgram internet_exact_polynomial_program(const Json &mapping) {
  require(mapping.is_object() && mapping.dump().size() <= 65536,
          "POLYNOMIAL_IR_BYTE_LIMIT");
  require(mapping.contains("metadata") && mapping.at("metadata").is_object() &&
              mapping.at("metadata").contains("internet_exact_polynomial"),
          "POLYNOMIAL_VERSIONED_CONTRACT_REQUIRED");
  const auto &contract = mapping.at("metadata").at("internet_exact_polynomial");
  require(contract.is_object(), "POLYNOMIAL_CONTRACT_REQUIRED");
  const auto version = contract.value("version", std::string{});
  require(version == internet_polynomial_version || version == internet_polynomial_reference_version,
          "POLYNOMIAL_VERSION_UNSUPPORTED");
  const auto &values = contract.at("coefficients");
  require(values.is_array() && !values.empty() && values.size() <= 17,
          "POLYNOMIAL_DEGREE_OUT_OF_RANGE");
  require(contract.at("maximum_integer_bits").is_number_integer(),
          "POLYNOMIAL_INVALID_RESOURCE_BUDGET");
  const auto budget = contract.at("maximum_integer_bits").get<long long>();
  require(budget >= 1 && budget <= 16384, "POLYNOMIAL_INVALID_RESOURCE_BUDGET");
  InternetExactPolynomialProgram program;
  program.direct_power_sum = version == internet_polynomial_reference_version;
  program.maximum_integer_bits = static_cast<std::size_t>(budget);
  for (const auto &value : values) program.coefficients.push_back(rational(value));
  // Exact closed-shape comparison rejects cycles, dangling/forward references,
  // disconnected nodes, attributes, alternate evaluation orders and extra ports.
  require(mapping == polynomial_ir(program),
          "POLYNOMIAL_GRAPH_OR_CONTRACT_MISMATCH");
  return program;
}

mpq_class internet_execute_exact_polynomial(const InternetExactPolynomialProgram &program,
                                           const mpq_class &input) {
  validate(program);
  bounded(input, 256);
  bounded(input, program.maximum_integer_bits);
  mpq_class x = input;
  x.canonicalize();
  require(x >= -1 && x <= 1, "POLYNOMIAL_INPUT_OUTSIDE_DOMAIN");
  auto coefficients = program.coefficients;
  for (auto &value : coefficients) value.canonicalize();
  mpq_class result = coefficients.back();
  const auto limit = program.maximum_integer_bits;
  if (program.direct_power_sum) {
    mpq_class total = 0;
    for (std::size_t i = 0; i < coefficients.size(); ++i) {
      mpq_class power = 1;
      for (std::size_t j = 0; j < i; ++j)
        power = bounded_product(power, x, limit);
      const auto term = bounded_product(coefficients[i], power, limit);
      total = bounded_sum(total, term, limit);
    }
    return total;
  }
  for (std::size_t i = coefficients.size() - 1; i > 0; --i) {
    // Conservative pre-allocation bounds; cancellation cannot authorize a
    // calculation whose unreduced intermediate would exceed the budget.
    require(bits(result.get_num()) + bits(x.get_num()) <= limit &&
                bits(result.get_den()) + bits(x.get_den()) <= limit,
            "POLYNOMIAL_INTEGER_RESOURCE_LIMIT");
    result *= x;
    const auto &c = coefficients[i - 1];
    require(std::max(bits(result.get_num()) + bits(c.get_den()),
                     bits(c.get_num()) + bits(result.get_den())) + 1 <= limit &&
                bits(result.get_den()) + bits(c.get_den()) <= limit,
            "POLYNOMIAL_INTEGER_RESOURCE_LIMIT");
    result += c;
    result.canonicalize();
    bounded(result, limit);
  }
  return result;
}

Json internet_measure_polynomial_pair(const Json &candidate_ir, const Json &baseline_ir,
                                      const std::vector<mpq_class> &frozen_inputs,
                                      std::size_t repetitions) {
  const auto candidate = internet_exact_polynomial_program(candidate_ir);
  const auto baseline = internet_exact_polynomial_program(baseline_ir);
  require(!candidate.direct_power_sum && baseline.direct_power_sum,
          "POLYNOMIAL_MEASUREMENT_REQUIRES_HORNER_AND_DIRECT_BASELINE");
  require(candidate.coefficients == baseline.coefficients &&
              candidate.maximum_integer_bits == baseline.maximum_integer_bits,
          "POLYNOMIAL_BASELINE_CONTRACT_MISMATCH");
  require(!frozen_inputs.empty() && frozen_inputs.size() <= 256 &&
              repetitions >= 2 && repetitions <= 100,
          "POLYNOMIAL_MEASUREMENT_WORK_BUDGET");
  require(frozen_inputs.size() * repetitions <= 4096,
          "POLYNOMIAL_MEASUREMENT_WORK_BUDGET");
  Json inputs = Json::array(), expected = Json::array();
  for (const auto &x : frozen_inputs) {
    // Preflight both variants before starting the paired timing experiment.
    const auto value = internet_execute_exact_polynomial(baseline, x);
    require(internet_execute_exact_polynomial(candidate, x) == value,
            "POLYNOMIAL_BASELINE_OUTPUT_MISMATCH");
    auto normalized = x;
    normalized.canonicalize();
    inputs.push_back(normalized.get_str());
    expected.push_back(value.get_str());
  }
  Json samples = Json::array();
  const auto measure = [&](const InternetExactPolynomialProgram &program) {
    std::vector<mpq_class> outputs;
    outputs.reserve(frozen_inputs.size());
    const auto start = std::chrono::steady_clock::now();
    for (const auto &x : frozen_inputs)
      outputs.push_back(internet_execute_exact_polynomial(program, x));
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    Json results = Json::array();
    for (const auto &value : outputs) results.push_back(value.get_str());
    require(results == expected, "POLYNOMIAL_MEASUREMENT_OUTPUT_CHANGED");
    return Json{{"elapsed_nanoseconds", elapsed},
                {"outputs_sha256", contracts::sha256_json(results)}};
  };
  for (std::size_t i = 0; i < repetitions; ++i) {
    Json c, b;
    if (i % 2 == 0) { c = measure(candidate); b = measure(baseline); }
    else { b = measure(baseline); c = measure(candidate); }
    samples.push_back({{"pair_index", i}, {"candidate_first", i % 2 == 0},
                       {"candidate", c}, {"baseline", b}});
  }
  return {{"schema_version", 1}, {"kind", "POLYNOMIAL_PAIRED_RAW_MEASUREMENTS_V1"},
          {"candidate_ir_sha256", contracts::sha256_json(candidate_ir)},
          {"baseline_ir_sha256", contracts::sha256_json(baseline_ir)},
          {"inputs", inputs}, {"inputs_sha256", contracts::sha256_json(inputs)},
          {"reference_outputs", expected}, {"samples", samples},
          {"environment", {{"gmp_version", gmp_version}, {"compiler", __VERSION__},
                            {"clock", "std::chrono::steady_clock"}}},
          {"shared_dependencies", {"GMP exact-rational arithmetic", "same process, compiler and host", "contract validation and resource-check code"}},
          {"limitations", {"Direct power-sum reference is not independently reviewed source truth", "Timings include evaluator validation and output storage", "No benchmark score mapping or longitudinal observations", "Freeze native protocol and record deployment environment before qualification use"}},
          {"simulated", false}, {"qualification_claim", "NONE"}};
}
} // namespace statewright::egcf
