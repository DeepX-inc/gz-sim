/*
 * Copyright (C) 2020 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifndef GZ_GAZEBO_COMPONENTS_LINEARVELOCITYCMD_HH_
#define GZ_GAZEBO_COMPONENTS_LINEARVELOCITYCMD_HH_

#include <gz/math/Vector3.hh>

#include <gz/sim/config.hh>

#include <gz/sim/components/Factory.hh>
#include <gz/sim/components/Component.hh>

namespace gz
{
namespace sim
{
// Inline bracket to help doxygen filtering.
inline namespace IGNITION_GAZEBO_VERSION_NAMESPACE {
namespace components
{
  // \brief A component type that contains the commanded linear velocity of an
  /// entity represented by gz::math::Vector3d, expressed in the entity's
  /// frame.
  using LinearVelocityCmd = Component<
    math::Vector3d, class LinearVelocityCmdTag>;
  IGN_GAZEBO_REGISTER_COMPONENT(
      "ign_gazebo_components.LinearVelocityCmd", LinearVelocityCmd)

  /// \brief A component type that contains the commanded linear velocity of an
  /// entity represented by gz::math::Vector3d, expressed in the world
  /// frame.
  using WorldLinearVelocityCmd =
      Component<math::Vector3d, class WorldLinearVelocityCmdTag>;
  IGN_GAZEBO_REGISTER_COMPONENT(
      "ign_gazebo_components.WorldLinearVelocityCmd", WorldLinearVelocityCmd)
}
}
}
}

#endif
