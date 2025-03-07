#ifndef ROAST_BEHAVIOR_TREE__BT_CANCEL_ACTION_NODE_HPP_
#define ROAST_BEHAVIOR_TREE__BT_CANCEL_ACTION_NODE_HPP_

#include <chrono>
#include <memory>
#include <string>

#include "behaviortree_cpp_v3/action_node.h"
#include "nav2_util/node_utils.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "roast_behavior_tree/bt_conversions.hpp"

namespace roast_behavior_tree {

using namespace std::chrono_literals;  // NOLINT

/**
 * @brief Abstract class representing an action for cancelling BT node
 * @tparam ActionT Type of action
 */
template <class ActionT>
class BtCancelActionNode : public BT::ActionNodeBase {
 public:
  /**
   * @brief A roast_behavior_tree::BtCancelActionNode constructor
   * @param xml_tag_name Name for the XML tag for this node
   * @param action_name Action name this node creates a client for
   * @param conf BT node configuration
   */
  BtCancelActionNode(const std::string &xml_tag_name,
                     const std::string &action_name,
                     const BT::NodeConfiguration &conf)
      : BT::ActionNodeBase(xml_tag_name, conf), action_name_(action_name) {
    node_ = config().blackboard->template get<rclcpp::Node::SharedPtr>("node");
    callback_group_ = node_->create_callback_group(
        rclcpp::CallbackGroupType::MutuallyExclusive, false);
    callback_group_executor_.add_callback_group(
        callback_group_, node_->get_node_base_interface());

    // Get the required items from the blackboard
    server_timeout_ =
        config().blackboard->template get<std::chrono::milliseconds>(
            "server_timeout");
    getInput<std::chrono::milliseconds>("server_timeout", server_timeout_);

    action_name_ = action_name;
    createActionClient(action_name_);

    // Give the derive class a chance to do any initialization
    RCLCPP_DEBUG(node_->get_logger(), "\"%s\" BtCancelActionNode initialized",
                 xml_tag_name.c_str());
  }

  BtCancelActionNode() = delete;

  virtual ~BtCancelActionNode() {}

  /**
   * @brief Create instance of an action client
   * @param action_name Action name to create client for
   */
  void createActionClient(const std::string &action_name) {
    // Now that we have the ROS node to use, create the action client for this
    // BT action
    action_client_ = rclcpp_action::create_client<ActionT>(node_, action_name,
                                                           callback_group_);

    // Make sure the server is actually there before continuing
    RCLCPP_DEBUG(node_->get_logger(), "Waiting for \"%s\" action server",
                 action_name.c_str());
    if (!action_client_->wait_for_action_server(
            server_timeout_))  // convert to seconds
    {
      RCLCPP_ERROR(node_->get_logger(),
                   "[cancel_action] \"%s\" action server not available after "
                   "waiting for 3 s",
                   action_name.c_str());
      throw std::runtime_error(std::string("Action server ") + action_name +
                               std::string(" not available"));
    }
  }

  /**
   * @brief Any subclass of BtCancelActionNode that accepts parameters must
   * provide a providedPorts method and call providedBasicPorts in it.
   * @param addition Additional ports to add to BT port list
   * @return BT::PortsList Containing basic ports along with node-specific ports
   */
  static BT::PortsList providedBasicPorts(BT::PortsList addition) {
    BT::PortsList basic = {
        BT::InputPort<std::chrono::milliseconds>("server_timeout")};
    basic.insert(addition.begin(), addition.end());

    return basic;
  }

  void halt() {}

  /**
   * @brief Creates list of BT ports
   * @return BT::PortsList Containing basic ports along with node-specific ports
   */
  static BT::PortsList providedPorts() { return providedBasicPorts({}); }

  /**
   * @brief The main override required by a BT action
   * @return BT::NodeStatus Status of tick execution
   */
  BT::NodeStatus tick() override {
    // setting the status to RUNNING to notify the BT Loggers (if any)
    setStatus(BT::NodeStatus::RUNNING);

    // Cancel all the goals specified before 10ms from current time
    // to avoid async communication error

    rclcpp::Time goal_expiry_time =
        node_->now() - std::chrono::milliseconds(10);

    auto future_cancel =
        action_client_->async_cancel_goals_before(goal_expiry_time);

    if (callback_group_executor_.spin_until_future_complete(future_cancel,
                                                            server_timeout_) !=
        rclcpp::FutureReturnCode::SUCCESS) {
      RCLCPP_ERROR(node_->get_logger(),
                   "Failed to cancel the action server for %s",
                   action_name_.c_str());
      return BT::NodeStatus::FAILURE;
    }
    return BT::NodeStatus::SUCCESS;
  }

 protected:
  std::string action_name_;
  typename std::shared_ptr<rclcpp_action::Client<ActionT>> action_client_;

  // The node that will be used for any ROS operations
  rclcpp::Node::SharedPtr node_;
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  rclcpp::executors::SingleThreadedExecutor callback_group_executor_;

  // The timeout value while waiting for response from a server when a
  // new action goal is canceled
  std::chrono::milliseconds server_timeout_;
};

}  // namespace roast_behavior_tree

#endif  // ROAST_BEHAVIOR_TREE__BT_CANCEL_ACTION_NODE_HPP_
