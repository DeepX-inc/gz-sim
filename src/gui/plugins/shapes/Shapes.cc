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

#include "Shapes.hh"

#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/stringmsg.pb.h>

#include <algorithm>
#include <iostream>
#include <string>

#include <gz/common/Console.hh>
#include <gz/gui/Application.hh>
#include <gz/gui/MainWindow.hh>
#include <gz/plugin/Register.hh>
#include <gz/transport/Node.hh>
#include <gz/transport/Publisher.hh>

#include "gz/sim/gui/GuiEvents.hh"

namespace gz::sim
{
  class ShapesPrivate
  {
  };
}

using namespace gz;
using namespace sim;

/////////////////////////////////////////////////
Shapes::Shapes()
  : gz::gui::Plugin(),
  dataPtr(std::make_unique<ShapesPrivate>())
{
}

/////////////////////////////////////////////////
Shapes::~Shapes() = default;

/////////////////////////////////////////////////
void Shapes::LoadConfig(const tinyxml2::XMLElement *)
{
  if (this->title.empty())
    this->title = "Shapes";

  // For shapes requests
  gz::gui::App()->findChild
    <gz::gui::MainWindow *>()->installEventFilter(this);
}

/////////////////////////////////////////////////
void Shapes::OnMode(const QString &_mode)
{
  std::string modelSdfString = _mode.toStdString();
  std::transform(modelSdfString.begin(), modelSdfString.end(),
                 modelSdfString.begin(), ::tolower);

  if (modelSdfString == "box")
  {
    modelSdfString = std::string("<?xml version=\"1.0\"?>"
                                 "<sdf version=\"1.6\">"
                                   "<model name=\"box\">"
                                     "<pose>0 0 0.5 0 0 0</pose>"
                                     "<link name=\"box_link\">"
                                       "<inertial>"
                                         "<inertia>"
                                           "<ixx>0.167</ixx>"
                                           "<ixy>0</ixy>"
                                           "<ixz>0</ixz>"
                                           "<iyy>0.167</iyy>"
                                           "<iyz>0</iyz>"
                                           "<izz>0.167</izz>"
                                         "</inertia>"
                                         "<mass>1.0</mass>"
                                       "</inertial>"
                                       "<collision name=\"box_collision\">"
                                         "<geometry>"
                                           "<box>"
                                             "<size>1 1 1</size>"
                                           "</box>"
                                         "</geometry>"
                                       "</collision>"
                                       "<visual name=\"box_visual\">"
                                         "<geometry>"
                                           "<box>"
                                             "<size>1 1 1</size>"
                                           "</box>"
                                         "</geometry>"
                                       "</visual>"
                                     "</link>"
                                   "</model>"
                                 "</sdf>");
  }
  else if (modelSdfString == "sphere")
  {
    modelSdfString = std::string("<?xml version=\"1.0\"?>"
                                 "<sdf version=\"1.6\">"
                                   "<model name=\"sphere\">"
                                     "<pose>0 0 0.5 0 0 0</pose>"
                                     "<link name=\"sphere_link\">"
                                       "<inertial>"
                                         "<inertia>"
                                           "<ixx>0.1</ixx>"
                                           "<ixy>0</ixy>"
                                           "<ixz>0</ixz>"
                                           "<iyy>0.1</iyy>"
                                           "<iyz>0</iyz>"
                                           "<izz>0.1</izz>"
                                         "</inertia>"
                                         "<mass>1.0</mass>"
                                       "</inertial>"
                                       "<collision name=\"sphere_collision\">"
                                         "<geometry>"
                                           "<sphere>"
                                             "<radius>0.5</radius>"
                                           "</sphere>"
                                         "</geometry>"
                                       "</collision>"
                                       "<visual name=\"sphere_visual\">"
                                         "<geometry>"
                                           "<sphere>"
                                             "<radius>0.5</radius>"
                                           "</sphere>"
                                         "</geometry>"
                                       "</visual>"
                                     "</link>"
                                   "</model>"
                                 "</sdf>");
  }
  else if (modelSdfString == "cylinder")
  {
    modelSdfString = std::string("<?xml version=\"1.0\"?>"
                                 "<sdf version=\"1.6\">"
                                   "<model name=\"cylinder\">"
                                     "<pose>0 0 0.5 0 0 0</pose>"
                                     "<link name=\"cylinder_link\">"
                                       "<inertial>"
                                         "<inertia>"
                                           "<ixx>0.146</ixx>"
                                           "<ixy>0</ixy>"
                                           "<ixz>0</ixz>"
                                           "<iyy>0.146</iyy>"
                                           "<iyz>0</iyz>"
                                           "<izz>0.125</izz>"
                                         "</inertia>"
                                         "<mass>1.0</mass>"
                                       "</inertial>"
                                       "<collision name=\"cylinder_collision\">"
                                         "<geometry>"
                                           "<cylinder>"
                                             "<radius>0.5</radius>"
                                             "<length>1.0</length>"
                                           "</cylinder>"
                                         "</geometry>"
                                       "</collision>"
                                       "<visual name=\"cylinder_visual\">"
                                         "<geometry>"
                                           "<cylinder>"
                                             "<radius>0.5</radius>"
                                             "<length>1.0</length>"
                                           "</cylinder>"
                                         "</geometry>"
                                       "</visual>"
                                     "</link>"
                                   "</model>"
                                 "</sdf>");
  }
  else
  {
    ignwarn << "Invalid model string " << modelSdfString << "\n";
    ignwarn << "The valid options are:\n";
    ignwarn << " - box\n";
    ignwarn << " - sphere\n";
    ignwarn << " - cylinder\n";
    return;
  }

  gui::events::SpawnPreviewModel event(modelSdfString);
  gz::gui::App()->sendEvent(
      gz::gui::App()->findChild<gz::gui::MainWindow *>(),
      &event);
}

// Register this plugin
IGNITION_ADD_PLUGIN(Shapes,
                    gz::gui::Plugin)
