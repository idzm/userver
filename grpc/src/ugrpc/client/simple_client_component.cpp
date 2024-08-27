#include <userver/ugrpc/client/simple_client_component.hpp>

#include <userver/yaml_config/merge_schemas.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::client::impl {

yaml_config::Schema SimpleClientComponentAny::GetStaticConfigSchema() {
  return yaml_config::MergeSchemas<components::ComponentBase>(R"(
type: object
description: Provides a ClientFactory in the component system
additionalProperties: false
properties:
    endpoint:
        type: string
        description: URL of the gRPC service
    client-name:
        type: string
        description: name of the gRPC server we talk to, for diagnostics
    factory-component:
        type: string
        description: ClientFactoryComponent name to use for client creation
)");
}

}  // namespace ugrpc::client::impl

USERVER_NAMESPACE_END
