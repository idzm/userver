#include <userver/yaml_config/impl/validate_static_config.hpp>

#include <userver/formats/yaml/serialize.hpp>
#include <userver/server/component.hpp>
#include <userver/yaml_config/schema.hpp>
#include <userver/yaml_config/yaml_config.hpp>

#include <gtest/gtest.h>

USERVER_NAMESPACE_BEGIN

namespace {

void Validate(const std::string& static_config, const std::string& schema) {
  yaml_config::impl::Validate(
      yaml_config::YamlConfig(formats::yaml::FromString(static_config), {}),
      yaml_config::Schema(schema));
}

void CheckConfigFail(const std::string& static_config,
                     const std::string& schema,
                     const std::string& expected_message) {
  try {
    Validate(static_config, schema);
    FAIL() << "Should have thrown";
  } catch (const std::runtime_error& exception) {
    EXPECT_EQ(exception.what(), expected_message);
  } catch (const std::exception& exception) {
    FAIL() << "Expect runtime error. Message: " << exception.what();
  }
}

void CheckSchemaFail(const std::string& schema,
                     const std::string& expected_message) {
  try {
    formats::yaml::FromString(schema).As<yaml_config::Schema>();
    FAIL() << "Should have thrown";
  } catch (const std::runtime_error& exception) {
    EXPECT_EQ(exception.what(), expected_message);
  } catch (const std::exception& exception) {
    FAIL() << "Expect runtime error. Message: " << exception.what();
  }
}

}  // namespace

TEST(StaticConfigValidator, IncorrectSchemaField) {
  const std::string kSchema = R"(
type: integer
description: with incorrect field name
incorrect_filed_name:
)";

  CheckSchemaFail(
      kSchema,
      "Schema field name must be one of ['type', 'description', "
      "'defaultDescription', 'additionalProperties', 'properties', 'items'], "
      "but 'incorrect_filed_name' was given. Schema path: '/'");
}

TEST(StaticConfigValidator, AdditionalPropertiesAbsent) {
  const std::string kSchema = R"(
type: object
description: object without additionalProperties
properties: {}
)";

  CheckSchemaFail(kSchema,
                  "Schema field '/' of type 'object' must have field "
                  "'additionalProperties'");
}

TEST(StaticConfigValidator, AdditionalPropertiesTrue) {
  const std::string kSchema = R"(
type: object
description: object with additionalProperties set to 'true'
additionalProperties: true
properties: {}
)";

  CheckSchemaFail(kSchema,
                  "Schema field '/' has 'additionalProperties' set to 'true' "
                  "which is unsupported");
}

TEST(StaticConfigValidator, PropertiesAbsent) {
  const std::string kSchema = R"(
type: object
description: object without properties
additionalProperties: false
)";

  CheckSchemaFail(
      kSchema,
      "Schema field '/' of type 'object' must have field 'properties'");
}

TEST(StaticConfigValidator, ItemsAbsent) {
  const std::string kSchema = R"(
type: array
description: array without items
)";

  CheckSchemaFail(kSchema,
                  "Schema field '/' of type 'array' must have field 'items'");
}

TEST(StaticConfigValidator, ItemsOutOfArray) {
  const std::string kSchema = R"(
type: string
description: string with items
items:
    type: integer
    description: element description
)";

  CheckSchemaFail(kSchema,
                  "Schema field '/' of type 'string' can not have field "
                  "'items', because its type is not 'array'");
}

TEST(StaticConfigValidator, PropertiesOutOfObject) {
  const std::string kSchema = R"(
type: integer
description: integer with properties
properties: {}
)";

  CheckSchemaFail(kSchema,
                  "Schema field '/' of type 'integer' can not have field "
                  "'properties', because its type is not 'object'");
}

TEST(StaticConfigValidator, AdditionalPropertiesOutOfObject) {
  const std::string kSchema = R"(
type: integer
description: integer with additionalProperties
additionalProperties: false
)";

  CheckSchemaFail(kSchema,
                  "Schema field '/' of type 'integer' can not have field "
                  "'additionalProperties, because its type is not 'object'");
}

TEST(StaticConfigValidator, Integer) {
  const std::string kStaticConfig = R"(
42
)";
  const std::string kSchema = R"(
type: integer
description: answer to the ultimate question
)";
  EXPECT_NO_THROW(Validate(kStaticConfig, kSchema));
}

TEST(StaticConfigValidator, RecursiveFailed) {
  const std::string kStaticConfig = R"(
listener:
    port: 0
    connection:
        in_buffer_size: abc # must be integer
)";

  const std::string kSchema = R"(
type: object
description: server description
additionalProperties: false
properties:
    listener:
        type: object
        description: listener description
        additionalProperties: false
        properties:
            port:
                type: integer
                description: port description
            connection:
                type: object
                description: connection description
                additionalProperties: false
                properties:
                    in_buffer_size:
                        type: integer
                        description: in_buffer_size description
)";

  CheckConfigFail(
      kStaticConfig, kSchema,
      "Error while validating static config against schema. Value 'abc' "
      "of field 'listener.connection.in_buffer_size' must be "
      "integer");
}

TEST(StaticConfigValidator, SimpleArrayFailed) {
  const std::string kStaticConfig = R"(
arr: [2, 4, 6, abc]
)";
  const std::string kSchema = R"(
type: object
description: simple array
additionalProperties: false
properties:
    arr:
        type: array
        description: integer array
        items:
            type: integer
            description: element of array
)";
  CheckConfigFail(
      kStaticConfig, kSchema,
      "Error while validating static config against schema. Value 'abc' "
      "of field 'arr[3]' must be integer");
}

TEST(StaticConfigValidator, ArrayFailed) {
  const std::string kStaticConfig = R"(
arr:
  - key: a
    value: 1
  - key: a
    value: 1
    not_declared_option:
)";
  const std::string kSchema = R"(
type: object
description: array description
additionalProperties: false
properties:
    arr:
        type: array
        description: key-value array
        items:
            type: object
            description: element description
            additionalProperties: false
            properties:
                key:
                    type: string
                    description: key description
                value:
                    type: integer
                    description: value description
)";
  CheckConfigFail(kStaticConfig, kSchema,
                  "Error while validating static config against schema. Field "
                  "'arr[1].not_declared_option' is not declared in schema "
                  "'properties.arr.items'");
}

TEST(StaticConfigValidator, Recursive) {
  const std::string kStaticConfig = R"(
huge-object:
    big-object:
        key: a
        value: 1
        arrays:
            simple-array: [2, 4, 6]
            key-value-array:
              - key: a
                value: 1
              - key: b
                value: 2
)";
  const std::string kSchema = R"(
type: object
description: recursive description
additionalProperties: false
properties:
    huge-object:
        type: object
        description: huge-object description
        additionalProperties: false
        properties:
            big-object:
                type: object
                description: big-object description
                additionalProperties: false
                properties:
                    key:
                        type: string
                        description: key description
                    value:
                        type: integer
                        description: value description
                    arrays:
                        type: object
                        description: arrays description
                        additionalProperties: false
                        properties:
                            simple-array:
                                type: array
                                description: integer array
                                items:
                                    type: integer
                                    description: element description
                            key-value-array:
                                type: array
                                description: key-value array
                                items:
                                    type: object
                                    description: element description
                                    additionalProperties: false
                                    properties:
                                        key:
                                            type: string
                                            description: key description
                                        value:
                                            type: integer
                                            description: value description
)";
  EXPECT_NO_THROW(Validate(kStaticConfig, kSchema));
}

USERVER_NAMESPACE_END