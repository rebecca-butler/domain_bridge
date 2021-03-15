// Copyright 2021, Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "rclcpp/context.hpp"
#include "rclcpp/node.hpp"
// #include "rclcpp/executors/single_threaded_executor.hpp"
#include "test_msgs/msg/basic_types.hpp"

#include "domain_bridge/domain_bridge.hpp"

/// Wait for a publisher to be available (or not)
/*
 * \return true if the wait was successful, false if a timeout occured.
 */
bool wait_for_publisher(
  std::shared_ptr<rclcpp::Node> node,
  const std::string & topic_name,
  bool to_be_available = true,
  std::chrono::nanoseconds timeout = std::chrono::seconds(3),
  std::chrono::nanoseconds sleep_period = std::chrono::milliseconds(100))
{
  auto start = std::chrono::steady_clock::now();
  std::chrono::microseconds time_slept(0);
  auto predicate = [&node, &topic_name, &to_be_available]() -> bool {
      if (to_be_available) {
        // A publisher is available if the count is greater than 0
        return node->count_publishers(topic_name) > 0;
      } else {
        return node->count_publishers(topic_name) == 0;
      }
    };

  while (!predicate() &&
    time_slept < std::chrono::duration_cast<std::chrono::microseconds>(timeout))
  {
    rclcpp::Event::SharedPtr graph_event = node->get_graph_event();
    node->wait_for_graph_change(graph_event, sleep_period);
    time_slept = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  }
  return predicate();
}

class TestDomainBridgeQosMatching : public ::testing::Test
{
protected:
  static void SetUpTestCase()
  {
    // Initialize contexts in different domains
    context_1_ = std::make_shared<rclcpp::Context>();
    rclcpp::InitOptions context_options_1;
    context_options_1.auto_initialize_logging(false).set_domain_id(domain_1_);
    context_1_->init(0, nullptr, context_options_1);

    context_2_ = std::make_shared<rclcpp::Context>();
    rclcpp::InitOptions context_options_2;
    context_options_2.auto_initialize_logging(false).set_domain_id(domain_2_);
    context_2_->init(0, nullptr, context_options_2);

    node_options_1_.context(context_1_);
    node_options_2_.context(context_2_);
  }

  static const std::size_t domain_1_{1u};
  static const std::size_t domain_2_{2u};
  static std::shared_ptr<rclcpp::Context> context_1_;
  static std::shared_ptr<rclcpp::Context> context_2_;
  static rclcpp::NodeOptions node_options_1_;
  static rclcpp::NodeOptions node_options_2_;
};

const std::size_t TestDomainBridgeQosMatching::domain_1_;
const std::size_t TestDomainBridgeQosMatching::domain_2_;
std::shared_ptr<rclcpp::Context> TestDomainBridgeQosMatching::context_1_;
std::shared_ptr<rclcpp::Context> TestDomainBridgeQosMatching::context_2_;
rclcpp::NodeOptions TestDomainBridgeQosMatching::node_options_1_;
rclcpp::NodeOptions TestDomainBridgeQosMatching::node_options_2_;

TEST_F(TestDomainBridgeQosMatching, qos_matches_topic_exists_before_bridge)
{
  const std::string topic_name("test_topic_exists_before_bridge");

  // Create a publisher on domain 1
  auto node_1 = std::make_shared<rclcpp::Node>(
    "test_topic_exists_before_bridge_node_1", node_options_1_);
  rclcpp::QoS qos(1);
  qos.best_effort()
  .transient_local()
  .deadline(rclcpp::Duration(123, 456u))
  .lifespan(rclcpp::Duration(554, 321u))
  .liveliness(rclcpp::LivelinessPolicy::Automatic);
  auto pub = node_1->create_publisher<test_msgs::msg::BasicTypes>(topic_name, qos);

  // Bridge the publisher topic to domain 2
  domain_bridge::DomainBridge bridge;
  bridge.bridge_topic(topic_name, "test_msgs/msg/BasicTypes", domain_1_, domain_2_);

  // Wait for bridge publisher to appear on domain 2
  auto node_2 = std::make_shared<rclcpp::Node>(
    "test_topic_exists_before_bridge_node_2", node_options_2_);
  ASSERT_TRUE(wait_for_publisher(node_2, topic_name));

  // Assert the QoS of the bridged publisher matches
  std::vector<rclcpp::TopicEndpointInfo> endpoint_info_vec =
    node_2->get_publishers_info_by_topic(topic_name);
  ASSERT_EQ(endpoint_info_vec.size(), 1u);
  const rclcpp::QoS & bridged_qos = endpoint_info_vec[0].qos_profile();
  EXPECT_EQ(bridged_qos.reliability(), qos.reliability());
  EXPECT_EQ(bridged_qos.durability(), qos.durability());
  EXPECT_EQ(bridged_qos.liveliness(), qos.liveliness());
  EXPECT_EQ(bridged_qos.deadline(), qos.deadline());
  EXPECT_EQ(bridged_qos.lifespan(), qos.lifespan());
}
